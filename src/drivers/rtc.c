#include "drivers/rtc.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"

#define RTC_INDEX_PORT 0x70U
#define RTC_DATA_PORT 0x71U
#define RTC_NMI_DISABLE 0x80U

#define RTC_REG_SECONDS 0x00U
#define RTC_REG_MINUTES 0x02U
#define RTC_REG_HOURS 0x04U
#define RTC_REG_DAY 0x07U
#define RTC_REG_MONTH 0x08U
#define RTC_REG_YEAR 0x09U
#define RTC_REG_STATUS_A 0x0AU
#define RTC_REG_STATUS_B 0x0BU
#define RTC_REG_CENTURY 0x32U

#define RTC_STATUS_A_UPDATE_IN_PROGRESS 0x80U
#define RTC_STATUS_B_24_HOUR 0x02U
#define RTC_STATUS_B_BINARY 0x04U
#define RTC_UPDATE_POLL_LIMIT 10000U
#define RTC_STABLE_READ_LIMIT 4U
#define RTC_DEFAULT_CENTURY 20U
#define RTC_MONTH_COUNT 12U

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
    uint8_t status_b;
} rtc_raw_snapshot_t;

static rtc_status_t rtc_status;

static void rtc_outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t rtc_inb(uint16_t port) {
    uint8_t value;

    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t rtc_read_register(uint8_t reg) {
    rtc_outb(RTC_INDEX_PORT, (uint8_t)(reg | RTC_NMI_DISABLE));
    return rtc_inb(RTC_DATA_PORT);
}

static int rtc_wait_update_complete(void) {
    for (uint32_t attempt = 0; attempt < RTC_UPDATE_POLL_LIMIT;
         attempt++) {
        if (!(rtc_read_register(RTC_REG_STATUS_A) &
              RTC_STATUS_A_UPDATE_IN_PROGRESS)) return OK;
    }
    LOG_ERROR("RTC", "CMOS permaneceu em atualizacao");
    return ERR_TIMEOUT;
}

static int rtc_read_raw_once(rtc_raw_snapshot_t* out_snapshot) {
    if (!out_snapshot) {
        LOG_ERROR("RTC", "Destino nulo na leitura CMOS");
        return ERR_NULL;
    }
    if (rtc_wait_update_complete() != OK) return ERR_TIMEOUT;
    out_snapshot->seconds = rtc_read_register(RTC_REG_SECONDS);
    out_snapshot->minutes = rtc_read_register(RTC_REG_MINUTES);
    out_snapshot->hours = rtc_read_register(RTC_REG_HOURS);
    out_snapshot->day = rtc_read_register(RTC_REG_DAY);
    out_snapshot->month = rtc_read_register(RTC_REG_MONTH);
    out_snapshot->year = rtc_read_register(RTC_REG_YEAR);
    out_snapshot->century = rtc_read_register(RTC_REG_CENTURY);
    out_snapshot->status_b = rtc_read_register(RTC_REG_STATUS_B);
    if (rtc_read_register(RTC_REG_STATUS_A) &
        RTC_STATUS_A_UPDATE_IN_PROGRESS) return ERR_TIMEOUT;
    return OK;
}

static int rtc_raw_equal(const rtc_raw_snapshot_t* first,
                         const rtc_raw_snapshot_t* second) {
    if (!first || !second) return 0;
    return first->seconds == second->seconds &&
           first->minutes == second->minutes &&
           first->hours == second->hours && first->day == second->day &&
           first->month == second->month && first->year == second->year &&
           first->century == second->century &&
           first->status_b == second->status_b;
}

static int rtc_read_raw(rtc_raw_snapshot_t* out_snapshot) {
    rtc_raw_snapshot_t first;
    rtc_raw_snapshot_t second;

    if (!out_snapshot) {
        LOG_ERROR("RTC", "Destino nulo no snapshot CMOS");
        return ERR_NULL;
    }
    for (uint32_t attempt = 0; attempt < RTC_STABLE_READ_LIMIT;
         attempt++) {
        if (rtc_read_raw_once(&first) != OK ||
            rtc_read_raw_once(&second) != OK) continue;
        if (rtc_raw_equal(&first, &second)) {
            *out_snapshot = first;
            return OK;
        }
    }
    LOG_ERROR("RTC", "Leituras CMOS nao convergiram");
    return ERR_TIMEOUT;
}

static int rtc_decode_value(uint8_t raw, uint8_t binary, uint8_t* out_value) {
    if (!out_value) {
        LOG_ERROR("RTC", "Destino nulo ao decodificar CMOS");
        return ERR_NULL;
    }
    if (binary) {
        *out_value = raw;
        return OK;
    }
    if ((raw & 0x0FU) > 9U || (raw >> 4U) > 9U) return ERR_INVALID;
    *out_value = (uint8_t)(((raw >> 4U) * 10U) + (raw & 0x0FU));
    return OK;
}

static uint8_t rtc_is_leap_year(uint16_t year) {
    return (uint8_t)((year % 4U == 0U) &&
                     ((year % 100U != 0U) || (year % 400U == 0U)));
}

static uint8_t rtc_days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[RTC_MONTH_COUNT] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if (month < 1U || month > RTC_MONTH_COUNT) return 0U;
    if (month == 2U && rtc_is_leap_year(year)) return 29U;
    return days[month - 1U];
}

