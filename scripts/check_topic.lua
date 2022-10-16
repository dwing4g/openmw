-- luajit check_topic.lua tes3cn.txt tes3cn_topic.txt

local string = string
local byte = string.byte
local char = string.char
local sub = string.sub
local table = table
local concat = table.concat
local io = io
local write = io.write
local tonumber = tonumber
local ipairs = ipairs
local pairs = pairs
local error = error
local next = next
local arg = arg

local src_filenames = {
	"Morrowind.txt",
--	"Bloodmoon.txt",
--	"Tribunal.txt",
}

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

local function loadTopics(filename, topicMap, topicMapR) -- topic => INAM, INAM => { topics }
	io.stderr:write("loading ", filename, " ... ")
	local n, topic = 0
	for line in io.lines(filename) do
		local s = line:match "^%x+:%s*DIAL%.NAME%s*\"(.-)\""
		if s then
			topic = s:gsub("%$00$", ""):lower()
		else
			s = line:match "^%x+:%s*DIAL%.DATA %[(.-)%]"
			if s then
				if s ~= "00" then -- 00:Topic 01:Voice 02:Greeting 03:Persuasion 04:Journal
					topic = nil
				end
			elseif topic then
				s = line:match "^%x+:%s*INFO%.INAM%s*\"(.*)\"$"
				if s then
					s = s:gsub("%$00$", "")
					if topicMap[topic] then
						io.stderr:write("WARN: duplicated topic: ", topic, " => ", s, "\n")
					end
					topicMap[topic] = s
					local topicR = topicMapR[s]
					if topicR then
						topicR[#topicR + 1] = topic
					else
						topicMapR[s] = { topic }
					end
					n = n + 1
					topic = nil
				end
			end
		end
	end
	io.stderr:write(n, " topics\n")
end

local topicMap, topicMapR = {}, {}
for _, filename in ipairs(src_filenames) do
	loadTopics(filename, topicMap, topicMapR)
end

local checkTopicMap, checkTopicMapR = {}, {}
loadTopics(arg[1], checkTopicMap, checkTopicMapR)

local f = io.open(arg[2], "rb")
if not f then
	io.stderr:write("creating ", arg[2], " ... ")
	f = io.open(arg[2], "wb")
	local n = 0
	for topic, inam in pairs(topicMap) do
		f:write("[", topic, "] =>")
		local checkTopics = checkTopicMapR[inam]
		if checkTopics then
			for _, checkTopic in ipairs(checkTopics) do
				f:write(" [", checkTopic, "]")
			end
		end
		if not checkTopics or #checkTopics ~= 1 then
			f:write " !!!"
		end
		f:write("\r\n")
		n = n + 1
	end
	f:close()
	io.stderr:write(n, " topics\n")
	io.stderr:write "========== DUMP TOPICS DONE ==========\n"
	return
end
f:close()
local topicPairs = {}
local i = 1
io.stderr:write("loading ", arg[2], " ... ")
for line in io.lines(arg[2]) do
	local topic, checkTopic, more = line:match "^%s*%[(.-)%]%s*=>%s*%[(.-)%](.*)$"
	if not topic or more:find "%S" then
		error("ERROR: invalid topic file at line " .. i)
	end
	if not topicMap[topic] then
		error("ERROR: invalid topic at line " .. i)
	end
	if not checkTopicMap[checkTopic] then
		error("ERROR: invalid check topic at line " .. i)
	end
	topicPairs[topic] = checkTopic
	i = i + 1
end
io.stderr:write(i - 1, " topics\n")

local function createTopicTree(topicMap, topicTree)
	for topic in pairs(topicMap) do
		local curNode = topicTree
		for i = 1, #topic do
			local word = topic:sub(i, i)
			local nextNode = curNode[word]
			if not nextNode then
				nextNode = {}
				curNode[word] = nextNode
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

local function loadTexts(filename, texts, topicMap) -- "INFO.INAM @ DIAL.NAME" => text
	io.stderr:write("loading ", filename, " ... ")
	local i, n, ss, inam, dial = 1, 0
	for line in io.lines(filename) do
		local topic = line:match "[Aa]dd[Tt]opic \"\"(.-)\"\""
		if topic and not topicMap[topic:lower()] then
			io.stderr:write("WARN: not found topic at line ", i, ": ", line, "\n")
		end
		if ss then
			local isEnd, s = readString(line, 1)
			ss[#ss + 1] = s
			if isEnd then
				s = concat(ss)
				ss = nil
				local key = inam .. " @ " .. dial
				if texts[key] then
					io.stderr:write("WARN: duplicated INFO.INAM @ DIAL.NAME: ", key)
					if texts[key] == s then io.stderr:write " but the same text" end
					io.stderr:write "\n"
				end
				texts[key] = s
				inam = nil
				n = n + 1
			else
				ss[#ss + 1] = "\r\n"
			end
		else
			local s = line:match "^%x+:%s*DIAL%.NAME%s*\"(.-)\""
			if s then
				dial = s:gsub("%$00$", ""):lower()
				if not topicMap[dial] then
					dial = nil
				end
			elseif dial then
				local s = line:match "^%x+:%s*INFO%.INAM%s*\"(.*)\"$"
				if s then
					inam = s:gsub("%$00$", "")
				elseif inam then
					s = line:match "^%x+:%s*INFO%.NAME%s*\"(.*)$"
					if s then
						local isEnd, s = readString(s, 1)
						if isEnd == true then
							local key = inam .. " @ " .. dial
							if texts[key] then
								io.stderr:write("WARN: duplicated INFO.INAM @ DIAL.NAME: ", key)
								if texts[key] == s then io.stderr:write " but the same text" end
								io.stderr:write "\n"
							end
							texts[key] = s
							inam = nil
							n = n + 1
						elseif isEnd == false then
							ss = { s, "\r\n" }
						else
							error("ERROR: invalid string at line " .. i)
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
	io.stderr:write(n, " texts\n")
end

local texts = {}
for _, filename in ipairs(src_filenames) do
	loadTexts(filename, texts, topicMap)
end
local checkTexts = {}
loadTexts(arg[1], checkTexts, checkTopicMap)

local function findTopics(texts, matches, topicTree, topicMap) -- "INFO.INAM @ DIAL.NAME" => { topics }
	for key, text in pairs(texts) do
		local topics, inams = {}, {}
		local i, j, n = 1, 1, #text
		local curNode, bestTopic = topicTree
		while j <= n do
			local c = text:sub(i, i)
			local nextNode = curNode[c]
			if nextNode then
				curNode = nextNode
				if curNode[0] and (i >= n or not text:sub(i + 1, i + 1):find "%w") then bestTopic = curNode[0] end
				i = i + 1
			else
				local topic = not c:find "%w" and curNode[0] or bestTopic
				if topic then
					local inam = topicMap[topic]
					local lastTopic = inams[inam]
					if lastTopic then
						if lastTopic ~= topic then
							io.stderr:write("WARN: duplicated INFO.INAM '", inam, "' for DIAL.NAME: [", lastTopic, "] and [", topic, "]\n")
						end
					else
						inams[inam] = topic
						topics[#topics + 1] = topic
					end
					j = i
				else
					j = j + 1
					i = j
				end
				curNode = topicTree
				bestTopic = nil
				while i <= n and text:sub(i - 1, i - 1):find "%w" do
					i = i + 1
					j = i
				end
			end
		end
		local topic = bestTopic
		if topic then
			local inam = topicMap[topic]
			local lastTopic = inams[inam]
			if lastTopic then
				if lastTopic ~= topic then
					io.stderr:write("WARN: duplicated INFO.INAM '", inam, "' for DIAL.NAME: [", lastTopic, "] and [", topic, "]\n")
				end
			else
				inams[inam] = topic
				topics[#topics + 1] = topic
			end
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
findTopics(texts, matches, topicTree, topicMap)
local checkMatches = {}
findTopics(checkTexts, checkMatches, checkTopicTree, checkTopicMap)
-- dumpMatchTexts(matches, texts)
-- dumpMatchTexts(checkMatches, checkTexts)

local n = 0
for key, topics in pairs(matches) do
	local inam, topic = key:match "^(%S+) @ (.+)$"
	if not inam then error("ERROR: invalid key: " .. key) end
	local checkTopic = topicPairs[topic]
	if not checkTopic then error("ERROR: not found topic in topicPairs: [" .. topic .. "]") end
	local checkKey = inam .. " @ " .. checkTopic
	local checkTopics = checkMatches[checkKey]
	if not checkTopics then
		write("========== NOT FOUND KEY '", checkKey, "':\n")
		write(texts[key], "\n\n")
		n = n + 1
	else
		local notfounds = {}, {}
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
			if not found then
				notfounds[topic] = checkTopic
			end
		end
		if next(notfounds) then
			write("========== TOPIC UNMATCHED '", checkKey, "':\n")
			write(texts[key])
			write "\n---------- topics:"
			for _, topic in ipairs(topics) do
				if notfounds[topic] then
					write(" [", topic, "]=>[", notfounds[topic], "]")
				else
					write(" [", topic, "]")
				end
			end
			write "\n---------- topics:"
			for _, topic in ipairs(checkTopics) do
				write(" [", topic, "]")
			end
			write("\n", checkTexts[checkKey], "\n\n")
			n = n + 1
		end
	end
end

io.stderr:write("========== CHECK DONE ========== (", n, " errors)\n")
