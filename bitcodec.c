#include "huffman.h"

//【修改】裸位流和单文件正文共用写位函数；out由调用者打开和关闭。
HufStatus encode_bits(FILE *fp,FILE *out,const uint64_t index[GBK_SIZE],const HuffmanCode *codes)//【修改】重读fp，用index定位codes，高位优先写入out并补齐末字节；两个流均由调用者关闭。
{
	if(fp==NULL||out==NULL||index==NULL)return HUF_ERR_ARG;
	if(fseek(fp,0,SEEK_SET)!=0)return HUF_ERR_IO;
	HufStatus status=HUF_OK;
	//【修改】删除原8字节头、有效位计数和回填，只写位流正文。
	uint8_t byte=0;//暂存现在的字节
	unsigned int used=0;//当前字节填了几位
	uint16_t ch;
	while(status==HUF_OK){
		status=readGBK(fp,&ch);
		if(status!=HUF_OK)break;
		if(codes==NULL||index[ch]==0){status=HUF_ERR_ARG;break;}
		const HuffmanCode *code=&codes[index[ch]-1];
		for(size_t i=0;i<code->len;i++){
			unsigned int bit=(code->bits[i/8]>>(7-i%8))&1u;
			byte|=(uint8_t)(bit<<(7-used));
			++used;
			if(used==8){//写入文件
				if(fputc(byte,out)==EOF){status=HUF_ERR_IO;break;}
				byte=0;used=0;
			}
		}
	}
	if(status==HUF_EOF)status=HUF_OK;
	if(status==HUF_OK&&used>0&&fputc(byte,out)==EOF)status=HUF_ERR_IO;
	return status;
}

//【修改】新增高位优先读位，供两种二进制解码共用。
HufStatus read_bit(FILE *in,uint64_t i,int *byte,unsigned int *bit)//【修改】读取高位优先位流的第i位到bit；每8位更新byte缓存，调用者从i=0连续调用。
{
	if(i%8==0){
		*byte=fgetc(in);
		if(*byte==EOF)return ferror(in)?HUF_ERR_IO:HUF_ERR_FORMAT;
	}
	*bit=((unsigned int)*byte>>(7-i%8))&1u;
	return HUF_OK;
}
