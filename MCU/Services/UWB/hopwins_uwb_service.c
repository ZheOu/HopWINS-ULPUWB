#include "hopwins_uwb_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "deca_device_api.h"
#include "deca_regs.h"
#include "hopwins_dw1000.h"
#include "hopwins_serial.h"

/* Shared radio configuration. */
#define UWB_PREAMBLE_SYMBOLS      4096U
#define UWB_STANDARD_SFD_SYMBOLS  8U
#define UWB_PAC_SYMBOLS           32U
#define UWB_SFD_TIMEOUT_SYMBOLS   \
    (UWB_PREAMBLE_SYMBOLS + 1U + UWB_STANDARD_SFD_SYMBOLS - UWB_PAC_SYMBOLS)

_Static_assert(UWB_SFD_TIMEOUT_SYMBOLS == 4073U, "Unexpected DW1000 SFD timeout");

static dwt_config_t uwb_radio_config = {
    5U,
    DWT_PRF_64M,
    DWT_PLEN_4096,
    DWT_PAC32,
    9U,
    9U,
    0U,
    DWT_BR_850K,
    DWT_PHRMODE_STD,
    UWB_SFD_TIMEOUT_SYMBOLS,
};

static void hopwins_uwb_report_bytes(const char *message, size_t length)
{
    (void)hopwins_serial_send((const uint8_t *)message, length);
}

static void hopwins_uwb_report(const char *message)
{
    hopwins_uwb_report_bytes(message, strlen(message));
}

static uint64_t hopwins_uwb_uint40_from_bytes(const uint8 bytes[5])
{
    uint64_t value = 0U;
    int index;

    for (index = 4; index >= 0; --index)
    {
        value = (value << 8U) | bytes[index];
    }

    return value;
}

/* Shared test packet: type byte followed by a big-endian 16-bit index. */
#define UWB_TEST_PACKET_TYPE                0x11U
#define UWB_TEST_PACKET_PAYLOAD_SIZE        3U
#define UWB_TEST_PACKET_FCS_SIZE            2U
#define UWB_TEST_PACKET_FRAME_SIZE          \
    (UWB_TEST_PACKET_PAYLOAD_SIZE + UWB_TEST_PACKET_FCS_SIZE)
#define UWB_TEST_PACKET_TYPE_OFFSET         0U
#define UWB_TEST_PACKET_INDEX_HIGH_OFFSET   1U
#define UWB_TEST_PACKET_INDEX_LOW_OFFSET    2U

/* Periodic-TX task. Delayed TX is only the timing mechanism. */
#define UWB_PERIODIC_TX_PERIOD_MS            5ULL
/* DW1000 device time runs at 499.2 MHz multiplied by 128. */
#define UWB_DW1000_TIME_COUNTER_HZ           63897600000ULL
#define UWB_MILLISECONDS_PER_SECOND          1000ULL
#define UWB_PERIODIC_TX_PERIOD_DTU           \
    ((UWB_DW1000_TIME_COUNTER_HZ / UWB_MILLISECONDS_PER_SECOND) * \
     UWB_PERIODIC_TX_PERIOD_MS)
#define UWB_TIMESTAMP_40BIT_MASK             0xFFFFFFFFFFULL
#define UWB_DELAYED_TIME_MASK                0xFFFFFFFEUL
#define UWB_DELAYED_TX_QUANTUM_DTU           512ULL
#define UWB_SYS_STATUS_TX_ERROR_OFFSET       3U
#define UWB_TX_REPORT_BUFFER_SIZE            128U

_Static_assert(UWB_PERIODIC_TX_PERIOD_DTU == 319488000ULL,
               "Unexpected DW1000 5 ms period");
_Static_assert((UWB_PERIODIC_TX_PERIOD_DTU % UWB_DELAYED_TX_QUANTUM_DTU) == 0U,
               "Periodic TX period must match delayed-TX resolution");
_Static_assert(UWB_TX_REPORT_BUFFER_SIZE <= HOPWINS_SERIAL_MAX_PAYLOAD_SIZE,
               "TX report exceeds serial payload limit");

static uint8 uwb_periodic_tx_payload[UWB_TEST_PACKET_PAYLOAD_SIZE] = {
    UWB_TEST_PACKET_TYPE, 0U, 0U,
};
static uint64_t uwb_periodic_tx_next_time_dtu;
static uint16_t uwb_periodic_tx_index;
static bool uwb_periodic_tx_running;

static uint64_t hopwins_uwb_read_system_time(void)
{
    uint8 timestamp[5];

    dwt_readsystime(timestamp);
    return hopwins_uwb_uint40_from_bytes(timestamp);
}

