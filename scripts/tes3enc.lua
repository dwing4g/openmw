-- luajit tes3enc.lua Morrowind.txt Morrowind.esm

local floor = math.floor
local string = string
local byte = string.byte
local char = string.char
local sub = string.sub
local table = table
local concat = table.concat
local io = io
local clock = os.clock
local arg = arg
local tonumber = tonumber
local error = error

local f = io.open(arg[2], "wb")

local goodGBK = { -- "霞鹜文楷 GB"字体支持的非GB2312字符
	["\x85\xde"] = true, -- 呣
	["\x86\xaa"] = true, -- 啰
	["\x8b\xa0"] = true, -- 嫚
	["\x9a\x47"] = true, -- 欸
	["\xb2\x74"] = true, -- 瞭
	["\xb5\x6f"] = true, -- 祇
	["\xd1\x59"] = true, -- 裏
	["\x87\x4e"] = true, -- 嘚
	["\xef\x76"] = true, -- 飗
}
local badGBK = {}

local function readString(s, i)
	local t = {}
	local b, n = i, #s
	local c, d, e
	local r = false
	while i <= n do
		c = byte(s, i)
		if c == 0x22 then -- "
			if i + 1 <= n and byte(s, i + 1) == 0x22 then
				if b < i then t[#t + 1] = sub(s, b, i - 1) end
				i = i + 1
				b = i
			else
				r = true
				if s:find("%S", i + 1) then return end
				break
			end
		elseif c == 0x24 then -- $
			if b < i then t[#t + 1] = sub(s, b, i - 1) end
			d = sub(s, i + 1, i + 2)
			if d:find "%x%x" then
				t[#t + 1] = char(tonumber(d, 16))
				i = i + 2
				b = i + 1
			else
				i = i + 1
				b = i
			end
		elseif badGBK then
			if not e then
				if c >= 0x81 and i + 1 <= n then
					e = true
					if c <= 0xa0 or c >= 0xf8 or byte(s, i + 1) <= 0xa0 then
						c = s:sub(i, i + 1)
						if not goodGBK[c] then
							badGBK[c] = true
						end
					end
				end
			elseif e then
				e = nil
			end
		end
		i = i + 1
	end
	if b < i then t[#t + 1] = sub(s, b, i - 1) end
	return r, concat(t)
end

local function readBinary(s)
	return s:gsub(" ", ""):gsub("%x%x", function(s) return char(tonumber(s, 16)) end)
end

local function writeInt4(v)
	f:write(char(v % 0x100, floor(v / 0x100) % 0x100, floor(v / 0x10000) % 0x100, floor(v / 0x1000000)))
end

local function writeInt2(v, tag)
	if v <= 0xffff then
		f:write(char(v % 0x100, floor(v / 0x100) % 0x100))
	else
		f:seek("cur", -4)
		f:write "XXXX\x04\0"
		writeInt4(v)
		f:write(tag, "\0\0")
	end
end

local t = clock()
local i = 1
local ss, q
local groups = {}
local ver
local tag, param
for line in io.lines(arg[1]) do
	if ss then
		local isEnd, s = readString(line, 1)
		ss[#ss + 1] = s
		if isEnd then
			s = concat(ss)
			ss = nil
			if ver == 3 then
				writeInt4(#s)
			else
				writeInt2(#s, tag)
			end
			f:write(s)
		else
			ss[#ss + 1] = "\r\n"
		end
	else
		line = line:gsub("^%x*:?%s*", "")
		tag, param = line:match "^[%w_<=>?:;@][%u%d_][%u%d_][%u%d_]%.([%w_<=>?:;@][%u%d_][%u%d_][%u%d_])%s*(.+)$"
		if not tag then param = line:match "^%s*(%[[%w ]*%])%s*$" end
		if param then
			if tag then
				tag = tag:gsub("%l", function(s) return string.char(s:byte(1) - 0x61) end)
				f:write(tag)
			end
			local c = param:sub(1, 1)
			if c == "\"" then
				local isEnd, s = readString(param, 2)
				if isEnd == true then
					if ver == 3 then
						writeInt4(#s)
					else
						writeInt2(#s, tag)
					end
					f:write(s)
				elseif isEnd == false then
					ss = { s, "\r\n" }
				else
					error("ERROR: invalid string at line " .. i)
				end
			elseif c == "[" then
				local s, e = param:match "^%[(.-)%](.*)$"
				if s then
					if e ~= "" then error("ERROR: invalid binary end at line " .. i) end
					s = readBinary(s)
					if s then
						if tag then
							if ver == 3 then
								writeInt4(#s)
							else
								writeInt2(#s, tag)
							end
						end
						f:write(s)
					else
						error("ERROR: invalid binary at line " .. i)
					end
				else
					error("ERROR: invalid binary bracket at line " .. i)
				end
			else
				error("ERROR: invalid param at line " .. i)
			end
		else
			local pre
			pre, tag, param = line:match "^([%-{])([%w_<=>?:;@][%u%d_?][%u%d_?][%u%d_?])%s*(.*)$"
			if tag then
				tag = tag:gsub("%l", function(s) return string.char(s:byte(1) - 0x61) end)
				if q then
					local p = f:seek()
					f:seek("set", q)
					writeInt4(p - q - ver * 4)
					f:seek("set", p)
					q = nil
				end
				if not ver then
					ver = tag == "TES3" and 3 or 4
				end
				f:write(tag)
				if pre == "{" then
					groups[#groups + 1] = f:seek()
				else
					q = f:seek()
				end
				if param ~= "" then
					local s, e = param:match "^%[(.-)%]%s*(.*)$"
					if s then
						if e ~= "" then error("ERROR: invalid class param end at line " .. i) end
						s = readBinary(s)
						if s then
							if tag == "TES4" and s:byte(1) ~= 1 then ver = 5 end
							if #s ~= ver * 4 - 4 then error("ERROR: invalid class param length at line " .. i) end
							writeInt4(#s)
							f:write(s)
						else
							error("ERROR: invalid class param binary at line " .. i)
						end
					else
						error("ERROR: invalid class param at line " .. i)
					end
				else
					f:write "\0\0\0\0\0\0\0\0\0\0\0\0"
				end
			elseif line:match "^}" and #groups > 0 then
				local p = f:seek()
				local b = groups[#groups]
				groups[#groups] = nil
				f:seek("set", b)
				writeInt4(p - b + 4)
				f:seek("set", p)
			else
				error("ERROR: invalid header at line " .. i)
			end
		end
	end
	i = i + 1
end
if ss then
	error("ERROR: invalid string end at line " .. i)
end
if q then
	local p = f:seek()
	f:seek("set", q)
	writeInt4(p - q - ver * 4)
end

f:close()
io.stderr:write("done! ", clock() - t, " seconds\n")

if #groups > 0 then
	error("ERROR: " .. #groups .. " group(s) not closed")
end

if badGBK and next(badGBK) and arg[1]:find "tes3cn" then
	io.stderr:write "chars not in GB2312: ["
	for c in pairs(badGBK) do
		io.stderr:write(c)
	end
	io.stderr:write "]\n"
end
