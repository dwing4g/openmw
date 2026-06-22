local open = io.open
local write = io.write

local ts_files = {
	"../files/lang/components_{lang}.ts",
	"../files/lang/launcher_{lang}.ts",
	"../files/lang/wizard_{lang}.ts",
}

for _, ts_file in ipairs(ts_files) do
	write(ts_file, " ... ")
	local f = open(ts_file:gsub("{lang}", "zh_CN"), "rb")
	local s = f:read "*a"
	f:close()

	local trans = {}
	local n1 = 0
	for msg in s:gmatch "<message>(.-)</message>" do
		local src = msg:match "<source>(.-)</source>"
		local dst = msg:match "<translation>(.-)</translation>"
		if not src or not dst then
			error("ERROR: no source or translation:\n" .. msg)
		end
		trans[src] = dst
		trans[src:lower()] = dst
		n1 = n1 + 1
	end

	f = open(ts_file:gsub("{lang}", "ru"), "rb")
	s = f:read "*a"
	f:close()

	local n2, n3 = 0, 0
	s = s:gsub('language="ru_RU">', 'language="zh_CN">')
		:gsub("<message>(.-)<source>(.-)</source>(.-)<translation>(.-)</translation>(.-)</message>", function(s1, src, s2, dst, s3)
		dst = trans[src] or trans[src:lower()]
		if dst then
			n2 = n2 + 1
		else
			dst = "###"
			n3 = n3 + 1
		end
		return "<message>" .. s1 .. "<source>" .. src .. "</source>" .. s2 .. "<translation>" .. dst .. "</translation>" .. s3 .. "</message>"
	end)

	f = open(ts_file:gsub("{lang}", "zh_CN"), "wb")
	f:write(s)
	f:close()

	write(n1, " => ", n2, " / ", n2 + n3, "\n")
end

write("done!\n")
