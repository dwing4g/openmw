@echo off
setlocal
pushd %~dp0

cd ..

xcopy /y /e files\data\*                     MSVC2022_64\Debug\resources\vfs\
xcopy /y /e files\lua_api\*                  MSVC2022_64\Debug\resources\lua_api\
xcopy /y /e files\shaders\*                  MSVC2022_64\Debug\resources\shaders\
xcopy /y    files\opencs\defaultfilters      MSVC2022_64\Debug\resources\
xcopy /y    files\launcher\images\openmw.png MSVC2022_64\Debug\resources\
rem xcopy /y files\MyGUIEngine_d.dll         MSVC2022_64\Debug\
xcopy /y    readme-zh_CN.txt                 MSVC2022_64\Debug\

xcopy /y /e files\data\*                     MSVC2022_64\RelWithDebInfo\resources\vfs\
xcopy /y /e files\lua_api\*                  MSVC2022_64\RelWithDebInfo\resources\lua_api\
xcopy /y /e files\shaders\*                  MSVC2022_64\RelWithDebInfo\resources\shaders\
xcopy /y    files\opencs\defaultfilters      MSVC2022_64\RelWithDebInfo\resources\
xcopy /y    files\launcher\images\openmw.png MSVC2022_64\RelWithDebInfo\resources\
xcopy /y    files\MyGUIEngine.dll            MSVC2022_64\RelWithDebInfo\
xcopy /y    readme-zh_CN.txt                 MSVC2022_64\RelWithDebInfo\

xcopy /y /e files\data\*                     MSVC2022_64\Release\resources\vfs\
xcopy /y /e files\lua_api\*                  MSVC2022_64\Release\resources\lua_api\
xcopy /y /e files\shaders\*                  MSVC2022_64\Release\resources\shaders\
xcopy /y    files\opencs\defaultfilters      MSVC2022_64\Release\resources\
xcopy /y    files\launcher\images\openmw.png MSVC2022_64\Release\resources\
xcopy /y    files\MyGUIEngine.dll            MSVC2022_64\Release\
xcopy /y    readme-zh_CN.txt                 MSVC2022_64\Release\

del MSVC2022_64\Debug\resources\vfs\CMakeLists.txt
del MSVC2022_64\Debug\resources\lua_api\CMakeLists.txt
del MSVC2022_64\Debug\resources\shaders\CMakeLists.txt

del MSVC2022_64\RelWithDebInfo\resources\vfs\CMakeLists.txt
del MSVC2022_64\RelWithDebInfo\resources\lua_api\CMakeLists.txt
del MSVC2022_64\RelWithDebInfo\resources\shaders\CMakeLists.txt

del MSVC2022_64\Release\resources\vfs\CMakeLists.txt
del MSVC2022_64\Release\resources\lua_api\CMakeLists.txt
del MSVC2022_64\Release\resources\shaders\CMakeLists.txt

pause
