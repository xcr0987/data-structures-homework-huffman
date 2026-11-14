#include "huffman.h"

//【修改】把第一次验收的字符显示和写表从table.c分出，行格式照旧。
static int write_character(FILE *fp,uint16_t ch)
{
	char text[5]={0};
	if(ch>0xFF){//两个字节的（汉字）
		text[0]=(char)(ch>>8);
		text[1]=(char)(ch&0xFF);
	}else{
		switch(ch){
			case '\0':text[0]='\\';text[1]='0';break;
			case '\n':text[0]='\\';text[1]='n';break;
			case '\r':text[0]='\\';text[1]='r';break;
			case '\t':text[0]='\\';text[1]='t';break;
			case '\\':text[0]='\\';text[1]='\\';break;
			default:
				//控制符等不宜直接输出
				if(ch<=0x20||ch==0x7F||ch=='|')snprintf(text,sizeof(text),"\\x%02X",(unsigned int)ch);
				else text[0]=(char)ch;
		}
	}
	return fputs(text,fp)!=EOF;
}

HufStatus write_table(const char *filename,const CharFreq *sorted,size_t n,const HuffmanCode *codes)//【修改】按sorted顺序把n项字符、频数及codes写入GBK对照表filename；内部关闭输出文件。
{
	if(filename==NULL)return HUF_ERR_ARG;
	if(n>0&&(sorted==NULL||codes==NULL))return HUF_ERR_ARG;
	FILE *fp=fopen(filename,"wb");
	if(fp==NULL)return HUF_ERR_IO;
	HufStatus status=HUF_OK;
	//输出"GBK编码|字符|频数|huffman编码\n"
	//【修改】读表和写表共用原GBK表头，输出字节不变。
	if(fputs(HUF_TABLE_HEADER "\n",fp)==EOF)status=HUF_ERR_IO;
	for(size_t i=0;i<n&&status==HUF_OK;i++){
		if(fprintf(fp,"%04X|",(unsigned int)sorted[i].ch)<0||
		   !write_character(fp,sorted[i].ch)||
		   fprintf(fp,"|%" PRIu64 "|",sorted[i].freq)<0){//PRIu64是输出uint64_t的格式宏
			status=HUF_ERR_IO;break;
		}
		for(size_t j=0;j<codes[i].len;j++){
			unsigned int bit=(codes[i].bits[j/8]>>(7-j%8))&1u;
			if(fputc('0'+(int)bit,fp)==EOF){status=HUF_ERR_IO;break;}
		}
		if(status!=HUF_OK)break;
		if(fputc('\n',fp)==EOF){status=HUF_ERR_IO;break;}
	}
	if(fclose(fp)!=0)status=HUF_ERR_IO;
	return status;
}
