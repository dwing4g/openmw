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
   (3) Data Files/Archive Files页面中选中"Tribunal.bsa","Morrowind.bsa","Bloodmoon.bsa"三个官方资源包.
   (4) Graphics页面中可设置游戏分辨率(Resolution).
   (5) Advanced/Interface页面中的"GUI scaling factor"可调大一些, 尤其是高分辨率模式下, "Font size"也可以调大.
   (6) 最后点下方的"Close"保存设置并退出.
4. 打开"文档"文件夹, 找到"OpenMW\openmw.cfg"文件, 用记事本打开,如果没有这行则手动加上: fallback=Fonts_Font_0,zh_CN
5. 以上都设置好后, 以后每次开启游戏都只需启动 openmw 即可.
6. 首次进入游戏, 先进入"Options", 确保以下几个设置:
   (1) 在"首选项"页面中, 打开"字幕".
   (2) 在"Language(语言)"页面中, 左边的下拉框选择"Chinese (China)", 修改此项需要重启游戏才能生效.


虽然也能支持UTF-8编码的汉化, 但必要性不大, 可以先继续用GBK编码.
字体可以修改, 找到 resources\vfs\fonts\zh_CN.omwfont, 修改里面的字体文件名"msyh.ttc", 同时放入字体文件即可.

resources\vfs\l10n\ 里有一些 zh_CN.yaml 但大部分还没翻译成中文.
openmw.cfg 里也有些英文需要汉化, 如职业选择的问答题.
以上文件修改成中文后需要以UTF-8编码保存.


以下是编译源码的一些说明:
1. 需要准备 7z.exe, 安装 Python3, Git for Windows, Visual Studio 2022 (include CMake)
2. 在 Git Bash 下进入 openmw 根目录, 执行: CI/before_script.msvc.sh -k -p Win64 -v 2022
3. 用 Visual Studio 2022 打开 MSVC2022_64\OpenMW.sln 并执行编译
