@echo off
setlocal
pushd %~dp0

luajit tes3dec.lua Morrowind.esm 1252 > Morrowind.txt
luajit tes3dec.lua tes3cn.esp    gbk  > tes3cn.txt
luajit tes3dec.lua 1.omwsave     utf8 > 1.txt

luajit tes3enc.lua Morrowind.txt Morrowind.esm.new
luajit tes3enc.lua tes3cn.txt    tes3cn.esp.new
luajit tes3enc.lua 1.txt         1.omwsave.new

fc /b Morrowind.esm Morrowind.esm.new
fc /b tes3cn.esp    tes3cn.esp.new
fc /b 1.omwsave     1.omwsave.new

pause
