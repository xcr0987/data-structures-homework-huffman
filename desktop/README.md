<!-- 【修改】独立PySide6桌面版的开发、操作、Windows打包和验收说明。 -->
# 哈夫曼桌面版

桌面窗口使用PySide6和Qt Widgets，内部调用已有的C命令行程序。不使用网页、HTTP或浏览器，不改C源码，不负责C编译。

## 开发启动

需要Python 3.10或更新版本。第二次验收目录应提前放好本平台已编译、可运行的 `huffman`（macOS）或 `huffman.exe`（Windows），包括其必需运行库。

- macOS：双击 `start_mac.command`，也可以 `sh /完整路径/desktop/start_mac.command`。
- Windows：双击 `start_win.bat`。

首次启动在本目录创建 `.venv` 并安装固定版本依赖，以后直接运行。Windows使用 `py -3`，macOS使用 `python3`；创建环境时请使用Python 3.10+。如果系统的 `python3` 较旧，可先用已安装的新版本手动创建环境，再启动脚本：

```sh
python3.12 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

运行依赖为官方PySide6 Essentials 6.10.1，包含本界面使用的QtCore、QtGui、QtWidgets，不安装未使用的Addons。Windows开发启动用 `pythonw.exe`，不显示控制台；制作正式包时另使用无控制台GUI程序。

## 操作

三个标签页独立选择文件。

- 编码：原文、原文编码（默认自动）、压缩模式（默认delivery）、输出目录。标准产物名为 `table.txt`、`encoded.txt`、`encoded.bin`；single另存 `packed.bin`，evil另存 `evil.deflate`，原来的三个验收文件始终保留。
- 解码：delivery用表＋裸位流，text用表＋ASCII 0/1密文，single用packed，evil用原始DEFLATE；只显示当前需要的输入。另存为默认名为 `decoded.txt`，结果为GBK。evil仅支持动态块。
- 比较：选择原文、GBK解码文件和报告保存位置，报告默认名 `compare.txt`。原文统一转为GBK后逐字节比较；位置从1开始，不忽略空格和换行。

运行前统一确认覆盖，输出不能与任何输入指向同一文件。结果表显示文件大小和“解码所需”标记。比例为解码所需文件合计／原文原始大小；空文件显示“—”。选中一行后可打开文件或所在目录。

计算在一个后台线程中运行，期间禁用重复提交，窗口仍可移动和关闭。编码歧义时手动选择UTF-8或GBK后重试，其他错误可展开运行详情查看CLI原始输出。比较返回1表示内容不同，不当作执行失败。

## 临时目录和退出

每次任务在系统临时目录创建 `huffman-desktop-随机字符`，复制输入为固定ASCII文件名，在该目录调用C。中文文件名、输出目录和另存为路径由Python处理，不要求C程序直接打开这些路径。

正常成功或C运行失败后删除本次临时目录，包括C生成的 `temp/`。保存失败则保留完整临时产物至窗口退出，并在运行详情显示位置；“打开所在文件夹”可打开已经保存的文件目录，若没有已保存的文件则打开保留的临时目录。已经保存到用户目录的文件不会随退出删除。

任务运行时关闭窗口会询问是否结束任务。确认后停止C子进程、等待后台任务退出、清理临时文件；取消则继续运行。强制结束整个应用或断电不保证临时目录能被清理。

## Windows交付

在Windows 10/11 x64上准备Python 3.10+（x64），确认项目中的 `huffman.exe` 已经可以独立运行。其必需DLL放在该exe旁；本脚本不会编译C，也不会自动查找外部运行库。

双击桌面版的 `build_win.bat`。它安装 `requirements-build.txt` 中的依赖，再使用 `Huffman.spec` 制作：

```text
dist/HuffmanDesktop/
├── HuffmanGUI.exe
├── huffman.exe
├── 引擎原有运行库.dll（如果有）
├── _internal/
│   ├── Python和Qt运行依赖
│   └── licenses/
├── README.md
└── THIRD_PARTY.md
```

把整个 `HuffmanDesktop` 文件夹交付给老师，双击 `HuffmanGUI.exe`，不需要安装Python、PySide6、venv或浏览器组件。不要只复制GUI的exe。目录可以整体移动，程序从自身所在目录定位C引擎，用户输出不写入应用目录。

不制作安装向导、不做单文件打包。Windows包必须在Windows构建，macOS产物不能作为Windows可执行文件交付。第三方许可证保持原文，位置见 `THIRD_PARTY.md`。

## 验收状态

macOS开发环境为Python 3.12、PySide6 Essentials 6.10.1。调用层53次任务测试通过：三种编码、四种解码与直接CLI结果逐字节一致，覆盖中文及空格路径、空文件、单字符、UTF-8 BOM、GBK、编码歧义、坏表、坏密文、比较差异、输入同文件/硬链接保护、保存失败保留、取消和临时目录清理。

Qt控件自动化测试通过：输入切换、系统对话框返回处理、覆盖确认、打开结果、后台线程响应、拒绝退出继续任务、确认退出回收子进程、保留文件随窗口关闭清理。macOS真实窗口已完成编码、保存、重新读取密文解码、文件比较和正常退出，恢复的45,307字节与原文完全一致。结果默认不自动选中行，避开本机Qt/macOS表格辅助功能枚举触发的崩溃；需要打开文件时由用户自行选择结果。

Windows打包配置已验证无控制台选项、交付目录结构、C引擎/DLL复制、许可证收集及拒绝在非Windows平台构建；这属于配置测试，不能代替Windows实际打包。Windows交付exe尚未生成，也未实测无需Python启动和移动目录后的运行。

当前第二次验收目录未含C可执行文件，本机测试使用系统临时目录内编译的测试引擎；没有往项目或备份写入测试引擎。实际开发启动前，请按前述约定放好本平台已有的可执行文件。

<!-- 【修改】新增Mac独立应用交付方式，Windows构建方式不变。 -->
## macOS交付

先准备第二次验收目录中可运行的 `huffman`，再双击 `desktop/build_mac.command`。打包脚本只打包GUI和已有引擎，不编译C。

产物统一放在：

```text
desktop/_mac_dist/
├── HuffmanGUI.app
└── HuffmanGUI-mac.zip
```

Mac用户双击 `HuffmanGUI.app` 即可；给别人时可发送ZIP，解压后使用整个.app。Python、Qt、C引擎和许可证都包含在应用内部，无需额外安装Python或配置venv，也不需要在.app旁另放huffman。开发用的 `.venv`、`_mac_build` 不需要交付。

包的CPU架构与构建时使用的Python和引擎一致；本机生成的是Apple Silicon（arm64）版，不应当作Intel通用包。Windows产物目录与打包脚本不受影响。
