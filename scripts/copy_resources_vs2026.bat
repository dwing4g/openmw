@echo off
setlocal
pushd %~dp0

cd ..

xcopy /y /e files\data\*                     MSVC2026_64\Debug\resources\vfs\
xcopy /y /e files\data-mw\*                  MSVC2026_64\Debug\resources\vfs-mw\
xcopy /y /e files\lua_api\*                  MSVC2026_64\Debug\resources\lua_api\
xcopy /y /e files\shaders\*                  MSVC2026_64\Debug\resources\shaders\
xcopy /y    files\opencs\defaultfilters      MSVC2026_64\Debug\resources\
xcopy /y    files\launcher\images\openmw.png MSVC2026_64\Debug\resources\
xcopy /y    files\pinyin.txt                 MSVC2026_64\Debug\
rem xcopy /y files\MyGUIEngine_d.dll         MSVC2026_64\Debug\

xcopy /y /e files\data\*                     MSVC2026_64\RelWithDebInfo\resources\vfs\
xcopy /y /e files\data-mw\*                  MSVC2026_64\RelWithDebInfo\resources\vfs-mw\
xcopy /y /e files\lua_api\*                  MSVC2026_64\RelWithDebInfo\resources\lua_api\
xcopy /y /e files\shaders\*                  MSVC2026_64\RelWithDebInfo\resources\shaders\
xcopy /y    files\opencs\defaultfilters      MSVC2026_64\RelWithDebInfo\resources\
xcopy /y    files\launcher\images\openmw.png MSVC2026_64\RelWithDebInfo\resources\
xcopy /y    files\MyGUIEngine.dll            MSVC2026_64\RelWithDebInfo\
xcopy /y    files\pinyin.txt                 MSVC2026_64\RelWithDebInfo\

xcopy /y /e files\data\*                     MSVC2026_64\Release\resources\vfs\
xcopy /y /e files\data-mw\*                  MSVC2026_64\Release\resources\vfs-mw\
xcopy /y /e files\lua_api\*                  MSVC2026_64\Release\resources\lua_api\
xcopy /y /e files\shaders\*                  MSVC2026_64\Release\resources\shaders\
xcopy /y    files\opencs\defaultfilters      MSVC2026_64\Release\resources\
xcopy /y    files\launcher\images\openmw.png MSVC2026_64\Release\resources\
xcopy /y    files\MyGUIEngine.dll            MSVC2026_64\Release\
xcopy /y    files\pinyin.txt                 MSVC2026_64\Release\
xcopy /y    files\*_reset.cfg                MSVC2026_64\Release\
xcopy /y    files\reset_cfg.bat              MSVC2026_64\Release\
xcopy /y    readme-zh_CN.txt                 MSVC2026_64\Release\

xcopy /y    %SystemRoot%\System32\vcruntime140.dll         MSVC2026_64\Release\
xcopy /y    %SystemRoot%\System32\vcruntime140_1.dll       MSVC2026_64\Release\
xcopy /y    %SystemRoot%\System32\msvcp140.dll             MSVC2026_64\Release\
xcopy /y    %SystemRoot%\System32\msvcp140_1.dll           MSVC2026_64\Release\
xcopy /y    %SystemRoot%\System32\msvcp140_2.dll           MSVC2026_64\Release\
xcopy /y    %SystemRoot%\System32\msvcp140_atomic_wait.dll MSVC2026_64\Release\

del MSVC2026_64\Debug\resources\vfs\builtin.omwscripts.in
del MSVC2026_64\Debug\resources\vfs\CMakeLists.txt
del MSVC2026_64\Debug\resources\vfs-mw\CMakeLists.txt
del MSVC2026_64\Debug\resources\lua_api\CMakeLists.txt
del MSVC2026_64\Debug\resources\shaders\CMakeLists.txt

del MSVC2026_64\RelWithDebInfo\resources\vfs\builtin.omwscripts.in
del MSVC2026_64\RelWithDebInfo\resources\vfs\CMakeLists.txt
del MSVC2026_64\RelWithDebInfo\resources\vfs-mw\CMakeLists.txt
del MSVC2026_64\RelWithDebInfo\resources\lua_api\CMakeLists.txt
del MSVC2026_64\RelWithDebInfo\resources\shaders\CMakeLists.txt

del MSVC2026_64\Release\resources\vfs\builtin.omwscripts.in
del MSVC2026_64\Release\resources\vfs\CMakeLists.txt
del MSVC2026_64\Release\resources\vfs-mw\CMakeLists.txt
del MSVC2026_64\Release\resources\lua_api\CMakeLists.txt
del MSVC2026_64\Release\resources\shaders\CMakeLists.txt

pause

rem local f = io.popen('dir C:\\git\\openmw\\MSVC2026_64\\Release\\*.exe C:\\git\\openmw\\MSVC2026_64\\Release\\*.dll /a/b/o/s')
rem local s = f:read '*a'
rem f:close()
rem
rem for fn in s:gmatch '[^\r\n]+' do
rem 	print(fn)
rem 	f = io.open(fn, 'rb')
rem 	s = f:read '*a'
rem 	f:close()
rem 	for d in s:gmatch '[%w_-]+%.[dD][lL][lL]' do
rem 		print('\t' .. d)
rem 	end
rem end
