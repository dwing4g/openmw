@echo off
setlocal
pushd %~dp0

@echo on

luajit tes3dec.lua Morrowind.esm        1252 > Morrowind.txt
luajit tes3dec.lua Tribunal.esm         1252 > Tribunal.txt
luajit tes3dec.lua Bloodmoon.esm        1252 > Bloodmoon.txt
luajit tes3dec.lua tes3cn_Morrowind.esp gbk  > tes3cn_Morrowind.txt
luajit tes3dec.lua tes3cn_Tribunal.esp  gbk  > tes3cn_Tribunal.txt
luajit tes3dec.lua tes3cn_Bloodmoon.esp gbk  > tes3cn_Bloodmoon.txt
luajit tes3dec.lua tes3cn.esp           gbk  > tes3cn.txt
luajit tes3dec.lua 1.omwsave            utf8 > 1.txt

luajit tes3enc.lua Morrowind.txt        Morrowind.esm.new
luajit tes3enc.lua Tribunal.txt         Tribunal.esm.new
luajit tes3enc.lua Bloodmoon.txt        Bloodmoon.esm.new
luajit tes3enc.lua tes3cn_Morrowind.txt tes3cn_Morrowind.esp.new
luajit tes3enc.lua tes3cn_Tribunal.txt  tes3cn_Tribunal.esp.new
luajit tes3enc.lua tes3cn_Bloodmoon.txt tes3cn_Bloodmoon.esp.new
luajit tes3enc.lua tes3cn.txt           tes3cn.esp.new
luajit tes3enc.lua 1.txt                1.omwsave.new

@echo off

fc /b Morrowind.esm        Morrowind.esm.new
fc /b Tribunal.esm         Tribunal.esm.new
fc /b Bloodmoon.esm        Bloodmoon.esm.new
fc /b tes3cn_Morrowind.esp tes3cn_Morrowind.esp.new
fc /b tes3cn_Tribunal.esp  tes3cn_Tribunal.esp.new
fc /b tes3cn_Bloodmoon.esp tes3cn_Bloodmoon.esp.new
fc /b tes3cn.esp           tes3cn.esp.new
fc /b 1.omwsave            1.omwsave.new

rem luajit count_text.lua
rem luajit check_topic.lua topics.txt > errors.txt
rem luajit tes3enc.lua tes3cn_Morrowind_fix.txt tes3cn_Morrowind_fix.esp
rem luajit tes3enc.lua tes3cn_Tribunal_fix.txt  tes3cn_Tribunal_fix.esp
rem luajit tes3enc.lua tes3cn_Bloodmoon_fix.txt tes3cn_Bloodmoon_fix.esp

pause