static uint64_t hopwins_uwb_read_tx_time(void)
{
    uint8 timestamp[5];

    dwt_readtxtimestamp(timestamp);
    return hopwins_uwb_uint40_from_bytes(timestamp);
}

static bool hopwins_uwb_periodic_tx_write_payload(void)
{
    uwb_periodic_tx_payload[UWB_TEST_PACKET_INDEX_HIGH_OFFSET] =
        (uint8)(uwb_periodic_tx_index >> 8U);
    uwb_periodic_tx_payload[UWB_TEST_PACKET_INDEX_LOW_OFFSET] =
        (uint8)uwb_periodic_tx_index;

    return dwt_writetxdata(UWB_TEST_PACKET_FRAME_SIZE,
                           uwb_periodic_tx_payload,
                           0U) == DWT_SUCCESS;
}

static bool hopwins_uwb_periodic_tx_schedule(uint64_t timestamp_dtu)
{
    uint32 delayed_time =
        ((uint32)(timestamp_dtu >> 8U)) & UWB_DELAYED_TIME_MASK;

    dwt_write16bitoffsetreg(SYS_STATUS_ID,
                            UWB_SYS_STATUS_TX_ERROR_OFFSET,
                            SYS_STATUS_TXERR);
    dwt_setdelayedtrxtime(delayed_time);
    return dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS;
}

static bool hopwins_uwb_periodic_tx_schedule_from_now(void)
{
    uwb_periodic_tx_next_time_dtu =
        (hopwins_uwb_read_system_time() + UWB_PERIODIC_TX_PERIOD_DTU) &
        UWB_TIMESTAMP_40BIT_MASK;
    return hopwins_uwb_periodic_tx_schedule(uwb_periodic_tx_next_time_dtu);
}

static void hopwins_uwb_periodic_tx_report(uint16_t index,
                                           uint64_t tx_timestamp_dtu,
                                           const char *status)
{
    char message[UWB_TX_REPORT_BUFFER_SIZE];
    int length = snprintf(message,
                          sizeof(message),
                          "[UWB][TX] event=sent type=0x%02X idx=%u tx_ts=%" PRIu64
                          " status=%s\r\n",
                          UWB_TEST_PACKET_TYPE,
                          (unsigned int)index,
                          tx_timestamp_dtu,
                          status);

    if ((length > 0) && ((size_t)length < sizeof(message)))
    {
        hopwins_uwb_report_bytes(message, (size_t)length);
    }
}

bool hopwins_uwb_periodic_tx_start(void)
{
    char message[UWB_TX_REPORT_BUFFER_SIZE];
    int length;

    uwb_periodic_tx_index = 0U;
    uwb_periodic_tx_running = false;

    if (!hopwins_uwb_periodic_tx_write_payload())
    {
        return false;
    }

    dwt_writetxfctrl(UWB_TEST_PACKET_FRAME_SIZE, 0U, 0);
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_TX);
    uwb_periodic_tx_running = hopwins_uwb_periodic_tx_schedule_from_now();

    if (uwb_periodic_tx_running)
    {
        length = snprintf(message,
                          sizeof(message),
                          "[UWB][TX] event=start period_ms=%" PRIu64
                          " type=0x%02X idx=%u\r\n",
                          (uint64_t)UWB_PERIODIC_TX_PERIOD_MS,
                          UWB_TEST_PACKET_TYPE,
                          (unsigned int)uwb_periodic_tx_index);
        if ((length > 0) && ((size_t)length < sizeof(message)))
        {
            hopwins_uwb_report_bytes(message, (size_t)length);
        }
    }

    return uwb_periodic_tx_running;
}

void hopwins_uwb_periodic_tx_process(void)
{
    uint16_t completed_index;
    uint64_t tx_timestamp_dtu;
    const char *report_status = "ok";

    if (!uwb_periodic_tx_running ||
        ((dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS) == 0U))
    {
        return;
    }

    completed_index = uwb_periodic_tx_index;
    tx_timestamp_dtu = hopwins_uwb_read_tx_time();
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
    ++uwb_periodic_tx_index;

    if (!hopwins_uwb_periodic_tx_write_payload())
    {
        uwb_periodic_tx_running = false;
        hopwins_uwb_periodic_tx_report(completed_index,
                                       tx_timestamp_dtu,
                                       "payload_error");
        return;
    }

    uwb_periodic_tx_next_time_dtu =
        (uwb_periodic_tx_next_time_dtu + UWB_PERIODIC_TX_PERIOD_DTU) &
        UWB_TIMESTAMP_40BIT_MASK;

    if (!hopwins_uwb_periodic_tx_schedule(uwb_periodic_tx_next_time_dtu))
    {
        report_status = "rescheduled";
        if (!hopwins_uwb_periodic_tx_schedule_from_now())
        {
            uwb_periodic_tx_running = false;
            report_status = "stopped";
        }
    }

    hopwins_uwb_periodic_tx_report(completed_index,
                                   tx_timestamp_dtu,
                                   report_status);
}

