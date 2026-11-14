cd "$(dirname "$0")"
clang -std=c11 main.c platform.c encode.c freq.c tree.c codes.c table_write.c table_read.c codec.c bitcodec.c decode.c decode_table.c packed.c compare.c evil.c -liconv -o huffman # 【修改】编译新增模块（含evil），链接系统iconv。