static int rtc_datetime_valid(const rtc_datetime_t* datetime) {
    uint8_t days;

    if (!datetime || datetime->year < RTC_YEAR_MIN ||
        datetime->year > RTC_YEAR_MAX || datetime->month < 1U ||
        datetime->month > RTC_MONTH_COUNT || datetime->hour > 23U ||
        datetime->minute > 59U || datetime->second > 59U) return 0;
    days = rtc_days_in_month(datetime->year, datetime->month);
    return datetime->day >= 1U && datetime->day <= days;
}

static int rtc_decode_snapshot(const rtc_raw_snapshot_t* raw,
                               rtc_datetime_t* out_datetime) {
    uint8_t binary;
    uint8_t value;
    uint8_t hour;
    uint8_t century;
    int result;

    if (!raw || !out_datetime) {
        LOG_ERROR("RTC", "Argumento nulo ao converter CMOS");
        return ERR_NULL;
    }
    binary = (raw->status_b & RTC_STATUS_B_BINARY) ? 1U : 0U;
    result = rtc_decode_value(raw->seconds, binary, &out_datetime->second);
    if (result == OK) result = rtc_decode_value(
        raw->minutes, binary, &out_datetime->minute);
    if (result == OK) result = rtc_decode_value(raw->hours & 0x7FU,
                                                  binary, &hour);
    if (result == OK) result = rtc_decode_value(raw->day, binary,
                                                  &out_datetime->day);
    if (result == OK) result = rtc_decode_value(raw->month, binary,
                                                  &out_datetime->month);
    if (result == OK) result = rtc_decode_value(raw->year, binary, &value);
    if (result != OK) {
        LOG_ERROR("RTC", "Valor BCD/binario invalido no CMOS");
        return result;
    }
    if (!(raw->status_b & RTC_STATUS_B_24_HOUR)) {
        if (hour < 1U || hour > 12U) return ERR_INVALID;
        if (raw->hours & 0x80U) {
            if (hour < 12U) hour = (uint8_t)(hour + 12U);
        } else if (hour == 12U) {
            hour = 0U;
        }
    }
    out_datetime->hour = hour;
    if (raw->century) {
        result = rtc_decode_value(raw->century, binary, &century);
        if (result != OK) return result;
        out_datetime->year = (uint16_t)(century * 100U + value);
    } else {
        out_datetime->year = (uint16_t)(RTC_DEFAULT_CENTURY * 100U + value);
    }
    if (!rtc_datetime_valid(out_datetime)) {
        LOG_ERROR("RTC", "Data CMOS fora da politica UTC");
        return ERR_INVALID;
    }
    return OK;
}

int rtc_init(void) {
    rtc_datetime_t datetime;
    int result;

    LOG_INFO("RTC", "Inicializando RTC CMOS");
    if (rtc_status.initialized) {
        LOG_WARN("RTC", "RTC CMOS ja estava inicializado");
        return rtc_status.valid ? OK : rtc_status.last_error;
    }
    kmemset(&rtc_status, 0, sizeof(rtc_status));
    rtc_status.initialized = 1U;
    result = rtc_read_utc(&datetime);
    if (result != OK) {
        rtc_status.available = 0U;
        rtc_status.valid = 0U;
        rtc_status.last_error = result;
        LOG_ERROR("RTC", "RTC CMOS indisponivel");
        return result;
    }
    LOG_INFO("RTC", "RTC CMOS inicializado com UTC valido");
    return OK;
}

int rtc_read_utc(rtc_datetime_t* out_datetime) {
    rtc_raw_snapshot_t raw;
    rtc_datetime_t datetime;
    int result;

    if (!out_datetime) {
        LOG_ERROR("RTC", "Destino nulo ao consultar UTC");
        return ERR_NULL;
    }
    if (!rtc_status.initialized) {
        LOG_ERROR("RTC", "Leitura UTC antes da inicializacao");
        return ERR_STATE;
    }
    rtc_status.reads++;
    result = rtc_read_raw(&raw);
    if (result == OK) result = rtc_decode_snapshot(&raw, &datetime);
    if (result != OK) {
        rtc_status.rejected_reads++;
        rtc_status.last_error = result;
        if (!rtc_status.valid) rtc_status.available = 0U;
        LOG_ERROR("RTC", "Falha ao validar leitura UTC do RTC");
        return result;
    }
    rtc_status.bcd_mode =
        (raw.status_b & RTC_STATUS_B_BINARY) ? 0U : 1U;
    rtc_status.twelve_hour_mode =
        (raw.status_b & RTC_STATUS_B_24_HOUR) ? 0U : 1U;
    rtc_status.century_available = raw.century ? 1U : 0U;
    rtc_status.stable_reads++;
    rtc_status.available = 1U;
    rtc_status.valid = 1U;
    rtc_status.utc = datetime;
    rtc_status.last_error = OK;
    *out_datetime = datetime;
    return OK;
}

