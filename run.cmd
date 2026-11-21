@echo off
setlocal
set "ROOT=%~dp0"
if not exist "%ROOT%build\run.exe" (
    echo [buyer-assistant] build\run.exe not found. Build the project first: 1>&2
    echo   cmake -S "%ROOT%." -B "%ROOT%build" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release 1>&2
    echo   cmake --build "%ROOT%build" 1>&2
    exit /b 3
)
"%ROOT%build\run.exe" --catalog "%ROOT%data\catalog.json" %*
