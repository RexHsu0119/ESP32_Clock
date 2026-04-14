#include "calendar_logic.h"

#include <time.h>

bool calendar_is_leap_year(int year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

int calendar_days_in_month(int year, int month)
{
    static const int days[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31};

    if (month < 1 || month > 12)
    {
        return 30;
    }

    if (month == 2 && calendar_is_leap_year(year))
    {
        return 29;
    }

    return days[month - 1];
}

int calendar_first_wday(int year, int month)
{
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    t.tm_hour = 12;
    t.tm_isdst = -1;

    time_t ts = mktime(&t);
    if (ts == (time_t)-1)
    {
        return 0;
    }

    if (localtime_r(&ts, &t) == NULL)
    {
        return 0;
    }

    return t.tm_wday; /* 0=Sun */
}

void calendar_reset_to_current_month(int *year, int *month)
{
    if (year == NULL || month == NULL)
    {
        return;
    }

    time_t now = time(NULL);
    struct tm t;

    if (localtime_r(&now, &t) != NULL)
    {
        *year = t.tm_year + 1900;
        *month = t.tm_mon + 1;
    }
    else
    {
        *year = 2025;
        *month = 1;
    }
}

void calendar_ensure_initialized(int *year, int *month)
{
    if (year == NULL || month == NULL)
    {
        return;
    }

    if (*year <= 0 || *month < 1 || *month > 12)
    {
        calendar_reset_to_current_month(year, month);
    }
}

void calendar_change_year(int *year, int *month, int delta)
{
    if (year == NULL || month == NULL)
    {
        return;
    }

    calendar_ensure_initialized(year, month);

    *year += delta;

    if (*year <= 0)
    {
        *year = 1;
    }
}

void calendar_change_month(int *year, int *month, int delta)
{
    if (year == NULL || month == NULL)
    {
        return;
    }

    calendar_ensure_initialized(year, month);

    *month += delta;

    while (*month < 1)
    {
        *month += 12;
        (*year)--;
    }

    while (*month > 12)
    {
        *month -= 12;
        (*year)++;
    }
}