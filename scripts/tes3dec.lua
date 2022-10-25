-- luajit tes3dec.lua Morrowind.esm > Morrowind.txt

local string = string
local byte = string.byte
local char = string.char
local format = string.format
local sub = string.sub
local table = table
local concat = table.concat
local io = io
local write = io.write
local clock = os.clock
local ipairs = ipairs
local arg = arg
local error = error

local ENCODING = arg[2] or "1252" -- "1252", "gbk", "jis", "utf8"
local RAW = arg[3] == "raw"

local isStr, addEscape
if ENCODING == "1252" then --TODO: 0x92 0xA0 0xE0 0xE1 0xE9 0xF3 used in TR_Mainland.esm
	isStr = function(s)
		local n = #s
		while n > 0 and byte(s, n) == 0 do n = n - 1 end
		if n == 0 and #s > 0 then return end
		for i = 1, n do
			local c = byte(s, i)
			if c >= 0x7f and c ~= 0x93 and c ~= 0x94 and c ~= 0xad and c ~= 0xef and c ~= 0xfa then return end -- “”­ïú used in official english Morrowind.esm
			if c < 0x20 and c ~= 9 and c ~= 10 and c ~= 13 then	return end
		end
		return true
	end
	addEscape = function(s)
		local t = {}
		local b, i, n = 1, 1, #s
		local c, e
		while i <= n do
			c, e = byte(s, i), 0
			if c <= 0x7e then
				if c >= 0x20 or c == 9 then e = 1 -- \t
				elseif c == 13 and i < n and byte(s, i + 1) == 10 then e = 2 -- \r\n
				end
			elseif c == 0x93 or c == 0x94 or c == 0xad or c == 0xef or c == 0xfa then e = 1 -- “”­ïú used in official english Morrowind.esm
			end
			if e == 0 then
				if b < i then t[#t + 1] = sub(s, b, i - 1) end
				i = i + 1
				b = i
				t[#t + 1] = format("$%02X", c)
			else
				if e == 1 and (c == 0x22 or c == 0x24) then -- ",$ => "",$$
					if b < i then t[#t + 1] = sub(s, b, i - 1) end
					b = i
					t[#t + 1] = char(c)
				end
				i = i + e
			end
		end
		if b < i then t[#t + 1] = sub(s, b, i - 1) end
		return concat(t):gsub("\r", "") -- windows console will add \r
	end
elseif ENCODING == "gbk" then
	isStr = function(s)
		local n = #s
		if n == 4 and byte(s, 4) == 0 and byte(s, 1) > 0x7f then return end
		while n > 0 and byte(s, n) == 0 do n = n - 1 end
		if n == 0 and #s > 0 then return end
		local b = 0
		for i = 1, n do
			local c = byte(s, i)
			if b == 1 then
				if c < 0x40 or c > 0xfe or c == 0x7f then return end
				b = 0
			elseif c >= 0x7f then
				if c < 0x81 or c > 0xfe or c == 0x7f then return end
				b = 1
			elseif c < 0x20 and c ~= 9 and c ~= 10 and c ~= 13 then	return
			end
		end
		return b == 0
	end
	addEscape = function(s)
		local t = {}
		local b, i, n = 1, 1, #s
		local c, d, e
		while i <= n do
			c, e = byte(s, i), 0
			if c <= 0x7e then
				if c >= 0x20 or c == 9 then e = 1 -- \t
				elseif c == 13 and i < n and byte(s, i + 1) == 10 then e = 2 -- \r\n
				end
			elseif i < n and c >= 0x81 and c <= 0xfe and c ~= 0x7f then
				local d = byte(s, i + 1)
				if d >= 0x40 and d <= 0xfe and d ~= 0x7f then e = 2 end
			end
			if e == 0 then
				if b < i then t[#t + 1] = sub(s, b, i - 1) end
				i = i + 1
				b = i
				t[#t + 1] = format("$%02X", c)
			else
				if e == 1 and (c == 0x22 or c == 0x24) then -- ",$ => "",$$
					if b < i then t[#t + 1] = sub(s, b, i - 1) end
					b = i
					t[#t + 1] = char(c)
				end
				i = i + e
			end
		end
		if b < i then t[#t + 1] = sub(s, b, i - 1) end
		return concat(t):gsub("\r", "") -- windows console will add \r
	end
elseif ENCODING == "jis" then
	isStr = function(s)
		local n = #s
		if n == 4 and byte(s, 4) == 0 and byte(s, 1) > 0x7f then return end
		while n > 0 and byte(s, n) == 0 do n = n - 1 end
		if n == 0 and #s > 0 then return end
		local b = 0
		for i = 1, n do
			local c = byte(s, i)
			if b == 1 then
				if c < 0x40 or c > 0xfc or c == 0x7f then return end
				b = 0
			elseif c >= 0x7f then
				if c < 0xa1 or c > 0xdf then -- 1-byte japanese char
					if c < 0x81 or c > 0xfc or c == 0x7f then return end
					b = 1
				end
			elseif c < 0x20 and c ~= 9 and c ~= 10 and c ~= 13 then	return
			end
		end
		return b == 0
	end
	addEscape = function(s)
		local t = {}
		local b, i, n = 1, 1, #s
		local c, d, e
		while i <= n do
			c, e = byte(s, i), 0
			if c <= 0x7e then
				if c >= 0x20 or c == 9 then e = 1 -- \t
				elseif c == 13 and i < n and byte(s, i + 1) == 10 then e = 2 -- \r\n
				end
			elseif i < n and c >= 0x81 and c <= 0xfc and c ~= 0x7f then
				if c >= 0xa1 and c <= 0xdf then -- 1-byte japanese char
					e = 1
				else
					local d = byte(s, i + 1)
					if d >= 0x40 and d <= 0xfc and d ~= 0x7f then e = 2 end
				end
			end
			if e == 0 then
				if b < i then t[#t + 1] = sub(s, b, i - 1) end
				i = i + 1
				b = i
				t[#t + 1] = format("$%02X", c)
			else
				if e == 1 and (c == 0x22 or c == 0x24) then -- ",$ => "",$$
					if b < i then t[#t + 1] = sub(s, b, i - 1) end
					b = i
					t[#t + 1] = char(c)
				end
				i = i + e
			end
		end
		if b < i then t[#t + 1] = sub(s, b, i - 1) end
		return concat(t):gsub("\r", "") -- windows console will add \r
	end
else -- "utf8"
	isStr = function(s)
		local n = #s
		while n > 0 and byte(s, n) == 0 do n = n - 1 end
		if n == 0 and #s > 0 then return end
		local b = 0
		for i = 1, n do
			local c = byte(s, i)
			if b > 0 then
				if c < 0x80 or c >= 0xc0 then return end
				b = b - 1
			elseif c < 0x7f then
				if c < 0x20 and c ~= 9 and c ~= 10 and c ~= 13 then	return
			elseif c >= 0xf8 then return end
			elseif c >= 0xf0 then if i + 3 <= n and (c > 0xf0 or byte(s, i + 1) >= 0x90) then b = 3 else return end
			elseif c >= 0xe0 then if i + 2 <= n and (c > 0xe0 or byte(s, i + 1) >= 0xa0) then b = 2 else return end
			elseif c >= 0xc0 then if i + 1 <= n and c >= 0xc4 then b = 1 else return end
			else return
			end
		end
		return b == 0
	end
	addEscape = function(s)
		local t = {}
		local b, i, n = 1, 1, #s
		local c, d, e
		while i <= n do
			c, e = byte(s, i), 0
			if c <= 0x7e then
				if c >= 0x20 or c == 9 then e = 1 -- \t
				elseif c == 13 and i < n and byte(s, i + 1) == 10 then e = 2 -- \r\n
				end
			elseif c >= 0xc0 and c < 0xf8 then
				if c >= 0xf0 and i + 3 <= n then
					local x, y, z = byte(s, i + 1, i + 3)
					if x >= 0x80 and x < 0xc0 and y >= 0x80 and y < 0xc0 and z >= 0x80 and z < 0xc0 and (c > 0xf0 or x >= 0x90) then e = 4 end
				elseif c >= 0xe0 and i + 2 <= n then
					local x, y = byte(s, i + 1, i + 2)
					if x >= 0x80 and x < 0xc0 and y >= 0x80 and y < 0xc0 and (c > 0xe0 or x >= 0xa0) then e = 3 end
				elseif c >= 0xc0 and i + 1 <= n then
					local x = byte(s, i + 1)
					if x >= 0x80 and x < 0xc0 and c >= 0xc4 then e = 2 end
				end
			end
			if e == 0 then
				if b < i then t[#t + 1] = sub(s, b, i - 1) end
				i = i + 1
				b = i
				t[#t + 1] = format("$%02X", c)
			else
				if e == 1 and (c == 0x22 or c == 0x24) then -- ",$ => "",$$
					if b < i then t[#t + 1] = sub(s, b, i - 1) end
					b = i
					t[#t + 1] = char(c)
				end
				i = i + e
			end
		end
		if b < i then t[#t + 1] = sub(s, b, i - 1) end
		return concat(t):gsub("\r", "") -- windows console will add \r
	end
end

local f = io.open(arg[1], "rb")

local function readInt4(limit)
	local a, b, c, d = byte(f:read(4), 1, 4)
	local v = a + b * 0x100 + c * 0x10000 + d * 0x100000000
	if limit and v > limit then error(format("ERROR: too large value: %d > %d", v, limit)) end
	return v
end

local stringTags = {
	"NAME", "SCHD", "SCTX", "TEXT",
}
local binaryTags = {
	"ACID", "BYDT", "CAST", "COUN", "DATA", "DISP", "DODT", "EFID", "FLAG", "FLTV", "FRMR",
	"ICNT", "INDX", "INTV", "MGEF", "NAM0", "NAM9", "NPDT",
	"RGNC", "SPAW", "STAR", "STBA", "WNAM", "XSCL",
}
for _, v in ipairs(stringTags) do stringTags[v] = true end
for _, v in ipairs(binaryTags) do binaryTags[v] = true end
--[[ classes for translation
class *.esm         tes3cn.esp Morrowind_cn.esp
TES3: 1                        -> 1
ACTI: 697+346+202              -> 697   NAME -> FNAM       地点名
ALCH: 258+2+6                  -> 258   NAME -> FNAM       药水名
APPA: 22+5+0                   -> 22    NAME -> FNAM       炼金器材名
ARMO: 280+79+96                -> 280   NAME -> FNAM       重甲盾牌名
BOOK: 574+44+49       -> 574   -> 574   NAME -> FNAM,TEXT  书籍
BSGN: 13                       -> 13    NAME -> FNAM       星座名
CELL: 2538+276+121             -> 2538  NAME -> NAME       地区名
CLAS: 77+1+5          -> 77    -> 77    NAME -> FNAM,DESC  职业
CLOT: 510+42+31                -> 510   NAME -> FNAM       轻甲饰品名
CONT: 890+133+104              -> 890   NAME -> FNAM       场景物品名
CREA: 260+75+97                -> 260   NAME -> FNAM       战斗NPC名
DIAL: 2358+860+893    -> 4053  -> 2354  NAME -> NAME(部分) 关键词
DOOR: 140+95+87                -> 139   NAME -> FNAM       传送门名
ENCH: 708+42+46                -> 708   NAME -> ????       ????
FACT: 22+2+3                   -> 22    NAME -> FNAM       家族名
GMST: 1449+102+101    -> 1220  -> 1521  NAME -> STRV       全局字符串,界面文字(PNAM,NNAM->NAME)
INFO: 23693+6504+6757 -> 23690 -> 23692 INAM -> NAME       普通对话
INGR: 95+26+12                 -> 95    NAME -> FNAM       炼金材料名
LIGH: 574+74+44                -> 574   NAME -> FNAM       灯源名
LOCK: 6                        -> 6     NAME -> FNAM       开锁器名
MGEF: 137+4+1         -> 137   -> 137   INDX -> DESC       魔法效果描述
MISC: 536+76+55                -> 536   NAME -> FNAM       杂项物品名
NPC_: 2675+215+159             -> 2675  NAME -> FNAM       NPC名
PROB: 6                        -> 6     NAME -> FNAM       侦测器名
RACE: 10              -> 10    -> 10    NAME -> FNAM,DESC  种族
REGN: 9+6+1                    -> 9     NAME -> FNAM       区域名
REPA: 6+4+0                    -> 6     NAME -> FNAM       修理锤名
SCPT: 632+336+263     -> 182   -> 632   SCHD -> SCTX       脚本
SKIL: 27              -> 27    -> 27    INDX -> DESC       技能描述
SPEL: 990+33+45       -> 990   -> 990   NAME -> FNAM       魔法名
WEAP: 485+110+74               -> 485   NAME -> FNAM       武器名
TOTAL:                            40744
--]]

local function readFields(class, posEnd)
	while true do
		local pos = f:seek()
		if pos >= posEnd then
			if pos == posEnd then return end
			error(format("ERROR: read overflow 0x%8X > 0x%8X", pos, posEnd))
		end
		local tag = f:read(4)
		if not tag:find "^[%u%d_]+$" then error(format("ERROR: %08X: unknown tag: %q", pos, tag)) end
		write(RAW and format(" %s.%s ", class, tag) or format("%08X: %s.%s ", pos, class, tag))
		local n = readInt4(0x100000)
		local s = f:read(n)
		if not binaryTags[tag] and (stringTags[tag] or isStr(s)) then
			write("\"", addEscape(s), "\"\n") -- :gsub("%z$", "")
		else
			for i = 1, n do
				write(format(i == 1 and "[%02X" or " %02X", byte(s, i)))
			end
			write "]\n"
		end
	end
end

local count = {}
local function readClasses(posEnd)
	while true do
		local pos = f:seek()
		if pos >= posEnd then
			if pos == posEnd then return end
			error(format("ERROR: read overflow 0x%8X > 0x%8X", pos, posEnd))
		end
		local tag = f:read(4)
		if not tag:find "^[%u%d_]+$" then error(format("ERROR: %08X: unknown tag: %q", pos, tag)) end
		count[tag] = (count[tag] or 0) + 1
		write(RAW and format("-%s", tag) or format("%08X:-%s", pos, tag))
		local n = readInt4(0x1000000)
		local b = f:read(8)
		if b ~= "\0\0\0\0\0\0\0\0" then
			for j = 1, 8 do
				write(format(j == 1 and " [%02X" or " %02X", byte(b, j)))
			end
			write "]"
		end
		write "\n"
		readFields(tag, f:seek() + n)
	end
end

local posEnd = f:seek "end"
f:seek "set"
local t = clock()
readClasses(posEnd)
f:close()
io.stderr:write("done! ", clock() - t, " seconds\n")

local n = 0
local index = {}
for k in pairs(count) do
	index[#index + 1] = k
end
table.sort(index)
for _, k in ipairs(index) do
	n = n + count[k]
	io.stderr:write(k, ": ", count[k], "\n")
end
io.stderr:write("TOTAL: ", n, "\n")
