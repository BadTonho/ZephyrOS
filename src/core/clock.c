#include "core/clock.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"
#include "drivers/rtc.h"

#define CLOCK_UNIX_EPOCH_YEAR 1970U
#define CLOCK_MONTH_COUNT 12U
#define CLOCK_SECONDS_PER_MINUTE 60U
#define CLOCK_MINUTES_PER_HOUR 60U
#define CLOCK_HOURS_PER_DAY 24U
#define CLOCK_SECONDS_PER_HOUR \
    (CLOCK_SECONDS_PER_MINUTE * CLOCK_MINUTES_PER_HOUR)
#define CLOCK_SECONDS_PER_DAY \
    (CLOCK_SECONDS_PER_HOUR * CLOCK_HOURS_PER_DAY)

static clock_status_t clock_status;
static uint32_t clock_last_raw_tick;
static uint64_t clock_tick_high;

static uint64_t clock_extend_tick(uint32_t raw_tick,
                                  uint32_t* last_raw_tick,
                                  uint64_t* tick_high) {
    if (raw_tick < *last_raw_tick) (*tick_high)++;
    *last_raw_tick = raw_tick;
    return (*tick_high << 32U) | raw_tick;
}

static uint8_t clock_is_leap_year(uint32_t year) {
    return (uint8_t)((year % 4U == 0U) &&
                     ((year % 100U != 0U) || (year % 400U == 0U)));
}

static uint8_t clock_days_in_month(uint32_t year, uint8_t month) {
    static const uint8_t days[CLOCK_MONTH_COUNT] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if (month < 1U || month > CLOCK_MONTH_COUNT) return 0U;
    if (month == 2U && clock_is_leap_year(year)) return 29U;
    return days[month - 1U];
}

static int clock_datetime_to_unix(const rtc_datetime_t* datetime,
                                  uint64_t* out_seconds) {
    uint64_t days = 0;

    if (!datetime || !out_seconds) {
        LOG_ERROR("CLOCK", "Argumento nulo ao converter UTC");
        return ERR_NULL;
    }
    if (datetime->year < CLOCK_UNIX_EPOCH_YEAR ||
        datetime->month < 1U || datetime->month > CLOCK_MONTH_COUNT ||
        datetime->hour >= CLOCK_HOURS_PER_DAY ||
        datetime->minute >= CLOCK_MINUTES_PER_HOUR ||
        datetime->second >= CLOCK_SECONDS_PER_MINUTE ||
        datetime->day < 1U ||
        datetime->day > clock_days_in_month(datetime->year,
                                            datetime->month)) {
        LOG_ERROR("CLOCK", "Data UTC invalida");
        return ERR_INVALID;
    }
    for (uint32_t year = CLOCK_UNIX_EPOCH_YEAR;
         year < datetime->year; year++) {
        days += clock_is_leap_year(year) ? 366U : 365U;
    }
    for (uint8_t month = 1U; month < datetime->month; month++) {
        days += clock_days_in_month(datetime->year, month);
    }
    days += (uint64_t)(datetime->day - 1U);
    *out_seconds = days * CLOCK_SECONDS_PER_DAY +
                   (uint64_t)datetime->hour * CLOCK_SECONDS_PER_HOUR +
                   (uint64_t)datetime->minute * CLOCK_SECONDS_PER_MINUTE +
                   datetime->second;
    return OK;
}

static int clock_update_monotonic(uint64_t* out_ticks) {
    uint32_t raw_tick;

    if (!out_ticks) {
        LOG_ERROR("CLOCK", "Destino nulo no tick monotono");
        return ERR_NULL;
    }
    if (!clock_status.initialized || !clock_status.monotonic_available) {
        LOG_ERROR("CLOCK", "Tick monotono indisponivel");
        return ERR_STATE;
    }
    raw_tick = timer_get_ticks();
    *out_ticks = clock_extend_tick(raw_tick, &clock_last_raw_tick,
                                   &clock_tick_high);
    clock_status.monotonic_wraps = clock_tick_high;
    return OK;
}

int clock_init(void) {
    rtc_datetime_t datetime;
    uint64_t unix_seconds;
    int result;

    LOG_INFO("CLOCK", "Inicializando relogio UTC e monotono");
    kmemset(&clock_status, 0, sizeof(clock_status));
    clock_status.initialized = 1U;
    clock_status.frequency = timer_get_frequency();
    if (!clock_status.frequency) {
        clock_status.last_error = ERR_STATE;
        LOG_ERROR("CLOCK", "Frequencia PIT indisponivel");
        return ERR_STATE;
    }
    clock_status.monotonic_available = 1U;
    clock_last_raw_tick = timer_get_ticks();
    clock_tick_high = 0;
    clock_status.anchor_monotonic_ticks =
        (uint64_t)clock_last_raw_tick;
    result = rtc_read_utc(&datetime);
    if (result == OK) result = clock_datetime_to_unix(
        &datetime, &unix_seconds);
    if (result != OK) {
        clock_status.source = CLOCK_SOURCE_NONE;
        clock_status.utc_available = 0U;
        clock_status.last_error = result;
        LOG_ERROR("CLOCK", "UTC confiavel indisponivel; monotono preservado");
        return ERR_UNAVAILABLE;
    }
    clock_status.source = CLOCK_SOURCE_RTC;
    clock_status.utc_available = 1U;
    clock_last_raw_tick = timer_get_ticks();
    clock_tick_high = 0U;
    clock_status.anchor_monotonic_ticks = (uint64_t)clock_last_raw_tick;
    clock_status.anchor_unix_seconds = unix_seconds;
    clock_status.last_error = OK;
    LOG_INFO("CLOCK", "Relogio UTC ancorado no RTC com sucesso");
    return OK;
}

