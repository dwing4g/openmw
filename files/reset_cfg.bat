@echo off
setlocal enabledelayedexpansion
pushd %~dp0
chcp 65001
color 0f

set CURPATH=%cd%
cd ..\Data Files 2>nul
if errorlevel 1 (
	echo 出错: OpenMW的文件夹没有放到原版游戏文件夹内！
	goto end
)
if not exist Morrowind.bsa (
	echo 出错: OpenMW的文件夹没有放到原版游戏文件夹内！
	goto end
)
set CFGLINE=data="%cd%"
cd %CURPATH%

for /f "tokens=3" %%a in ('"reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders" /v Personal"') do set DOCPATH=%%a
set CFGPATH=%DOCPATH%\My Games\OpenMW

set TIP=0
set FILE=%CFGPATH%\launcher.cfg
if exist "%FILE%" (
	echo %FILE%
	set TIP=1
)
set FILE=%CFGPATH%\openmw.cfg
if exist "%FILE%" (
	echo %FILE%
	set TIP=1
)
set FILE=%CFGPATH%\settings.cfg
if exist "%FILE%" (
	echo %FILE%
	set TIP=1
)
if %TIP%==1 (
	echo.
	choice /c yn /m "以上配置文件即将被重置成初始状态, 按 y 继续, 按 n 取消"
	if errorlevel 2 goto cancel
)

md "%CFGPATH%" 2>nul
copy /y launcher_reset.cfg "%CFGPATH%\launcher.cfg"
copy /y openmw_reset.cfg   "%CFGPATH%\openmw.cfg"
copy /y settings_reset.cfg "%CFGPATH%\settings.cfg"
echo %CFGLINE% >> "%CFGPATH%\openmw.cfg"

rem if exist tes3cn_Morrowind.esp move /y tes3cn_Morrowind.esp "..\Data Files\"
rem if exist tes3cn_Tribunal.esp  move /y tes3cn_Tribunal.esp  "..\Data Files\"
rem if exist tes3cn_Bloodmoon.esp move /y tes3cn_Bloodmoon.esp "..\Data Files\"

echo.
if %TIP%==1 (
	echo 重置配置文件完成！可以关闭了
) else (
	echo 安装配置文件完成！可以关闭了
)
echo 然后运行 openmw 启动游戏, 或者 openmw-launcher 配置游戏
goto end

:cancel
echo.
echo 重置被取消, 可以关闭了

:end
echo.
pause
