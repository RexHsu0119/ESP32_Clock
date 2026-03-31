@echo off
setlocal

cd /d "%~dp0"

set "OUT=selected_sources.txt"

if exist "%OUT%" del /f /q "%OUT%"

call :append "main\main.c"

call :append "components\button\button.c"
call :append "components\button\button.h"

call :append "components\display\display.c"
call :append "components\display\display.h"
call :append "components\display\drivers\lcd\esp_lcd_st7735.c"
call :append "components\display\drivers\lcd\esp_lcd_st7735.h"

call :append "components\rtc\rtc.c"
call :append "components\rtc\my_rtc.h"

call :append "components\wifi\wifi.c"
call :append "components\wifi\wifi.h"

echo.
echo 完成，已輸出到 %OUT%
start notepad "%OUT%"
goto :eof

:append
if exist "%~1" (
    >>"%OUT%" echo ==============================
    >>"%OUT%" echo %~1
    >>"%OUT%" echo ==============================
    type "%~1" >> "%OUT%"
    >>"%OUT%" echo.
    >>"%OUT%" echo.
) else (
    echo [略過] 找不到檔案: %~1
)
goto :eof