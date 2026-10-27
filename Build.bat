@echo off
rem Builds Pico Cheat Menu for the DSpico and puts the files for the sd card in "_pico" next to this script.
rem Requires Git and Docker Desktop.
setlocal
rem Docker Desktop may not be on the PATH when it is installed for the current user only.
where docker >nul 2>&1 || if exist "%LOCALAPPDATA%\Programs\DockerDesktop\resources\bin\docker.exe" set "PATH=%LOCALAPPDATA%\Programs\DockerDesktop\resources\bin;%PATH%"
set "PROJECT=%~dp0"
set "OUTPUT=%PROJECT%_pico"

docker info >nul 2>&1
if errorlevel 1 (
    echo ERROR: Docker is not running. Start Docker Desktop, wait until it is ready and try again.
    goto :end_with_error
)

rem libtwl is a git submodule: fetch it if it is missing.
if not exist "%PROJECT%source\libs\libtwl\Makefile" (
    echo Fetching submodules...
    git -C "%PROJECT%." submodule update --init --recursive || goto :error
)

echo Preparing the Docker image pico-cheat-menu-build...
docker build -q -t pico-cheat-menu-build "%PROJECT%docker" >nul || goto :error

echo Building Pico Cheat Menu...
docker run --rm -v "%PROJECT%:/work" -w /work/source pico-cheat-menu-build make -j%NUMBER_OF_PROCESSORS% PICO_PLATFORM=DSPICO || goto :error

if exist "%OUTPUT%" rmdir /s /q "%OUTPUT%" || goto :error
mkdir "%OUTPUT%\loader" || goto :error
copy /y "%PROJECT%source\picoLoader7.bin" "%OUTPUT%\picoLoader7.bin" >nul || goto :error
copy /y "%PROJECT%source\picoLoader9_DSPICO.bin" "%OUTPUT%\picoLoader9.bin" >nul || goto :error

rem Remove all build intermediates, so that only "_pico" is left.
echo Cleaning up...
docker run --rm -v "%PROJECT%:/work" -w /work/source pico-cheat-menu-build make clean >nul || goto :error

echo.
echo Done. Copy "_pico" to the root of the SD card, then put your own Pico Loader
echo (picoLoader7.bin and picoLoader9.bin, any fork with API version 3) in _pico\loader\
echo and the other Pico Loader files (aplist.bin, savelist.bin, patchlist.bin, usrcheat.dat) in _pico\.
pause
exit /b 0

:error
echo.
echo ERROR: the build failed, see the messages above.

:end_with_error
pause
exit /b 1
