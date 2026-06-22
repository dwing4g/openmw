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
			:gsub(" ", "&nbsp;")
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

io.write "creating ui_en_cn.txt\n"
local used = {}
n = 0
f = io.open("ui_en_cn.txt", "wb")
for _, ui_file in ipairs(ui_files) do
	io.write("loading ", ui_file, " ... ")
	local sn, tn = 0, 0
	for line in io.lines(ui_file) do
		local s = line:match "<string>(.-)</string>"
		s = unescape(trim(s))
		if s and not used[s] then
			used[s] = true
			sn = sn + 1
			f:write(s, '\n')
			local t = trans[s]
			if t then tn = tn + 1 end
			f:write(t or s, '\n\n')
		end
	end
	io.write(tn, "/", sn, " items\n")
	n = n + tn
end
f:close()

io.write("done! use ", n, " items\n")
