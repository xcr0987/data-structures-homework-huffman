<!-- 【修改】新增第二次验收操作、文件格式和后续GUI设计说明。 -->
# 数据结构课程作业：哈夫曼编码（第 5、6 次验收）

整个工程只编译一个程序。macOS 运行 `sh build_mac.command`；Windows 在 GCC 环境运行 `build_win.bat`。在项目目录运行 `./huffman`（Windows 为 `huffman.exe`）可进入菜单。直接指定参数则不进入菜单，便于验收脚本调用。

```sh
./huffman encode
./huffman decode
./huffman compare

./huffman encode --mode single
./huffman decode --mode single
./huffman decode --mode text

# 【修改】evil使用动态DEFLATE，解码无需外部表。
./huffman encode --mode evil --from gbk
./huffman decode --mode evil
./huffman compare --from gbk
```

Windows 使用同一组参数，把命令开头换成 `huffman.exe` 即可。源文件默认为 `SourceDoc.txt`，源编码默认为 `auto`。自动模式识别 UTF-8（含 BOM）和项目原有的 GBK 字节范围；两种解释都合法时要求选 `--from utf8` 或 `--from gbk`，不猜。已是 GBK 的字节原样保留；UTF-8 不能无损转换时返回错误。中间 GBK 文件写入 `temp/`，不会覆盖输入。macOS 使用系统 iconv；Windows 使用系统 Win32 转码 API。

<!-- 【修改】evil也保留原有三个验收文件，只交付额外的DEFLATE文件。 -->
三个编码模式都在 `encode-output/` 写出旧格式的 `table.txt`、ASCII `encoded.txt` 和无头 `encoded.bin`。交付型把表和裸位流复制到 `decode-input/`；单文件型另写 `packed.bin`，只复制该文件；evil另写 `evil.deflate`，只复制该文件。程序会列出本次解码文件，不清理此前其他操作生成的文件。根目录可能还保留旧验收的 8 字节头位流，新解码器应使用 `encode-output/` 或 `decode-input/` 下的产物。

自定义文件名示例（含空格时加引号）：

```sh
./huffman encode --input "my source.txt" --from gbk --name work
./huffman decode --input decode-input/work.encoded.bin --table decode-input/work.table.txt --output restored.txt
./huffman compare --left "my source.txt" --right restored.txt --from gbk --output check.txt
```

<!-- 【修改】移除evil预留说明，以下原有格式只适用于delivery和single。 -->
解码不需要原文。裸位流用表中各项的「频度 × 码长」之和决定有效位数；文本位流模式读取 ASCII 0/1。单文件格式为 `HUF`、版本字节 `01`、ULEB128 有效位数、ULEB128 字符种类数、按 GBK 值升序的「1～2 字节字符 + ULEB128 码长」、高位优先正文。规范码按码长、GBK 值重建，正文不使用外部表。两种二进制位流的末字节未用低位补零。

