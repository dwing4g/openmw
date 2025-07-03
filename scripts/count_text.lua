-- luajit count_text.lua

local string = string
local byte = string.byte
local char = string.char
local sub = string.sub
local concat = table.concat
local io = io
local write = io.write
local tonumber = tonumber
local ipairs = ipairs
local error = error

local filenames = {
	"Morrowind.txt",
	"Bloodmoon.txt",
	"Tribunal.txt",
}

local tags = {
	["ACTI.FNAM"] = true,
	["ALCH.FNAM"] = true,
	["APPA.FNAM"] = true,
	["ARMO.FNAM"] = true,
	["BOOK.FNAM"] = true,
	["BSGN.FNAM"] = true,
	["CELL.NAME"] = true,
	["CLAS.FNAM"] = true,
	["CLOT.NAME"] = true,
	["CONT.FNAM"] = true,
	["CREA.FNAM"] = true,
	["DIAL.NAME"] = true,
	["DOOR.FNAM"] = true,
--	["ENCH.????"] = true,
	["FACT.FNAM"] = true,
	["GMST.STRV"] = true,
	["INFO.NAME"] = true,
	["INGR.FNAM"] = true,
	["LIGH.FNAM"] = true,
	["LOCK.FNAM"] = true,
	["MGEF.DESC"] = true,
	["MISC.FNAM"] = true,
	["NPC_.FNAM"] = true,
	["PROB.FNAM"] = true,
	["RACE.FNAM"] = true,
	["REGN.FNAM"] = true,
	["REPA.FNAM"] = true,
--	["SCPT.SCTX"] = true,
	["SKIL.DESC"] = true,
	["SPEL.FNAM"] = true,
	["WEAP.FNAM"] = true,
}

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

local tn, tns = 0, 0
for _, filename in ipairs(filenames) do
	write("loading ", filename, " ... ")
	local i, n, ns, ss = 1, 0, 0
	for line in io.lines(filename) do
		if ss then
			local isEnd, s = readString(line, 1)
			ss[#ss + 1] = s
			if isEnd then
				s = concat(ss)
				n = n + 1
				ns = ns + #s
				ss = nil
			else
				ss[#ss + 1] = "\r\n"
			end
		else
			local tag, s = line:match "^%x+:%s*([%u%d_][%u%d_][%u%d_][%u%d_]%.[%u%d_][%u%d_][%u%d_][%u%d_])%s*\"(.*)$"
			if tags[tag] then
				local isEnd, s = readString(s, 1)
				if isEnd == true then
					n = n + 1
					ns = ns + #s
				elseif isEnd == false then
					ss = { s, "\r\n" }
				else
					error("ERROR: invalid string at line " .. i)
				end
			end
		end
		i = i + 1
	end
	if ss then
		error("ERROR: invalid string end at line " .. i)
	end
	write("[", n, "] ", ns, " bytes\n")
	tn = tn + n
	tns = tns + ns
end
write("TOTAL: [", tn, "] ", tns, " bytes\n")
