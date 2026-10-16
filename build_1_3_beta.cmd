@echo off
setlocal
if "%STEAMWORKS_SDK%"=="" (
  echo Set STEAMWORKS_SDK to the folder containing the SDK's sdk directory.
  exit /b 1
)
if not exist "%STEAMWORKS_SDK%\sdk\public\steam\isteamremoteplay.h" (
  echo Steamworks SDK headers not found. Check STEAMWORKS_SDK.
  exit /b 1
)
if not exist build mkdir build
cl /nologo /EHsc /std:c++17 /W4 /MT /LD /I"%STEAMWORKS_SDK%\sdk\public" /Fe:build\HalfSwordBridge13Beta.dll src\bridge_v1_3_beta.cpp user32.lib
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /MT /DUNICODE /D_UNICODE /Fe:build\HalfSwordJoinerControls13Beta.exe src\bridge_controls_v1_3_beta.cpp src\bridge_loader_v1_3_beta.cpp user32.lib comctl32.lib
if errorlevel 1 exit /b 1
echo 1.3 Lock On Unstable Beta build completed.
