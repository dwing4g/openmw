-- luajit tes3trim.lua tes3cn_Morrowind.txt > tes3cn_Morrowind.txt.new
-- move /y Morrowind.txt.new Morrowind.txt

local reserved = {
	ACTI = true,
	ALCH = true,
	APPA = true,
	ARMO = true,
	BODY = true,
	BOOK = true,
	BSGN = true,
	CLAS = true,
	CLOT = true,
	CONT = true,
	CREA = true,
	DIAL = true,
	DOOR = true,
	ENCH = true,
	FACT = true,
	GLOB = true,
	GMST = true,
	INFO = true,
	INGR = true,
	LEVC = true,
	LEVI = true,
	LIGH = true,
	LOCK = true,
	LTEX = true,
	MGEF = true,
	MISC = true,
	NPC_ = true,
	PROB = true,
	RACE = true,
	REGN = true,
	REPA = true,
	SCPT = true,
	SKIL = true,
	SNDG = true,
	SOUN = true,
	SPEL = true,
	SSCR = true,
	STAT = true,
	TES3 = true,
	WEAP = true,
}

local w, e, i
local ln = 0
for line in io.lines(arg[1]) do
	ln = ln + 1
	if e then
		i = 1
	else
		local tag = line:match "^[%- ]([%u%d_<=>?:;@%z\x01-\x14][%u%d_]+)"
		if tag then
			w = reserved[tag]
			i = line:match "^[%- ]%S+%s+()"
			if i then
				e = line:sub(i, i)
				if e == "[" then e = "]" end
				i = i + 1
			else
				if w then
					print(line)
				end
			end
		else
			error("ERROR: unknown tag at line " .. ln)
		end
	end
	if e then
		while true do
			local p = line:find(e, i, true)
			if p then
				p = p + 1
				if line:sub(p, p) == e then
					i = p + 1
				else
					e = nil
					break
				end
			else
				break
			end
		end
		if w then
			print(line)
		end
	end
end
