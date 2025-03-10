-- luajit check_topic.lua topics.txt [Morrowind.txt] [tes3cn_Morrowind.txt] [tes3cn_Morrowind.fix.txt] > errors.txt

local string = string
local byte = string.byte
local char = string.char
local format = string.format
local sub = string.sub
local table = table
local concat = table.concat
local io = io
local write = io.write
local tonumber = tonumber
local ipairs = ipairs
local pairs = pairs
local error = error
local arg = arg

local function errwrite(...)
	io.stderr:write(...)
end

local newLine = true
local function warn(...)
	if not newLine then
		newLine = true
		errwrite "\n"
	end
	errwrite("WARN: ", ...)
	errwrite "\n"
end

local src_filenames = arg[2] and { arg[2] } or {
	"Morrowind.txt",
	"Tribunal.txt",
	"Bloodmoon.txt",
}

local dst_filenames = arg[3] and arg[4] and {{ arg[3], arg[4] }} or {
	{ "tes3cn_Morrowind.txt", "tes3cn_Morrowind.fix.txt" },
	{ "tes3cn_Tribunal.txt",  "tes3cn_Tribunal.fix.txt" },
	{ "tes3cn_Bloodmoon.txt", "tes3cn_Bloodmoon.fix.txt" },
}

local topics_filename = arg[1] or "topics.txt"

local delete_tags = {
	CELL = true,
	LAND = true,
	PGRD = true,
}

local function canDeleteTag(tag)
	return delete_tags[sub(tag, 1, 4)]
end

-- 044CAAC2: DIAL.NAME "...$00"
-- 044CAAEF: DIAL.DATA [00]
-- 0441C5AD: INFO.INAM  "...$00"
-- 0441C602: INFO.NAME "..."
-- AddTopic ""...""
-- addtopic ""...""

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

local function inTableValue(t, v)
	for _, tv in ipairs(t) do
		if tv == v then return true end
	end
end

