#ifndef HUFFMAN_H
#define HUFFMAN_H

#define GBK_SIZE 65536
//【修改】提取原GBK表头，供读表和写表共用。
#define HUF_TABLE_HEADER "GBK" "\xB1\xE0\xC2\xEB" "|" "\xD7\xD6\xB7\xFB" "|" "\xC6\xB5\xCA\xFD" "|huffman" "\xB1\xE0\xC2\xEB"
typedef enum{
    HUF_OK=0,		//成功
    HUF_EOF=1,		//读取结束
    HUF_ERR_IO=-1,	//读写错误
    HUF_ERR_GBK=-2,	//GBK编码错误
    HUF_ERR_MEM=-3,	//内存错误
    HUF_ERR_ARG=-4,	//参数错误
    //【修改】增加比较、格式和源编码歧义的状态。
    HUF_DIFFERENT=2,
    HUF_ERR_FORMAT=-5,
    HUF_ERR_AMBIG=-6
}HufStatus;

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<string.h>
#include<inttypes.h>
#include<errno.h>

//【修改】字符频度项：供排序、建树和写表使用；结构体本身不持有动态内存。
typedef struct{
	uint16_t ch;//【修改】GBK字符值：单字节直接存；双字节为(首字节<<8)|尾字节；evil中复用为符号编号。
	uint64_t freq;//【修改】该字符或符号出现的次数，也是建树的初始权重。
}CharFreq;
//【修改】编码树节点：双队列建树和DFS共用；全部节点连续放在HuffTree.nodes中。
typedef struct HuffNode{
	size_t index;//【修改】仅叶子有效，指向sorted下标；不是GBK值，同一位置对应codes[index]。
	uint64_t wei;//【修改】叶子为字符频数，父节点为左右子树权重之和。
	struct HuffNode *ls,*rs;//【修改】左右孩子；叶子二者均为NULL，指针借用nodes内元素，不单独释放。
}HuffNode;
//【修改】编码树整体：调用者分配nodes，make_tree填写节点和根；使用完只需free(nodes)。
typedef struct{
	HuffNode *head;//【修改】根节点，指向nodes中的一个元素；不单独分配或释放。
	HuffNode *nodes;//【修改】连续节点数组，容纳n个叶子和n-1个父节点；资源由调用者持有。
	size_t siz;//【修改】已经使用的节点数量，不是数组容量，也不是字符种类数。
}HuffTree;
//【修改】一个字符的紧凑哈夫曼码：整个codes数组与sorted一一对应，用free_codes统一释放。
typedef struct{
	uint8_t *bits;//【修改】动态位数组，占(len+7)/8字节；第i位在bits[i/8]的第7-i%8位，尾部无效位为0。
	size_t len;//【修改】有效编码位数，不是字节数；没有编码时为0。
}HuffmanCode;
//【修改】建树内部队列：A存叶子指针，B存父节点指针；只拥有指针数组，不拥有树节点。
typedef struct{
	HuffNode* *data;//【修改】动态数组，每项是一个HuffNode*，指向HuffTree.nodes中的节点。
	size_t front,rear;//【修改】front指队头，rear指下一入队位置，均按MAXSIZE循环；相等表示空。
	size_t MAXSIZE;//【修改】循环队列使用的槽位数，保留一槽区分空/满；队列结束只释放data。
}Queue;

HufStatus readGBK(FILE *fp,uint16_t *ch);//【修改】从fp读取一个完整GBK字符到ch；正常结束返回HUF_EOF，不关闭输入流。
//fp指向文件（已经成功指向）。
//freq已经成功初始化，且存储0。
//函数内不关闭fp，也不释放freq。
//成功返回HUF_OK，失败返回对应错误码。
//注意之后的函数传参都要注意传入变量的条件满足和是否释放。
HufStatus make_freq(FILE *fp,uint64_t freq[GBK_SIZE]);//【修改】遍历fp并按完整GBK字符累计freq；调用前freq须清零，不关闭流或释放数组。
HufStatus sort_freq(const uint64_t freq[GBK_SIZE],CharFreq **sorted,size_t *n);//【修改】从freq提取非零字符并按频数降序、字符升序排序；返回sorted和项数n，数组由调用者释放。
//【修改】树节点仍由调用方分配，编码流程移入encode.c。
//调用者分配足够的树节点
//创建两个队列，输入sorted数组，将sorted数组的每个元素分配节点，并放入队列A中，然后n-1次循环、更改head
HufStatus make_tree(const CharFreq *sorted,HuffTree *tree,size_t n);//【修改】使用sorted的n项数据和已分配的tree->nodes进行双队列建树，填写head和siz；节点由调用者释放。
//输入树。成功时分配n项编码表，codes[i]对应sorted[i]。
//【修改】由编码流程调用方用free_codes释放。
//失败时释放已分配编码，*codes为NULL。编码按高位优先存储。
HufStatus make_codes(const HuffTree *tree,HuffmanCode **codes,size_t n);//【修改】对tree做DFS并分配n项紧凑编码，codes[i]对应sorted[i]；失败自行清理，成功由调用者free_codes。
void free_codes(HuffmanCode *codes,size_t n);//【修改】释放n项codes中的每个位数组，再释放codes本身；允许codes为NULL。
//输入sorted数组，按频度降序、同频字符升序输出，不修改数组。
HufStatus write_table(const char *filename,const CharFreq *sorted,size_t n,const HuffmanCode *codes);//【修改】按sorted顺序把n项字符、频数及codes写入GBK对照表filename；内部关闭输出文件。
//index[ch]存sorted下标+1，0表示字符未出现；两个函数自行将fp定位到开头，不关闭fp。
HufStatus encode_text(FILE *fp,const char *filename,const uint64_t index[GBK_SIZE],const HuffmanCode *codes);//【修改】重读fp，通过index[ch]-1定位codes，写ASCII 0/1到filename；内部关闭输出，不关闭fp。
//【修改】encode_bits接收已打开的输出流，仅写高位优先的正文，末字节补零。
HufStatus encode_bits(FILE *fp,FILE *out,const uint64_t index[GBK_SIZE],const HuffmanCode *codes);//【修改】重读fp，用index定位codes，高位优先写入out并补齐末字节；两个流均由调用者关闭。
//【修改】新增读位接口，供二进制解码共用。
HufStatus read_bit(FILE *in,uint64_t i,int *byte,unsigned int *bit);//【修改】读取高位优先位流的第i位到bit；每8位更新byte缓存，调用者从i=0连续调用。