/* Polling-RX task and its private diagnostics. */
#define UWB_RX_REPORT_BUFFER_SIZE          256U
#define UWB_RX_TRACKING_OFFSET_SIGN_BIT    (1UL << 18U)
#define UWB_RX_RECOVERY_STATUS             \
    (SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO | SYS_STATUS_RXOVRR)

_Static_assert(UWB_RX_REPORT_BUFFER_SIZE <= HOPWINS_SERIAL_MAX_PAYLOAD_SIZE,
               "RX report exceeds serial payload limit");

typedef struct
{
    uint64_t adjusted_timestamp_dtu;
    uint64_t raw_timestamp_dtu;
    uint64_t system_timestamp_dtu;
    uint64_t system_status;
    uint32_t time_tracking_interval;
    int32_t time_tracking_offset;
    int32_t carrier_integrator;
    uint16_t frame_length;
    uint16_t first_path_index_10_6;
    uint16_t preamble_count;
    uint16_t first_path_amplitude_1;
    uint16_t first_path_amplitude_2;
    uint16_t first_path_amplitude_3;
    uint16_t cir_power;
    uint16_t standard_noise;
    uint16_t max_noise;
    uint8_t resampler_delay;
    bool payload_valid;
    uint8_t packet_type;
    uint16_t packet_index;
} hopwins_uwb_rx_diagnostics_t;

static void hopwins_uwb_rx_read_diagnostics(hopwins_uwb_rx_diagnostics_t *diagnostics)
{
    dwt_rxdiag_t device_diagnostics;
    uint8 bytes[5];
    uint32_t frame_info;
    uint32_t tracking_offset_register;
    uint32_t tracking_offset;
    uint8 payload[UWB_TEST_PACKET_PAYLOAD_SIZE];

    dwt_readfromdevice(SYS_STATUS_ID, SYS_STATUS_OFFSET, SYS_STATUS_LEN, bytes);
    diagnostics->system_status = hopwins_uwb_uint40_from_bytes(bytes);
    frame_info = dwt_read32bitreg(RX_FINFO_ID);
    diagnostics->frame_length = (uint16_t)(frame_info & RX_FINFO_RXFLEN_MASK);
    diagnostics->payload_valid =
        diagnostics->frame_length == UWB_TEST_PACKET_FRAME_SIZE;
    diagnostics->packet_type = 0U;
    diagnostics->packet_index = 0U;
    if (diagnostics->payload_valid)
    {
        dwt_readrxdata(payload, UWB_TEST_PACKET_PAYLOAD_SIZE, 0U);
        diagnostics->packet_type = payload[UWB_TEST_PACKET_TYPE_OFFSET];
        diagnostics->packet_index =
            ((uint16_t)payload[UWB_TEST_PACKET_INDEX_HIGH_OFFSET] << 8U) |
            payload[UWB_TEST_PACKET_INDEX_LOW_OFFSET];
    }

    dwt_readrxtimestamp(bytes);
    diagnostics->adjusted_timestamp_dtu = hopwins_uwb_uint40_from_bytes(bytes);
    dwt_readfromdevice(RX_TIME_ID, RX_TIME_FP_RAWST_OFFSET, 5U, bytes);
    diagnostics->raw_timestamp_dtu = hopwins_uwb_uint40_from_bytes(bytes);
    diagnostics->system_timestamp_dtu = hopwins_uwb_read_system_time();

    dwt_readdiagnostics(&device_diagnostics);
    diagnostics->first_path_index_10_6 = device_diagnostics.firstPath;
    diagnostics->preamble_count = device_diagnostics.rxPreamCount;
    diagnostics->first_path_amplitude_1 = device_diagnostics.firstPathAmp1;
    diagnostics->first_path_amplitude_2 = device_diagnostics.firstPathAmp2;
    diagnostics->first_path_amplitude_3 = device_diagnostics.firstPathAmp3;
    diagnostics->cir_power = device_diagnostics.maxGrowthCIR;
    diagnostics->standard_noise = device_diagnostics.stdNoise;
    diagnostics->max_noise = device_diagnostics.maxNoise;

    diagnostics->time_tracking_interval = dwt_read32bitreg(RX_TTCKI_ID);
    tracking_offset_register = dwt_read32bitreg(RX_TTCKO_ID);
    tracking_offset = tracking_offset_register & RX_TTCKO_RXTOFS_MASK;
    if ((tracking_offset & UWB_RX_TRACKING_OFFSET_SIGN_BIT) != 0U)
    {
        tracking_offset |= ~RX_TTCKO_RXTOFS_MASK;
    }
    diagnostics->time_tracking_offset = (int32_t)tracking_offset;
    diagnostics->resampler_delay =
        (uint8_t)((tracking_offset_register & RX_TTCKO_RSMPDEL_MASK) >> 24U);
    diagnostics->carrier_integrator = (int32_t)dwt_readcarrierintegrator();
}

