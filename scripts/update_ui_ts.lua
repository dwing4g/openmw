local function readAll(fn)
	local f = io.open(fn, "rb")
	if not f then return end
	local s = f:read "*a"
	f:close()
	return s
end

local function trans(tsFileName, uiPath)
	local a = readAll(tsFileName):gsub("<context>(.-)</context>", function(c)
		local ms = {}
		for m in c:gmatch "<message>(.-)</message>" do
			ms[m:match "<source>(.-)</source>"] = m:match "<translation>(.-)</translation>"
		end

		local name = c:match "<name>(.-)</name>"
		local t = { "<context>\n    <name>", name, "</name>\n" }
		local ui = readAll(uiPath .. name:lower() .. ".ui")
		if ui then
			for s in ui:gmatch "<string>(.-)</string>" do
				s = s:gsub("'", "&apos;")
				if #s > 0 and ms[s] ~= false then
					if ms[s] then
						t[#t + 1] = string.format('    <message>\n        <source>%s</source>\n        <translation>%s</translation>\n    </message>\n', s, ms[s])
					else
						t[#t + 1] = string.format('    <message>\n        <source>%s</source>\n        <translation type="unfinished"></translation>\n    </message>\n', s)
					end
					ms[s] = false
				end
			end
			t[#t + 1] = "</context>"
			return table.concat(t)
		else
			return "<context>" .. c .. "</context>"
		end

		for k, v in pairs(ms) do
			if v then
				print("> " .. k)
				print("] " .. v)
			end
		end
	end)
	local f = io.open(tsFileName, "wb")
	f:write(a)
	f:close()
end

trans("../files/lang/launcher_zh_CN.ts", "../apps/launcher/ui/")
print "OK!"