//【修改】新增操作模式、跨平台文件处理、编码调度和比较入口。
//【修改】原文编码选择：自动检测、明确UTF-8、明确GBK；只影响输入转码。
typedef enum{FROM_AUTO,FROM_UTF8,FROM_GBK}SourceEncoding;
//【修改】处理模式：表加裸位流、规范码单文件、表加文本位流（仅解码）、动态DEFLATE。
typedef enum{MODE_DELIVERY,MODE_SINGLE,MODE_TEXT,MODE_EVIL}HufMode;

HufStatus ensure_dir(const char *path);//【修改】创建path目录，目录已经存在也算成功；失败返回文件错误。
int same_file(const char *a,const char *b);//【修改】判断a和b是否指向同一文件（含硬链接）；相同返回1，否则返回0。
HufStatus copy_file(const char *src,const char *dst);//【修改】按字节将src复制到dst，内部打开和关闭文件；失败删除不完整目标文件。
//成功后返回temp/下已转成GBK并定位到开头的流，调用者负责关闭。
HufStatus open_gbk(const char *path,SourceEncoding from,FILE **result);//【修改】按from检测或转换path到GBK临时文件；result返回定位在开头的流，由调用者关闭，原文不改。

HufStatus encode_file(const char *input,const char *name,SourceEncoding from,HufMode mode);//【修改】按from读取input并完成统计、建树和编码；name为产物基名，mode决定附加压缩文件，内部统一释放资源。
HufStatus compare_files(const char *left,const char *right,const char *report,SourceEncoding from);//【修改】将left按from转成GBK后与right逐字节比较并写report；不同返回HUF_DIFFERENT。

//【修改】新增规范码生成与解码接口，单文件不需要外部码表。
static inline unsigned int code_bit(const HuffmanCode *code,size_t i)//【修改】返回code的第i位（从0开始，高位优先）；调用者保证i小于len，不修改码字。
{
	return (code->bits[i/8]>>(7-i%8))&1u;
}

HufStatus make_canonical(const CharFreq *symbols,const HuffmanCode *lengths,
                         size_t n,HuffmanCode **result);//【修改】按symbols字符值及lengths码长生成n项规范码，索引保持不变；result由调用者用free_codes释放。
//【修改】解码树节点：由码字逐位插入生成，不使用频数重建；evil中也用于识别DEFLATE符号。
typedef struct{
	size_t child[2];//【修改】读到0或1时前往的节点下标；0表示分支不存在（根恰好位于下标0）。
	uint16_t ch;//【修改】仅叶子有效，普通解码存GBK字符值，evil存字面量/长度/距离/码长符号编号。
	int leaf;//【修改】非0表示叶子；普通解码到此输出字符并回根，evil则返回符号继续解释。
}DecodeNode;
//【修改】解码树整体：按需扩容连续节点数组，根为nodes[0]；调用者零初始化并最终free(nodes)。
typedef struct{
	DecodeNode *nodes;//【修改】动态节点数组；扩容可能改变地址，所以孩子使用下标而非指针。
	size_t count,capacity;//【修改】count为已用节点数，capacity为已分配节点容量；count为0表示尚未建根。
}DecodeTree;

HufStatus add_decode_code(DecodeTree *tree,uint16_t ch,const HuffmanCode *code);//【修改】把ch的code逐位插入tree；已有前缀或码字冲突时返回格式错误，节点数组由调用者释放。
HufStatus decode_stream(FILE *in,FILE *out,const DecodeTree *tree,uint64_t bits,int text);//【修改】用tree将in的bits个有效位还原到out；text非0读ASCII位，否则读高位优先正文；不关闭流。
HufStatus read_table(const char *path,DecodeTree *tree,uint64_t *bits);//【修改】读取path码表并填充tree，返回有效位数bits；内部关闭表文件，tree.nodes由调用者释放。
HufStatus decode_table(const char *table,const char *input,const char *output,int text);//【修改】读取table建立解码树，将input恢复到output；text选择文本或裸二进制，内部管理文件和树。
HufStatus write_packed(FILE *fp,const char *output,const CharFreq *sorted,size_t n,
                       const HuffmanCode *codes,const uint64_t *index,uint64_t bits);//【修改】将fp写成output单文件：头部、字符码长表和规范码正文；bits为有效位数，内部释放临时码表。
HufStatus decode_packed(const char *input,const char *output);//【修改】从input的内置码长表恢复规范码并解码到output；内部关闭文件并释放解码树。

//【修改】evil使用原始DEFLATE动态块，不需要外部码表；fp由调用者关闭。
HufStatus encode_evil(FILE *fp,const char *output);//【修改】重读fp并以LZ77加动态哈夫曼块写入output；跨块保留32KiB历史，fp由调用者关闭。
HufStatus decode_evil(const char *input,const char *output);//【修改】读取input的原始DEFLATE动态块，通过环形窗口恢复output；内部关闭文件并释放树和窗口。

#endif