static void hopwins_uwb_rx_report(const hopwins_uwb_rx_diagnostics_t *diagnostics)
{
    char message[UWB_RX_REPORT_BUFFER_SIZE];
    int length;

    length = snprintf(message,
                      sizeof(message),
                      "[UWB][RX] valid=%u type=0x%02X idx=%u"
                      " len=%u rx=%" PRIu64 " raw=%" PRIu64
                      " sys=%" PRIu64 " fp_q6=%u pre=%u"
                      " amp=%u/%u/%u cir=%u noise=%u/%u"
                      " ttcki=%" PRIu32 " ttcko=%" PRId32
                      " ci=%" PRId32 " rsd=%u st=0x%010" PRIX64 "\r\n",
                      diagnostics->payload_valid ? 1U : 0U,
                      (unsigned int)diagnostics->packet_type,
                      (unsigned int)diagnostics->packet_index,
                      (unsigned int)diagnostics->frame_length,
                      diagnostics->adjusted_timestamp_dtu,
                      diagnostics->raw_timestamp_dtu,
                      diagnostics->system_timestamp_dtu,
                      (unsigned int)diagnostics->first_path_index_10_6,
                      (unsigned int)diagnostics->preamble_count,
                      (unsigned int)diagnostics->first_path_amplitude_1,
                      (unsigned int)diagnostics->first_path_amplitude_2,
                      (unsigned int)diagnostics->first_path_amplitude_3,
                      (unsigned int)diagnostics->cir_power,
                      (unsigned int)diagnostics->standard_noise,
                      (unsigned int)diagnostics->max_noise,
                      diagnostics->time_tracking_interval,
                      diagnostics->time_tracking_offset,
                      diagnostics->carrier_integrator,
                      (unsigned int)diagnostics->resampler_delay,
                      diagnostics->system_status);

    if ((length > 0) && ((size_t)length < sizeof(message)))
    {
        hopwins_uwb_report_bytes(message, (size_t)length);
    }
}

static void hopwins_uwb_rx_report_error(uint32_t status)
{
    char message[48];
    int length = snprintf(message,
                          sizeof(message),
                          "[UWB][RX] event=error status=0x%08" PRIX32 "\r\n",
                          status);

    if ((length > 0) && ((size_t)length < sizeof(message)))
    {
        hopwins_uwb_report_bytes(message, (size_t)length);
    }
}

bool hopwins_uwb_rx_start(void)
{
    dwt_write32bitreg(SYS_STATUS_ID,
                      SYS_STATUS_ALL_RX_GOOD |
                          SYS_STATUS_ALL_RX_ERR |
                          SYS_STATUS_ALL_RX_TO |
                          SYS_STATUS_RXOVRR);

    if (dwt_rxenable(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS)
    {
        return false;
    }

    hopwins_uwb_report("[UWB][RX] event=start\r\n");
    return true;
}

void hopwins_uwb_rx_process(void)
{
    uint32_t status = dwt_read32bitreg(SYS_STATUS_ID);

    if ((status & UWB_RX_RECOVERY_STATUS) != 0U)
    {
        dwt_write32bitreg(SYS_STATUS_ID,
                          UWB_RX_RECOVERY_STATUS | SYS_STATUS_ALL_RX_GOOD);
        dwt_forcetrxoff();
        dwt_rxreset();
        (void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
        hopwins_uwb_rx_report_error(status);
    }
    else if ((status & SYS_STATUS_RXFCG) != 0U)
    {
        hopwins_uwb_rx_diagnostics_t diagnostics = {0};

        hopwins_uwb_rx_read_diagnostics(&diagnostics);
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_GOOD);
        (void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
        hopwins_uwb_rx_report(&diagnostics);
    }
}

bool hopwins_uwb_init(void)
{
    hopwins_uwb_report("[UWB] reset and initialise\r\n");

    if (hopwins_dw1000_init() != DWT_SUCCESS)
    {
        hopwins_uwb_report("[UWB] initialisation failed\r\n");
        return false;
    }

    dwt_configure(&uwb_radio_config);
    hopwins_uwb_report("[UWB] ready: ch5 prf64 plen4096 pac32 code9 rate850k sfdTO4073\r\n");
    return true;
}
