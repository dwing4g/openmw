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

local function readString(s, i)
	local t = {}
	local b, n = i, #s
	local c, d
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

local t = clock()
local i = 1
local ss, q
for line in io.lines(arg[1]) do
	if ss then
		local isEnd, s = readString(line, 1)
		ss[#ss + 1] = s
		if isEnd then
			s = concat(ss)
			ss = nil
			writeInt4(#s)
			f:write(s)
		else
			ss[#ss + 1] = "\r\n"
		end
	elseif line:find "%x%x%x%x%x%x%x%x: " then
		line = sub(line, 11)
		local tag, param = line:match "^%s*([%u%d_][%u%d_][%u%d_][%u%d_])%s+(.+)$"
		if tag then
			f:write(tag)
			local p = param:find "^%s*(\")"
			if p then
				local isEnd, s = readString(param, p + 1)
				if isEnd == true then
					writeInt4(#s)
					f:write(s)
				elseif isEnd == false then
					ss = { s, "\r\n" }
				else
					error("ERROR: invalid string at line " .. i)
				end
			else
				local s, e = param:match "^%s*%[(.-)%](.*)$"
				if s then
					if #e > 0 then
						if q then
							error("ERROR: invalid open at line " .. i)
						end
						q = f:seek()
					end
					s = readBinary(s)
					if s then
						if #e > 0 and #s ~= 8 then
							error("ERROR: invalid class at line " .. i)
						end
						writeInt4(#s)
						f:write(s)
					else
						error("ERROR: invalid binary at line " .. i)
					end
				elseif param:find "^%s*{%s*$" then
					q = f:seek()
					f:write "\0\0\0\0\0\0\0\0\0\0\0\0"
				else
					error("ERROR: invalid param at line " .. i)
				end
			end
		elseif line:match "^%s*}%s*$" then
			if q then
				local p = f:seek()
				f:seek("set", q)
				writeInt4(p - q - 12)
				f:seek("set", p)
				q = nil
			else
				error("ERROR: invalid close at line " .. i)
			end
		else
			error("ERROR: invalid tag at line " .. i)
		end
	else
		error("ERROR: invalid header at line " .. i)
	end
	i = i + 1
end

f:close()
io.stderr:write("done! ", clock() - t, " seconds\n")