int rtc_get_status(rtc_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("RTC", "Destino nulo ao consultar estado");
        return ERR_NULL;
    }
    *out_status = rtc_status;
    return OK;
}

int rtc_validate_state(void) {
    if (!rtc_status.initialized || rtc_status.bcd_mode > 1U ||
        rtc_status.twelve_hour_mode > 1U ||
        rtc_status.century_available > 1U ||
        rtc_status.stable_reads > rtc_status.reads ||
        (rtc_status.valid && !rtc_datetime_valid(&rtc_status.utc))) {
        LOG_ERROR("RTC", "Estado interno do RTC invalido");
        return ERR_STATE;
    }
    if (rtc_status.valid != rtc_status.available) {
        LOG_ERROR("RTC", "Disponibilidade e validade do RTC divergem");
        return ERR_STATE;
    }
    return OK;
}

int rtc_self_test(rtc_self_test_result_t* out_result) {
    rtc_raw_snapshot_t raw;
    rtc_datetime_t datetime;
    int result;

    if (!out_result) {
        LOG_ERROR("RTC", "Destino nulo no autoteste RTC");
        return ERR_NULL;
    }
    kmemset(out_result, 0, sizeof(*out_result));
    kmemset(&raw, 0, sizeof(raw));
    kmemset(&datetime, 0, sizeof(datetime));

    raw.seconds = 0x59U;
    raw.minutes = 0x58U;
    raw.hours = 0x23U;
    raw.day = 0x31U;
    raw.month = 0x12U;
    raw.year = 0x24U;
    raw.century = 0x20U;
    raw.status_b = RTC_STATUS_B_24_HOUR;
    result = rtc_decode_snapshot(&raw, &datetime);
    out_result->bcd_conversion = (uint8_t)(result == OK &&
        datetime.year == 2024U && datetime.month == 12U &&
        datetime.day == 31U && datetime.hour == 23U &&
        datetime.minute == 58U && datetime.second == 59U);

    raw.seconds = 59U;
    raw.minutes = 58U;
    raw.hours = 23U;
    raw.day = 29U;
    raw.month = 2U;
    raw.year = 24U;
    raw.century = 20U;
    raw.status_b = RTC_STATUS_B_BINARY | RTC_STATUS_B_24_HOUR;
    result = rtc_decode_snapshot(&raw, &datetime);
    out_result->binary_conversion = (uint8_t)(result == OK &&
        datetime.year == 2024U && datetime.month == 2U &&
        datetime.day == 29U && datetime.hour == 23U);
    raw.day = 30U;
    raw.month = 4U;
    result = rtc_decode_snapshot(&raw, &datetime);
    out_result->calendar_validation = (uint8_t)(result == OK &&
        datetime.month == 4U && datetime.day == 30U);

    raw.seconds = 0x06U;
    raw.minutes = 0x05U;
    raw.hours = 0x81U;
    raw.day = 0x01U;
    raw.month = 0x01U;
    raw.year = 0x24U;
    raw.century = 0x20U;
    raw.status_b = 0U;
    result = rtc_decode_snapshot(&raw, &datetime);
    out_result->twelve_hour_conversion = (uint8_t)(result == OK &&
        datetime.hour == 13U && datetime.minute == 5U &&
        datetime.second == 6U);

    raw.status_b = RTC_STATUS_B_BINARY | RTC_STATUS_B_24_HOUR;
    raw.seconds = 6U;
    raw.minutes = 5U;
    raw.hours = 23U;
    raw.year = 1U;
    raw.century = 20U;
    raw.month = 2U;
    raw.day = 29U;
    result = rtc_decode_snapshot(&raw, &datetime);
    out_result->invalid_dates_rejected = (uint8_t)(result != OK);
    raw.century = 21U;
    raw.month = 1U;
    raw.day = 1U;
    result = rtc_decode_snapshot(&raw, &datetime);
    out_result->invalid_dates_rejected = (uint8_t)(
        out_result->invalid_dates_rejected && result != OK);

    out_result->passed = (uint8_t)(out_result->bcd_conversion +
        out_result->binary_conversion + out_result->twelve_hour_conversion +
        out_result->calendar_validation + out_result->invalid_dates_rejected);
    out_result->failed = (uint8_t)(5U - out_result->passed);
    if (out_result->failed) {
        LOG_ERROR("RTC", "Autoteste RTC falhou");
        return ERR_STATE;
    }
    LOG_INFO("RTC", "Autoteste RTC concluido");
    return OK;
}
