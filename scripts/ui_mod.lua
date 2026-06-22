local ui_files = {
	"../files/ui/contentselector.ui",
	"../files/ui/datafilespage.ui",
	"../files/ui/directorypicker.ui",
	"../files/ui/filedialog.ui",
	"../files/ui/graphicspage.ui",
	"../files/ui/importpage.ui",
	"../files/ui/mainwindow.ui",
	"../files/ui/settingspage.ui",
	"../files/ui/wizard/componentselectionpage.ui",
	"../files/ui/wizard/conclusionpage.ui",
	"../files/ui/wizard/existinginstallationpage.ui",
	"../files/ui/wizard/importpage.ui",
	"../files/ui/wizard/installationpage.ui",
	"../files/ui/wizard/installationtargetpage.ui",
	"../files/ui/wizard/intropage.ui",
	"../files/ui/wizard/languageselectionpage.ui",
	"../files/ui/wizard/methodselectionpage.ui",
}

local function trim(s)
	return s and s:gsub("^%s+", ""):gsub("%s+$", "")
end

local function escape(s)
	return s and s
			:gsub("&", "&amp;")
			:gsub("\"", "&quot;")
			:gsub("<", "&lt;")
			:gsub(">", "&gt;")
--			:gsub(" ", "&nbsp;")
end

local function unescape(s)
	return s and s
			:gsub("&amp;", "&")
			:gsub("&quot;", "\"")
			:gsub("&lt;", "<")
			:gsub("&gt;", ">")
			:gsub("&nbsp;", " ")
end

io.write "loading ui_en_cn.txt ... "
local trans = {}
local n = 0
local f = io.open("ui_en_cn.txt", "rb")
if f then
	f:close()
	local k
	for line in io.lines "ui_en_cn.txt" do
		line = trim(line)
		if line == "" then
			k = nil
		elseif not k then
			k = line
		else
			if k ~= line then
				if not trans[k] then n = n + 1 end
				trans[k] = line
			end
			k = nil
		end
	end
	io.write(n, " items\n")
else
	io.write("not found\n")
end

local used = {}
n = 0
for _, ui_file in ipairs(ui_files) do
	io.write("modifying ", ui_file, " ... ")
	local sn, tn = 0, 0
	f = io.open(ui_file, "rb")
	local src = f:read "*a"
	f:close()
	local dst = src:gsub("(<string>%s*)(.-)(%s*</string>)", function(a, s, b)
		sn = sn + 1
		local r = s
		s = unescape(s)
		local t = trans[s]
		if t then
			if not used[s] then
				used[s] = true
				n = n + 1
			end
			r = escape(t)
			tn = tn + 1
		end
		return a .. r .. b
	end)
	if dst ~= src then
		f = io.open(ui_file, "wb")
		f:write(dst)
		f:close()
	end
	io.write(tn, "/", sn, " items\n")
end

io.write("done! use ", n, " items\n")