int clock_get_utc(uint64_t* out_unix_seconds) {
    uint64_t now_ticks;
    uint64_t elapsed_ticks;
    uint64_t elapsed_seconds;

    if (!out_unix_seconds) {
        LOG_ERROR("CLOCK", "Destino nulo ao consultar UTC");
        return ERR_NULL;
    }
    if (!clock_status.initialized || !clock_status.utc_available) {
        LOG_ERROR("CLOCK", "UTC confiavel indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (clock_update_monotonic(&now_ticks) != OK) return ERR_STATE;
    elapsed_ticks = now_ticks - clock_status.anchor_monotonic_ticks;
    elapsed_seconds = elapsed_ticks / clock_status.frequency;
    *out_unix_seconds = clock_status.anchor_unix_seconds + elapsed_seconds;
    clock_status.reads++;
    return OK;
}

int clock_get_monotonic_ticks(uint64_t* out_ticks) {
    return clock_update_monotonic(out_ticks);
}

int clock_get_status(clock_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("CLOCK", "Destino nulo ao consultar estado");
        return ERR_NULL;
    }
    *out_status = clock_status;
    return OK;
}

int clock_validate_state(void) {
    uint64_t now_ticks;

    if (!clock_status.initialized || !clock_status.frequency ||
        !clock_status.monotonic_available ||
        clock_status.source > CLOCK_SOURCE_RTC ||
        (clock_status.utc_available &&
         (clock_status.source != CLOCK_SOURCE_RTC ||
          clock_status.last_error != OK))) {
        LOG_ERROR("CLOCK", "Estado interno do relogio invalido");
        return ERR_STATE;
    }
    if (clock_update_monotonic(&now_ticks) != OK ||
        now_ticks < clock_status.anchor_monotonic_ticks) {
        LOG_ERROR("CLOCK", "Relogio monotono retrocedeu");
        return ERR_STATE;
    }
    if (!clock_status.utc_available && clock_status.source !=
        CLOCK_SOURCE_NONE) {
        LOG_ERROR("CLOCK", "Fonte UTC ausente com source incorreto");
        return ERR_STATE;
    }
    return OK;
}

const char* clock_source_name(clock_source_t source) {
    if (source == CLOCK_SOURCE_RTC) return "RTC";
    return "NONE";
}

int clock_self_test(clock_self_test_result_t* out_result) {
    rtc_datetime_t datetime;
    uint64_t seconds = 0U;
    uint64_t rollover_ticks;
    uint32_t last_tick = 0xFFFFFFFEU;
    uint64_t tick_high = 0U;
    int result;

    if (!out_result) {
        LOG_ERROR("CLOCK", "Destino nulo no autoteste do relogio");
        return ERR_NULL;
    }
    kmemset(out_result, 0, sizeof(*out_result));

    kmemset(&datetime, 0, sizeof(datetime));
    datetime.year = 1970U;
    datetime.month = 1U;
    datetime.day = 1U;
    result = clock_datetime_to_unix(&datetime, &seconds);
    out_result->epoch_conversion = (uint8_t)(result == OK && seconds == 0U);

    datetime.year = 2000U;
    datetime.month = 2U;
    datetime.day = 29U;
    result = clock_datetime_to_unix(&datetime, &seconds);
    out_result->leap_year_conversion = (uint8_t)(
        result == OK && seconds == 951782400ULL);

    datetime.year = 2001U;
    datetime.month = 2U;
    datetime.day = 29U;
    result = clock_datetime_to_unix(&datetime, &seconds);
    out_result->invalid_date_rejected = (uint8_t)(result != OK);

    rollover_ticks = clock_extend_tick(1U, &last_tick, &tick_high);
    out_result->monotonic_rollover = (uint8_t)(
        rollover_ticks == ((1ULL << 32U) | 1U) && tick_high == 1U);
    out_result->invariants = (uint8_t)(clock_validate_state() == OK);
    out_result->passed = (uint8_t)(out_result->epoch_conversion +
        out_result->leap_year_conversion + out_result->invalid_date_rejected +
        out_result->monotonic_rollover + out_result->invariants);
    out_result->failed = (uint8_t)(5U - out_result->passed);
    if (out_result->failed) {
        LOG_ERROR("CLOCK", "Autoteste do relogio falhou");
        return ERR_STATE;
    }
    LOG_INFO("CLOCK", "Autoteste do relogio concluido");
    return OK;
}