local function loadTopics(filename, topicMap, topicMapR, dials) -- topic => INAM, INAM => { topics }
	errwrite("loading ", filename, " ... ")
	newLine = false
	local localTopicMap = {}
	local n, topic = 0
	for line in io.lines(filename) do
		local s = line:match "^%x*:?%s*DIAL%.NAME%s*\"(.-)\""
		if s then
			topic = s:gsub("%$00$", "")
		elseif topic then
			s = line:match "^%x*:?%s*DIAL%.DATA %[(.-)%]"
			if s then
				if s == "00" then -- 00:Topic 01:Voice 02:Greeting 03:Persuasion 04:Journal
					topicMap[topic:lower()] = true
				else
					dials[topic] = true
					topic = nil
				end
			else
				s = line:match "^%x*:?%s*INFO%.INAM%s*\"(.*)\"$"
				if s then
					s = s:gsub("%$00$", "")
					topic = topic:lower()
					if localTopicMap[topic] then
						warn("duplicated topic: ", topic, " => ", s)
					end
					localTopicMap[topic] = s
					topicMap[topic] = s
					local topicR = topicMapR[s]
					if topicR then
						if not inTableValue(topicR, topic) then
							topicR[#topicR + 1] = topic
						end
					else
						topicMapR[s] = { topic }
					end
					n = n + 1
					topic = nil
				end
			end
		end
	end
	errwrite(n, " topics\n")
	newLine = true
end

local topicMap, topicMapR, dials = {}, {}, {}
for _, filename in ipairs(src_filenames) do
	local f = io.open(filename, "rb")
	if f then f:close() else filename = "../" .. filename end
	loadTopics(filename, topicMap, topicMapR, dials)
end

local checkTopicMap, checkTopicMapR, checkDials = {}, {}, {}
for _, filename in ipairs(dst_filenames) do
	loadTopics(filename[1], checkTopicMap, checkTopicMapR, checkDials)
end

for dial in pairs(checkDials) do
	if not dials[dial] then
		warn("unmatched DIAL.NAME: ", dial)
	end
end

local f = io.open(topics_filename, "rb")
if not f then
	errwrite("creating ", topics_filename, " ... ")
	newLine = false
	f = io.open(topics_filename, "wb")
	if not f then error "ERROR: can not create topic file" end
	local n = 0
	local keys = {}
	for topic in pairs(topicMap) do
		keys[#keys + 1] = topic
	end
	table.sort(keys)
	for _, topic in ipairs(keys) do
		f:write("[", topic, "] =>")
		local inam = topicMap[topic]
		local checkTopics = checkTopicMapR[inam]
		local written = false
		if checkTopics then
			for _, checkTopic in ipairs(checkTopics) do
				f:write(" [", checkTopic, "]")
				written = true
			end
		end
		if (not checkTopics or #checkTopics ~= 1) and (not arg[2] or arg[2] ~= arg[3]) then
			f:write " !!!"
		elseif not written then
			f:write(" [", topic, "]")
		end
		f:write("\r\n")
		n = n + 1
	end
	f:close()
	errwrite(n, " topics\n")
	newLine = true
	errwrite "========== DUMP TOPICS DONE ==========\n"
	return
end
f:close()
local topicPairs = {}
local i = 1
errwrite("loading ", topics_filename, " ... ")
newLine = false
local err = 0
local check0, check1 = {}, {}
for line in io.lines(topics_filename) do
	local topic, checkTopic, more = line:match "^%s*%[(.-)%]%s*=>%s*%[(.-)%](.*)$"
	if not topic or more:find "%S" then
		if err == 0 then errwrite "\n" newLine = true end
		errwrite("ERROR: invalid topic file at line ", i, "\n")
		err = err + 1
	else
		if check0[topic] then
			if err == 0 then errwrite "\n" newLine = true end
			errwrite("ERROR: duplicated topic [", topic, "] at line ", i, "\n")
			err = err + 1
		end
		if check1[checkTopic] then
			if err == 0 then errwrite "\n" newLine = true end
			errwrite("ERROR: duplicated checkTopic [", checkTopic, "] at line ", i, "\n")
			err = err + 1
		end
		check0[topic] = checkTopic
		check1[checkTopic] = topic
		if not topicMap[topic] then
			-- warn("unused topic [", topic, "] at line ", i, "\n")
		end
		if not checkTopicMap[checkTopic] then
			local checkTopics = checkTopicMapR[topicMap[topic]]
			local t = {}
			if checkTopics then
				t[#t + 1] = ", maybe:"
				for _, topic in ipairs(checkTopics) do
					t[#t + 1] = " ["
					t[#t + 1] = topic
					t[#t + 1] = "]"
				end
			end
			-- warn("unused check topic [", checkTopic, "] at line ", i, concat(t), "\n")
		end
		topicPairs[topic] = checkTopic
		i = i + 1
	end
end
errwrite(i - 1, " topics\n")
newLine = true
for topic in pairs(topicMap) do
	if not check0[topic] then
		errwrite("ERROR: undefined topic [", topic, "]\n")
		err = err + 1
	end
end
for checkTopic, inam in pairs(checkTopicMap) do
	if not check1[checkTopic] then
		errwrite("ERROR: undefined check topic [", checkTopic, "], ref [" .. (topicMapR[inam] or {})[1] .. "] => [" .. inam .. "]\n")
		err = err + 1
	end
end
--[[
for t0, t1 in pairs(check0) do
	for s0, s1 in pairs(check0) do
		if t0 ~= s0 then
			local tp = t0:find(s0, 1, true)
			if tp and (tp == 1 or sub(t0, tp - 1, tp - 1):find "%W") and not t1:find(s1, 1, true) then
				warn("topic contain unmatched: [", t0, "] => [", t1, "], [", s0, "] => [", s1, "]")
			end
		end
	end
end
--]]
if err ~= 0 then
	error("ERROR: " .. err .. " errors")
end

local function createTopicTree(topicMap, topicTree)
	for topic in pairs(topicMap) do
		local curNode = topicTree
		for i = 1, #topic do
			local c = sub(topic, i, i)
			local nextNode = curNode[c]
			if not nextNode then
				nextNode = {}
				curNode[c] = nextNode
			end
			curNode = nextNode
		end
		curNode[0] = topic
	end
	topicTree[0] = nil -- ensure no empty topic
end

local topicTree = {}
createTopicTree(topicMap, topicTree)
local checkTopicTree = {}
createTopicTree(checkTopicMap, checkTopicTree)

local function dumpTopicTree(topicMap, node, indent)
	for k, v in pairs(node) do
		if k ~= 0 then
			write(indent, k)
			if v[0] then
				write(" = ", v[0], " => ", topicMap[v[0]])
			end
			write "\n"
			dumpTopicTree(topicMap, v, indent .. "  ")
		end
	end
end
-- dumpTopicTree(topicMap, topicTree, "")

local function loadTexts(filename, texts, topicMap, ignoreKeys) -- "INFO.INAM @ DIAL.NAME" => text
	errwrite("loading ", filename, " ... ")
	newLine = false
	local i, n, dn, ss, inam, dial, key = 1, 0, 0
	for line in io.lines(filename) do
		local topic = line:match "[Aa]dd[Tt]opic%s*\"\"(.-)\"\""
		if topic and not topicMap[topic:lower()] and not line:find ";%s*[Aa]dd[Tt]opic%s*\"\"" then
			warn("not found topic at line ", i, ": ", line)
		end
		if ss then
			local isEnd, s = readString(line, 1)
			ss[#ss + 1] = s
			if isEnd then
				s = concat(ss)
				ss = nil
				key = inam .. " @ " .. dial
				if texts[key] and texts[key] ~= s then
					warn("duplicated INFO.INAM @ DIAL.NAME: ", key)
				end
				texts[key] = s
				inam = nil
				n = n + 1
				dn = dn + 1
			else
				ss[#ss + 1] = "\r\n"
			end
		elseif line:find "^%x*:?%s*INFO%.QSTN%s*%[01%]$" then
			if ignoreKeys and key and dn == 1 then
				ignoreKeys[key] = true
			end
			key = nil
		else
			key = nil
			local s = line:match "^%x*:?%s*DIAL%.NAME%s*\"(.-)\""
			if s then
				dial = s:gsub("%$00$", "")
				dn = 0
			else
				s = line:match "^%x*:?%s*DIAL%.DATA %[(.-)%]"
				if s then
					if s == "01" then -- 00:Topic 01:Voice 02:Greeting 03:Persuasion 04:Journal
						dial = nil
					elseif s == "00" then
						dial = dial:lower()
					end
				elseif dial then
					local s = line:match "^%x*:?%s*INFO%.INAM%s*\"(.*)\"$"
					if s then
						inam = s:gsub("%$00$", "")
					elseif inam then
						s = line:match "^%x*:?%s*INFO%.NAME%s*\"(.*)$"
						if s then
							local isEnd, s = readString(s, 1)
							if isEnd == true then
								key = inam .. " @ " .. dial
								if texts[key] and texts[key] ~= s then
									warn("duplicated INFO.INAM @ DIAL.NAME: ", key)
								else
									texts[key] = s
								end
								inam = nil
								n = n + 1
								dn = dn + 1
							elseif isEnd == false then
								ss = { s, "\r\n" }
							else
								error("ERROR: invalid string at line " .. i)
							end
						end
					end
				end
			end
		end
		i = i + 1
	end
	if ss then
		error("ERROR: invalid string end at line " .. i)
	end
	errwrite(n, " texts\n")
	newLine = true
end

local function loadFile(src_filename, dst_filename)
	local texts, ignoreKeys = {}, {}
	local f = io.open(src_filename, "rb")
	if f then f:close() else src_filename = "../" .. src_filename end
	loadTexts(src_filename, texts, topicMap, ignoreKeys)
	local checkTexts = {}
	loadTexts(dst_filename, checkTexts, checkTopicMap, ignoreKeys)

	local function findTopics(texts, matches, topicTree, topicMap, ignoreKeys) -- "INFO.INAM @ DIAL.NAME" => { topics }
		for key, text in pairs(texts) do
			local t, oldFixes = text:match "^(.-)%s*({[^{}]*})%s*$"
			if t then
				text = t
			end
			local topics = {}
			local i, j, n = 1, 1, #text
			local curNode = topicTree
			text = text:lower()
			while j <= n do
				local c = sub(text, i, i)
				local nextNode = curNode[c]
				if nextNode then
					curNode = nextNode
					if curNode[0] and (i >= n or curNode[0]:find "%W" or sub(text, i + 1, i + 4):find "%W") then
						topics[#topics + 1] = curNode[0]
					end
					i = i + 1
				elseif sub(text, j, j):find "%w" then
					j = text:find("%W", j + 1) or n + 1
					i = j
					curNode = topicTree
				else
					j = j + 1
					i = j
					curNode = topicTree
				end
			end
			if ignoreKeys[key] then
				topics = {}
			end
			matches[key] = topics
		end
	end

	local function dumpMatchTexts(matches, texts)
		for key, topics in pairs(matches) do
			for _, topic in ipairs(topics) do
				write("[", topic, "] ")
			end
			write("=> ", texts[key]:gsub("\r", "\\r"):gsub("\n", "\\n"), "\n")
		end
	end

	local matches = {}
	findTopics(texts, matches, topicTree, topicMap, ignoreKeys)
	local checkMatches = {}
	findTopics(checkTexts, checkMatches, checkTopicTree, checkTopicMap, ignoreKeys)
	-- dumpMatchTexts(matches, texts)
	-- dumpMatchTexts(checkMatches, checkTexts)

	local fixedTexts = {}
	local n1, n2 = 0, 0
	for key, topics in pairs(matches) do
		local inam, topic = key:match "^(%S+) @ (.+)$"
		if not inam then error("ERROR: invalid key: " .. key) end
		local checkTopic = topicPairs[topic]
		if not checkTopic then checkTopic = dials[topic] and topic end
		if not checkTopic then error("ERROR: not found topic in topicPairs: [" .. topic .. "]") end
		local checkKey = inam .. " @ " .. checkTopic
		local checkTopics = checkMatches[checkKey]
		if not checkTopics then
			write("========== NOT FOUND KEY '", checkKey, "':\n")
			write(texts[key], "\n\n")
			n1 = n1 + 1
		else
			local notFounds = {}
			for _, topic in ipairs(topics) do
				local found
				local checkTopic = topicPairs[topic]
				if checkTopic then
					for _, t in ipairs(checkTopics) do
						if checkTopic == t then
							found = true
							break
						end
					end
				end
				if not found and not notFounds[topic] then
					notFounds[topic] = checkTopic
					notFounds[#notFounds + 1] = checkTopic
				end
			end
			local checkText = checkTexts[checkKey]
			if #notFounds > 0 then
				write("========== TOPIC UNMATCHED '", checkKey, "':\n")
				write(texts[key])
				write "\n---------- topics:"
				for _, topic in ipairs(topics) do
					if notFounds[topic] then
						write(" [", topic, "]=>[", notFounds[topic], "]")
					else
						write(" [", topic, "]")
					end
				end
				write "\n---------- topics:"
				for _, topic in ipairs(checkTopics) do
					write(" [", topic, "]")
				end
				write("\n", checkText, "\n\n")
				local t = checkText:match "^(.-)%s*{[^{}]*}%s*$"
				if t then
					checkText = t
				end
				t = { checkText, " {" }
				for _, checkTopic in ipairs(notFounds) do
					t[#t + 1] = checkTopic
					t[#t + 1] = ","
				end
				t[#t] = "}"
				fixedTexts[checkKey] = concat(t)
				n2 = n2 + 1
			elseif ignoreKeys[checkKey] then
				local t = checkText:match "^(.-)%s*{[^{}]*}%s*$"
				if t then
					fixedTexts[checkKey] = t
				end
			end
		end
	end

	errwrite("========== CHECK DONE ========== (", n1, " + ", n2, " errors)\n")
	newLine = true
	return fixedTexts
end

local function addEscape(s) -- for GBK
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
			d = byte(s, i + 1)
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
	return concat(t)
end

local function fixTexts(src_filename, dst_filename, fixedTexts)
	errwrite("loading ", src_filename, " ... \n")
	errwrite("creating ", dst_filename, " ... ")
	newLine = false
	local f = io.open(dst_filename, "wb")
	if not f then error "ERROR: can not create fix file" end
	local i, n, ss, inam, dial, out, key, prefix, t, isEnd = 1, 0
	local output = function()
		local fixedText = fixedTexts[key]
		if fixedText then
			if fixedText ~= t then
				n = n + 1
			end
			f:write(prefix, addEscape(fixedText), "\"\r\n")
		else
			f:write(prefix, addEscape(t:gsub("%s*{[^{}]*}%s*$", "")), "\"\r\n")
		end
	end
	for line in io.lines(src_filename) do
		out = false
		if ss then
			local isEnd, s = readString(line, 1)
			if isEnd == nil then
				error("ERROR: invalid string at line " .. i)
			end
			t = t .. "\r\n" .. s
			if isEnd then
				output()
				ss = nil
				inam = nil
			end
			out = true
		else
			local tag, s = line:match "^%x*:?[%s%-]*([%w_.]+)%s*(.*)$"
			if tag and canDeleteTag(tag) then
				out = true
			elseif tag == "DIAL.NAME" then
				s = s:match "^\"(.*)\"%s*$"
				if s then
					dial = s:gsub("%$00$", "")
				end
			elseif tag == "DIAL.DATA" then
				s = s:match "^%[(.*)%]%s*$"
				if s == "01" then -- 00:Topic 01:Voice 02:Greeting 03:Persuasion 04:Journal
					dial = nil
				elseif s == "00" then
					dial = dial:lower()
				end
			elseif dial then
				if tag == "INFO.INAM" then
					s = s:match "^\"(.*)\"%s*$"
					if s then
						inam = s:gsub("%$00$", "")
					end
				elseif inam and tag == "INFO.NAME" then
					prefix, s = line:match "^(%x*:?%s*INFO%.NAME%s*\")(.*)$"
					if s then
						key = inam .. " @ " .. dial
						isEnd, t = readString(s, 1)
						if isEnd == true then
							output()
							inam = nil
						elseif isEnd == false then
							ss = true
						else
							error("ERROR: invalid string at line " .. i)
						end
						out = true
					end
				end
			end
		end
		if not out then
			f:write(line, "\r\n")
		end
		i = i + 1
	end
	if ss then
		error("ERROR: invalid string at line " .. i)
	end
	f:close()
	errwrite("fixed ", n, " texts\n")
	newLine = true
end

for i, filename in ipairs(dst_filenames) do
	if filename[2] then
		local fixedTexts = loadFile(src_filenames[i], filename[1])
		if next(fixedTexts) then
			fixTexts(filename[1], filename[2], fixedTexts)
		end
	end
end
