■ OpenMW 中文支持版本 ■

遵照GPL协议开源: https://github.com/dwing4g/openmw

■ 首次运行说明(必读)
0. 首先确保操作系统是 Windows 7 以上, 必须是 x64 版本, 不支持32位的Windows.
1. 首次运行前先确定是否安装了较新版本的VC动态库, 可尝试启动 openmw-iniimporter,
   如果提示"无法定位程序输入点..."则需要安装, 安装包的官方下载地址: https://aka.ms/vs/17/release/vc_redist.x64.exe
   如果有个窗口一闪而过则说明已经装好.
2. 启动 openmw-launcher, 如果提示"Could not create directory ...", 则需要再次启动一次, 或者以管理员权限再次启动.
   如果提示"Run Installation Wizard", 则点击进入设置向导, 依次点击: Next -> Existing Installation, Next -> Browse,
   找到游戏本体中的"Morrowind.esm", Next -> 选"Chinese(GBK)", Next -> 3个"Import..."都选上, Next -> Finish
3. 进入 OpenMW Launcher 主界面, 确保以下几个设置:
   (1) Settings页面的首个下拉列表选择了"Chinese(GBK)".
   (2) Data Files/Content Files页面中如果列表框是空的, 点击右上角的"Refresh Data Files", 在左上角下拉列表选择"Morrowind.esm",
       下面的列表框就会出现一些esm和esp文件, 通常必选"Tribunal.esm"和"Bloodmoon.esm"两个官方资料片,
       如果有汉化插件(如"tes3cn.esp")则也选上.
   (3) Data Files/Archive Files页面中选中"Morrowind.bsa","Tribunal.bsa","Bloodmoon.bsa"三个官方资源包.
   (4) Graphics页面中可设置游戏分辨率(Resolution).
   (5) Advanced/Interface页面中的"GUI scaling factor"可调大一些, 尤其是高分辨率模式下, "Font size"也可以调大.
   (6) 最后点下方的"Close"保存设置并退出.
4. 打开"文档"文件夹, 找到"My Games\OpenMW\openmw.cfg"文件, 用记事本打开,如果没有这行则手动加上: fallback=Fonts_Font_0,zh_CN
5. 以上都设置好后, 以后每次开启游戏都只需启动 openmw 即可.
6. 首次进入游戏, 先进入"Options", 确保以下几个设置:
   (1) 在"首选项"页面中, 打开"字幕".
   (2) 在"Language(语言)"页面中, 左边的下拉框选择"Chinese (China)", 修改此项需要重启游戏才能生效.


■ 其它说明
1. 虽然也能支持UTF-8编码的汉化, 但必要性不大, 可以先继续用GBK编码.
   字体可以修改, 找到 resources\vfs\fonts\zh_CN.omwfont, 修改里面的字体文件名"LXGWWenKaiGB-Regular.ttf", 同时放入字体文件即可.
2. resources\vfs\l10n\ 里有一些 zh_CN.yaml 但大部分还没翻译成中文.
   openmw.cfg 里也有些英文需要汉化, 如职业选择的问答题.
   以上文件修改成中文后需要以UTF-8编码保存.


■ 编译源码的一些说明:
1. 需要准备 7z.exe, 安装 Python3, Git for Windows, Visual Studio 2022 (include CMake)
2. 在 Git Bash 下进入 openmw 根目录, 执行: CI/before_script.msvc.sh -k -p Win64 -v 2022
3. 用 Visual Studio 2022 打开 MSVC2022_64\OpenMW.sln 并执行编译
4. 如果需要编译 MyGUI, 需要先下载编译FreeType, 然后使用命令: cmake -DMYGUI_RENDERSYSTEM=1 -DFREETYPE_INCLUDE_DIRS=... -DFREETYPE_LIBRARY=...

■ 汉化版的ChangeLog:

● 2022-10-18 v4
1. scripts: 改进检查esp导出文本的工具 check_topic.lua 并支持补充可能遗漏的关键词
2. scripts: 改进 tes3enc.lua 支持非GB2312字符的检查
3. openmw: 修正MyGUI自动换行有时出现字符缺失的bug
4. openmw: 修正clock_cast触发的异常
5. files/data/i10n: 更新zh_CN语言配置(完整翻译)

● 2022-10-16 v3
1. openmw: 修正MyGUI的中文自动换行, 撤回之前在字间加空格的做法
2. scripts: 改进 tes3dec.lua, tes3enc.lua
3. scripts: 增加统计和检查esp导出文本的工具 check_topic.lua, count_text.lua
4. files/data/fonts: 默认字体改用免费开源的 LXGWWenKaiGB-Regular.ttf 下载地址: https://github.com/lxgw/LxgwWenkaiGB/releases
5. openmw: 去掉通过 IsHungAppWindow 判断 AppFrozen 的方式, 避免过于敏感地弹出 Frozen 消息

● 2022-10-12 v2
1. openmw: 字幕框的中文自动换行, 改进字幕框的显示时间
2. scripts: 增加 esm,esp,omwsave 的导出导入文本文件的工具: tes3dec.lua, tes3enc.lua

● 2022-10-10 v1
1. openmw,launcher,wizard: 语言列表增加 Chinese(GBK) 和 UTF-8
2. openmw: 对话框支持中文自动换行
3. components/compiler: 修正一些std::isdigit和std::isalpha对扩展字符的支持
4. components/crashcatcher: 判断窗口Frozen的超时时间从5秒增加到15秒
5. components/to_utf8: 支持GBK和UTF-8的编码转换
6. files/data/fonts: 增加中文字体配置
7. files/data/i10n: 增加zh_CN语言配置(少量翻译)
8. readme-zh_CN: 增加这个说明文档
9. copy_resources.bat: 增加复制最新资源到编译结果目录中的脚本