<!-- 【修改】新增evil实现、固定参数和兼容范围说明。 -->
`evil.c` 对转好的GBK字节执行LZ77，然后用字面量／长度树、距离树和码长树写标准 [RFC 1951](https://www.rfc-editor.org/rfc/rfc1951.html) 动态块。文件是原始DEFLATE流，没有自定义头、zlib/gzip包装或校验和，不需要安装zlib。字节按低位到高位填充，哈夫曼码本身高位先写，与原来 `encoded.bin` 的位序不同。

参数固定：32768字节滑动窗口，匹配长度3～258，三字节哈希链最多查找512个候选，向后看一个位置的延迟匹配，每块最多65535字节，跨块保留窗口。复用现有双队列建树、DFS和规范码；超出标准码长限制时将本轮正权重向上折半重建，保证限长合法，但不保证限长条件下最优。不提供等级或算法参数，不写固定表块、存储块；难压缩的文件可能变大。

`./huffman decode --mode evil` 默认从 `decode-input/evil.deflate` 解码到 `decoded.txt`；支持原有 `--input`、`--output`，不接受 `--table`。自定义基名仍用 `--name work`，额外文件为 `work.evil.deflate`。解码器仅接受动态块，支持跨块和重叠复制；能读取标准工具产生的纯动态块原始DEFLATE流，不接受含固定表块或存储块的流，也不直接读取 `.gz`、`.zip` 或zlib包装。

比较时将左侧统一为 GBK，再和右侧逐字节比较；默认生成 `compare.txt`。位置从 1 开始；退出码 0 为成功或一致，1 为内容不同，2 为参数、格式或读写错误。

<!-- 【修改】更新已实现的GUI状态；GUI单向调用同一个CLI。 -->
桌面版位于 `desktop/`，使用 PySide6 调用已编译的 C 程序，启动及打包见该目录 README；它与C核心单向连接，不另写压缩和解压算法。已保存的网页版本与备份保持独立。

两次验收沿用同一棵双队列哈夫曼树：权重相等时优先取 A 队列的叶子。第二次的新增、迁移和调整在源码中用「【修改】」注明。

**相比于第一次验收，freq.c tree.c codec.c 没有变化，而 bitcodec.c 删除了预留8字节、增加了读位方法等；huffman.h main.c 进行了更多修改，table.c 进行了拆分，decode_table.c decode.c packed.c platform.c compare.c 均实现新功能。**

<!-- 【修改】按当前C实现补充函数调用架构；箭头表示调用，括号注明源文件。 -->
## 函数调用架构

### 统一入口与输入编码

```text
main (main.c)
├─ 无参数：menu → read_value / parse_mode / source_encoding
├─ 有参数：arguments → parse_mode / source_encoding
└─ run
   ├─ encode  → encode_file    (encode.c)
   ├─ decode  → 按模式选择解码入口，见下图
   └─ compare → compare_files (compare.c)

open_gbk (platform.c，编码和比较共用)
├─ read_source：按二进制读取原文
├─ auto：valid_utf8 / valid_gbk 判别；歧义返回HUF_ERR_AMBIG
├─ utf8：去掉开头BOM → valid_utf8 → utf8_to_gbk
│  ├─ Windows：MultiByteToWideChar → WideCharToMultiByte → 回转核对
│  └─ macOS：convert(iconv) → 回转核对
└─ valid_gbk → ensure_dir("temp") → new_temp → 返回GBK文件流
```

`open_gbk` 不改原文。明确 `--from gbk` 时不转码；UTF-8 中不能无损表示为 GBK 的字符会报错。菜单遇到编码歧义会追问；命令行需要明确给出 `--from`。

### 三种编码模式

所有模式先执行同一套普通哈夫曼流程；single、evil 的附加压缩文件各有自己的码字，不与普通表混用。

```text
encode_file (encode.c)
├─ open_gbk (platform.c) → GBK原文流fp
├─ make_freq (freq.c)
│  └─ readGBK：按完整GBK字符累计频数
├─ sort_freq (freq.c) → sorted[]：频数降序、同频字符升序
├─ make_tree (tree.c) → tree：双队列建树，同权优先A叶子队列
├─ make_codes (codes.c)
│  └─ dfs → set_bit：左0右1 → codes[]，与sorted[]逐项对应
├─ write_table (table_write.c) → encode-output/table.txt
│  └─ write_character：实际GBK字节，特殊字符转义
├─ 计算有效位数Σ(频数×码长)，复用freq为index[ch]=下标+1
├─ encode_text (codec.c) → encode-output/encoded.txt
│  └─ readGBK → index → codes → ASCII字符0/1
├─ encode_bits (bitcodec.c) → encode-output/encoded.bin
│  └─ readGBK → index → codes → 高位优先裸正文，末字节补0
└─ 按模式完成附加输出和交付复制
   ├─ delivery：copy_file(table.txt、encoded.bin) → decode-input/
   ├─ single：write_packed → packed.bin → copy_file → decode-input/
   └─ evil：encode_evil → evil.deflate → copy_file → decode-input/
```

空文件跳过建树和DFS，仍生成对应格式的合法产物。编码流程最后由 `free_codes`、`free`、`fclose` 统一释放码表、频度表、排序表、树和输入流。

```text
single：write_packed (packed.c)
├─ make_canonical (codes.c)
│  └─ cmp_code_order排序(码长、字符值) → 用原码长生成规范码
├─ 写HUF标记、版本
├─ put_number：ULEB128有效位数、字符种类数
├─ 按GBK值升序写字符及ULEB128码长（不存频数和完整码字）
└─ encode_bits：用规范码写正文 → packed.bin

evil：encode_evil (evil.c，输入仍是fp的GBK字节)
├─ 分块读取，保留上一块最后32768字节历史
├─ make_tokens：LZ77，得到字面量或(长度、距离)
│  ├─ find_match → hash3：哈希链找匹配
│  ├─ insert_position → hash3：更新历史候选
│  └─ length_symbol / distance_symbol：统计两类符号频数
└─ write_block：每块写标准动态DEFLATE
   ├─ make_deflate_codes：生成字面量/长度树和距离树
   │  ├─ make_tree → make_codes：复用原建树及DFS
   │  └─ canonical_codes → make_canonical：规范码；超限折半权重重建
   ├─ make_repeats：码长串用16/17/18做游程编码
   ├─ make_deflate_codes：为游程符号生成码长树（第三棵树）
   └─ put_bits / put_code：块头、机器表、Token正文及256结束符
```

`evil` 不是对 `encoded.bin` 再压缩，而是重新对 **GBK原文字节**执行LZ77和三棵动态树。DEFLATE普通数值低位先写，哈夫曼码高位先写；字节从低位开始填，与普通裸位流的高位优先填充不同。

| 编码模式 | 解码必须拿到的文件 | 正文如何生成 |
|---|---|---|
| delivery | `table.txt` + `encoded.bin` | 原双队列树的哈夫曼码 |
| single | `packed.bin` | 保留原码长，改用规范哈夫曼码 |
| evil | `evil.deflate` | GBK字节的LZ77 + 动态DEFLATE三棵树 |

三种模式都保留 `table.txt`、`encoded.txt`、`encoded.bin`；single、evil 各额外保留自己的单文件。`text` 是第四种**解码入口**，不是第四种压缩模式。

### 解码：四个入口、三种压缩格式

```text
run (main.c)
├─ delivery / text → decode_table (decode_table.c)
│  ├─ read_table (table_read.c)
│  │  ├─ read_line → read_row：解析十六进制字符、频数、码字
│  │  └─ add_decode_code → new_node (decode.c)：按码字建立解码树
│  │     同时累计有效位数Σ(频数×码长)，不根据频数重新建树
│  └─ decode_stream (decode.c)
│     ├─ delivery：read_bit (bitcodec.c)，读裸二进制位
│     ├─ text：fgetc，读ASCII字符0/1
│     └─ 逐位走树 → 叶子写GBK字符 → 回到根
│
├─ single → decode_packed (packed.c)
│  ├─ read_packed
│  │  ├─ get_number：读取ULEB128有效位数和字符种类数
│  │  ├─ readGBK / get_number：读取字符及码长
│  │  ├─ make_canonical (codes.c)：重建规范码
│  │  └─ add_decode_code (decode.c)：建立解码树
│  └─ decode_stream → read_bit：从当前正文位置解码
│
└─ evil → decode_evil (evil.c)
   ├─ get_bits：读取BFINAL和BTYPE，只接受动态块BTYPE=2
   ├─ read_trees：读取动态头并展开码长游程
   │  ├─ make_decode_tree：先构建码长树，再构建正文树和距离树
   │  │  └─ canonical_codes → make_canonical → add_decode_code
   │  └─ get_symbol → get_bits：解出码长值和重复标记
   └─ get_symbol：从正文树读取符号
      ├─ 0~255：直接输出该字节
      ├─ 256：当前块结束；非最后块继续读取下一块
      └─ 257~285：长度+附加位 → 距离树符号+附加位
         → 从32768字节环形窗口复制并输出，允许重叠复制
```

`get_symbol` 和普通解码都复用 `DecodeTree`，但读位顺序不同。四个解码入口都不需要原文；输出均为转码后的GBK内容，UTF-8输入的原始字节形式不会被恢复。

### 文件比较

```text
run (main.c)
└─ compare_files (compare.c)
   ├─ same_file (platform.c)：避免报告覆盖输入
   ├─ open_gbk(left, from)：左侧原文统一为GBK
   ├─ fopen(right, "rb")：右侧解码结果按字节读取
   ├─ fgetc逐字节比较，保留换行、空格
   └─ 同一份结果同时写到屏幕和report
      ├─ 全部一致：MATCH，返回HUF_OK
      ├─ 首个字节不同或一侧提前结束：DIFFERENT，返回HUF_DIFFERENT
      └─ 读写失败：返回文件错误，不当作内容不同
```

差异位置从1开始。GUI也调用这同一个CLI入口，不另写压缩算法。

<!-- 【修改】新增同内容UTF-8样例和独立标准库解压演示。 -->
## UTF-8与标准解压演示

`SourseDoc_utf8.txt`（按本次指定拼写）由上一级 `SourceDoc.txt` 的GBK字节无损转换得到，UTF-8无BOM，正文和换行不变。以下命令均在第二次验收目录运行。

Windows PowerShell：

```powershell
# UTF-8原文 → GBK → evil压缩，同时生成原有三个验收产物
.\huffman.exe encode --input SourseDoc_utf8.txt --from utf8 --mode evil

# 先用我们自己的解码器演示
.\huffman.exe decode --mode evil --output decoded.txt
.\huffman.exe compare --left SourseDoc_utf8.txt --right decoded.txt --from utf8

# 再用Python标准库zlib独立解压，不调用我们的解码代码、不读取码表
py -c "from pathlib import Path; import zlib; Path('zlib_decoded.txt').write_bytes(zlib.decompress(Path('encode-output/evil.deflate').read_bytes(), -15))"
.\huffman.exe compare --left SourseDoc_utf8.txt --right zlib_decoded.txt --from utf8
```

macOS将 `.\huffman.exe` 换为 `./huffman`，将 `py -c` 换为 `python3 -c`，其余参数相同。GUI演示时选择该样例，来源编码选择UTF-8即可；解码结果仍为GBK，比较时左侧原文编码也选UTF-8。

这里 `-15` 明确指定**原始DEFLATE流**，没有zlib或gzip包装；它调用的是Python标准库所链接的独立zlib解压实现，无需pip安装。参见 [Python zlib文档](https://docs.python.org/3/library/zlib.html#zlib.decompress)。

不能直接把 `.deflate` 改名成 `.zip` 或 `.gz` 让资源管理器解压：ZIP和gzip还需要各自的容器头、校验等字段。最直接的标准兼容性演示是上面的zlib解压，加本程序逐字节比较；证明压缩正文符合标准，不依赖我们的私有码表或解码器。
