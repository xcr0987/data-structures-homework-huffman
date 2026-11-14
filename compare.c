#include "huffman.h"

//【修改】新增转成GBK后逐字节比较，并写比较报告。

HufStatus compare_files(const char *left,const char *right,const char *report,SourceEncoding from)//【修改】将left按from转成GBK后与right逐字节比较并写report；不同返回HUF_DIFFERENT。
{
	if(same_file(left,report)||same_file(right,report))return HUF_ERR_ARG;
	FILE *a=NULL,*b=NULL,*out=NULL;
	HufStatus status=open_gbk(left,from,&a);
	if(status!=HUF_OK)return status;
	b=fopen(right,"rb");
	if(b==NULL){status=HUF_ERR_IO;goto done;}
	uint64_t matched=0;
	int x,y;
	do{
		x=fgetc(a);y=fgetc(b);
		if(x==y&&x!=EOF)++matched;
	}while(x==y&&x!=EOF);
	if(ferror(a)||ferror(b)){status=HUF_ERR_IO;goto done;}
	char result[160];
	if(x==y)snprintf(result,sizeof(result),"MATCH: %" PRIu64 " bytes\n",matched);
	else{
		status=HUF_DIFFERENT;
		if(x==EOF||y==EOF)
			snprintf(result,sizeof(result),"DIFFERENT at byte %" PRIu64 ": %s ended\n",
			         matched+1,x==EOF?"left":"right");
		else snprintf(result,sizeof(result),"DIFFERENT at byte %" PRIu64 ": %02X != %02X\n",
		              matched+1,(unsigned int)x,(unsigned int)y);
	}
	out=fopen(report,"wb");
	if(out==NULL||fputs(result,out)==EOF){status=HUF_ERR_IO;goto done;}
	fputs(result,stdout);
done:
	if(fclose(a)!=0)status=HUF_ERR_IO;
	if(b!=NULL&&fclose(b)!=0)status=HUF_ERR_IO;
	if(out!=NULL&&fclose(out)!=0)status=HUF_ERR_IO;
	return status;
}
