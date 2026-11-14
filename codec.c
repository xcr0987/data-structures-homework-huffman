#include "huffman.h"

HufStatus encode_text(FILE *fp,const char *filename,const uint64_t index[GBK_SIZE],const HuffmanCode *codes)
{
	if(fp==NULL||filename==NULL||index==NULL)return HUF_ERR_ARG;
	if(fseek(fp,0,SEEK_SET)!=0)return HUF_ERR_IO;//指针挪到开头
	FILE *out=fopen(filename,"wb");
	if(out==NULL)return HUF_ERR_IO;
	uint16_t ch;
	HufStatus status;
	while((status=readGBK(fp,&ch))==HUF_OK){
		if(codes==NULL||index[ch]==0){status=HUF_ERR_ARG;break;}
		const HuffmanCode *code=codes+index[ch]-1;
		for(size_t i=0;i<code->len;i++){
			unsigned int bit=(code->bits[i/8]>>(7-i%8))&1u;
			if(fputc('0'+(int)bit,out)==EOF){status=HUF_ERR_IO;break;}
		}
		if(status!=HUF_OK)break;
	}
	if(status==HUF_EOF)status=HUF_OK;
	if(fclose(out)!=0)status=HUF_ERR_IO;
	return status;
}
