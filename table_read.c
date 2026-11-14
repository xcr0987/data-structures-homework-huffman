#include "huffman.h"

//【修改】新增原格式GBK码表读取，解析十六进制字符、频度及码字。

//表中的码字最长不超过GBK_SIZE-1，额外空间用于其余列。
static HufStatus read_line(FILE *fp,char *line,size_t capacity)//【修改】从fp读取一行到容量capacity的line，去掉行末换行；正常文件结束返回HUF_EOF。
{
	size_t n=0;
	int ch;
	while((ch=fgetc(fp))!=EOF&&ch!='\n'){
		if(ch==0||n+1>=capacity)return HUF_ERR_FORMAT;
		line[n++]=(char)ch;
	}
	if(ch==EOF&&ferror(fp))return HUF_ERR_IO;
	if(ch==EOF&&n==0)return HUF_EOF;
	if(n>0&&line[n-1]=='\r')--n;
	line[n]=0;
	return HUF_OK;
}

static HufStatus read_row(char *line,DecodeTree *tree,uint64_t *bits,uint8_t *seen)//【修改】解析一行十六进制字符、频数和码字并插入tree；seen检查重复字符，bits累计频数乘码长。
{
	char *first=strchr(line,'|'),*last=strrchr(line,'|');
	if(first!=line+4||first==last)return HUF_ERR_FORMAT;
	unsigned int ch=0;
	for(char *p=line;p<first;p++){
		unsigned int digit;
		if(*p>='0'&&*p<='9')digit=(unsigned int)(*p-'0');
		else if(*p>='A'&&*p<='F')digit=(unsigned int)(*p-'A'+10);
		else return HUF_ERR_FORMAT;
		ch=(ch<<4)|digit;
	}
	unsigned int lead=ch>>8,trail=ch&0xFF;
	if(ch>0x80&&(lead<0x81||lead>0xFE||trail<0x40||trail>0xFE||trail==0x7F))
		return HUF_ERR_FORMAT;
	if(seen[ch])return HUF_ERR_FORMAT;

	char *freq=last-1;
	while(freq>first&&*freq!='|')--freq;
	if(freq==first||freq[1]<'0'||freq[1]>'9')return HUF_ERR_FORMAT;
	char *end;
	errno=0;
	uintmax_t count=strtoumax(freq+1,&end,10);
	size_t len=strlen(last+1);
	if(end!=last||errno!=0||count==0||len==0||len>=GBK_SIZE||
	   count>(UINT64_MAX-*bits)/len)return HUF_ERR_FORMAT;
	HuffmanCode code={calloc((len+7)/8,1),len};
	if(code.bits==NULL)return HUF_ERR_MEM;
	HufStatus status=HUF_OK;
	for(size_t i=0;i<len;i++){
		char bit=last[i+1];
		if(bit=='1')code.bits[i/8]|=(uint8_t)(1u<<(7-i%8));
		else if(bit!='0'){status=HUF_ERR_FORMAT;break;}
	}
	if(status==HUF_OK)status=add_decode_code(tree,(uint16_t)ch,&code);
	free(code.bits);
	if(status==HUF_OK){seen[ch]=1;*bits+=(uint64_t)count*len;}
	return status;
}

HufStatus read_table(const char *path,DecodeTree *tree,uint64_t *bits)//【修改】读取path码表并填充tree，返回有效位数bits；内部关闭表文件，tree.nodes由调用者释放。
{
	FILE *fp=fopen(path,"rb");
	if(fp==NULL)return HUF_ERR_IO;
	size_t capacity=GBK_SIZE+128;
	char *line=malloc(capacity);
	uint8_t *seen=calloc(GBK_SIZE,1);
	HufStatus status=HUF_ERR_MEM;
	*bits=0;
	if(line!=NULL&&seen!=NULL){
		status=read_line(fp,line,capacity);
		if(status==HUF_OK&&strcmp(line,HUF_TABLE_HEADER)==0){
			while((status=read_line(fp,line,capacity))==HUF_OK){
				status=read_row(line,tree,bits,seen);
				if(status!=HUF_OK)break;
			}
			if(status==HUF_EOF)status=HUF_OK;
		}else if(status==HUF_OK||status==HUF_EOF)status=HUF_ERR_FORMAT;
	}
	free(line);free(seen);
	if(fclose(fp)!=0&&status==HUF_OK)status=HUF_ERR_IO;
	return status;
}
