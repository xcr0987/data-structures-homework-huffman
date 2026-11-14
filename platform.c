#include "huffman.h"

//【修改】新增跨平台目录、文件复制及原文转GBK；转换后的输入放在temp。
#include <limits.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <iconv.h>
#endif

static FILE *new_temp(const char *path)//【修改】独占创建path指定的二进制临时文件；成功返回流供调用者关闭，失败返回NULL。
{
#ifdef _WIN32
	int fd=_open(path,_O_CREAT|_O_EXCL|_O_RDWR|_O_BINARY,_S_IREAD|_S_IWRITE);
	if(fd<0)return NULL;
	FILE *fp=_fdopen(fd,"w+b");
	if(fp==NULL){_close(fd);remove(path);errno=EIO;}
	return fp;
#else
	return fopen(path,"w+bx");
#endif
}

HufStatus ensure_dir(const char *path)//【修改】创建path目录，目录已经存在也算成功；失败返回文件错误。
{
#ifdef _WIN32
	if(_mkdir(path)==0)return HUF_OK;
	struct _stat info;
	if(errno==EEXIST&&_stat(path,&info)==0&&(info.st_mode&_S_IFMT)==_S_IFDIR)return HUF_OK;
#else
	if(mkdir(path,0777)==0)return HUF_OK;
	struct stat info;
	if(errno==EEXIST&&stat(path,&info)==0&&S_ISDIR(info.st_mode))return HUF_OK;
#endif
	return HUF_ERR_IO;
}

int same_file(const char *a,const char *b)//【修改】判断a和b是否指向同一文件（含硬链接）；相同返回1，否则返回0。
{
	if(strcmp(a,b)==0)return 1;
#ifdef _WIN32
	DWORD share=FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;
	HANDLE x=CreateFileA(a,0,share,NULL,OPEN_EXISTING,0,NULL);
	HANDLE y=CreateFileA(b,0,share,NULL,OPEN_EXISTING,0,NULL);
	BY_HANDLE_FILE_INFORMATION i,j;
	int same=x!=INVALID_HANDLE_VALUE&&y!=INVALID_HANDLE_VALUE&&
		GetFileInformationByHandle(x,&i)&&GetFileInformationByHandle(y,&j)&&
		i.dwVolumeSerialNumber==j.dwVolumeSerialNumber&&
		i.nFileIndexHigh==j.nFileIndexHigh&&i.nFileIndexLow==j.nFileIndexLow;
	if(x!=INVALID_HANDLE_VALUE)CloseHandle(x);
	if(y!=INVALID_HANDLE_VALUE)CloseHandle(y);
	return same;
#else
	struct stat x,y;
	return stat(a,&x)==0&&stat(b,&y)==0&&x.st_dev==y.st_dev&&x.st_ino==y.st_ino;
#endif
}

HufStatus copy_file(const char *src,const char *dst)//【修改】按字节将src复制到dst，内部打开和关闭文件；失败删除不完整目标文件。
{
	if(same_file(src,dst))return HUF_OK;
	FILE *in=fopen(src,"rb");
	if(in==NULL)return HUF_ERR_IO;
	FILE *out=fopen(dst,"wb");
	if(out==NULL){fclose(in);return HUF_ERR_IO;}
	unsigned char buf[4096];
	size_t n;
	HufStatus status=HUF_OK;
	while((n=fread(buf,1,sizeof(buf),in))>0){
		if(fwrite(buf,1,n,out)!=n){status=HUF_ERR_IO;break;}
	}
	if(ferror(in))status=HUF_ERR_IO;
	if(fclose(in)!=0)status=HUF_ERR_IO;
	if(fclose(out)!=0)status=HUF_ERR_IO;
	if(status!=HUF_OK)remove(dst);
	return status;
}

static HufStatus read_source(const char *path,unsigned char **data,size_t *len)//【修改】读取path全部字节，写入data和len；data由调用者释放，本函数关闭文件。
{
	FILE *fp=fopen(path,"rb");
	if(fp==NULL)return HUF_ERR_IO;
	HufStatus status=HUF_ERR_IO;
	if(fseek(fp,0,SEEK_END)==0){
		long size=ftell(fp);
		if(size>=0&&(uintmax_t)size<SIZE_MAX&&fseek(fp,0,SEEK_SET)==0){
			*len=(size_t)size;
			*data=malloc(*len==0?1:*len);
			if(*data==NULL)status=HUF_ERR_MEM;
			else if(fread(*data,1,*len,fp)==*len&&!ferror(fp))status=HUF_OK;
		}
	}
	if(fclose(fp)!=0&&status==HUF_OK)status=HUF_ERR_IO;
	return status;
}

static int valid_utf8(const unsigned char *s,size_t n)//【修改】检查s的前n个字节是否满足UTF-8字节规则；合法返回1，否则返回0。
{
	for(size_t i=0;i<n;){
		unsigned int a=s[i++];
		if(a<0x80)continue;
		size_t k;
		if(a>=0xC2&&a<=0xDF)k=1;
		else if(a>=0xE0&&a<=0xEF)k=2;
		else if(a>=0xF0&&a<=0xF4)k=3;
		else return 0;
		if(n-i<k)return 0;
		unsigned int b=s[i];
		if((a==0xE0&&b<0xA0)||(a==0xED&&b>=0xA0)||
		   (a==0xF0&&b<0x90)||(a==0xF4&&b>=0x90))return 0;
		while(k--!=0)if((s[i++]&0xC0)!=0x80)return 0;
	}
	return 1;
}

//和readGBK使用同一字节规则；已是GBK的原文不重新映射。
static int valid_gbk(const unsigned char *s,size_t n)//【修改】按readGBK相同的单/双字节规则检查s的前n字节；合法返回1。
{
	for(size_t i=0;i<n;){
		unsigned int a=s[i++];
		if(a<=0x80)continue;
		if(a>0xFE||i==n)return 0;
		unsigned int b=s[i++];
		if(b<0x40||b>0xFE||b==0x7F)return 0;
	}
	return 1;
}

#ifdef _WIN32
static HufStatus utf8_to_gbk(const unsigned char *src,size_t n,unsigned char **out,size_t *len)//【修改】把src的n个UTF-8字节无损转为GBK，返回out及长度len；out由调用者释放。
{
	if(n>INT_MAX)return HUF_ERR_ARG;
	int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char *)src,(int)n,NULL,0);
	if(count<=0)return HUF_ERR_GBK;
	if((size_t)count>SIZE_MAX/sizeof(wchar_t))return HUF_ERR_MEM;
	wchar_t *wide=malloc((size_t)count*sizeof(*wide));
	if(wide==NULL)return HUF_ERR_MEM;
	HufStatus status=HUF_ERR_GBK;
	if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char *)src,(int)n,wide,count)==count){
		BOOL replaced=FALSE;
		int bytes=WideCharToMultiByte(936,WC_NO_BEST_FIT_CHARS,wide,count,NULL,0,NULL,&replaced);
		if(bytes>0&&!replaced){
			*out=malloc((size_t)bytes);
			if(*out==NULL)status=HUF_ERR_MEM;
			else if(WideCharToMultiByte(936,WC_NO_BEST_FIT_CHARS,wide,count,
			        (char *)*out,bytes,NULL,&replaced)==bytes&&!replaced){
				wchar_t *back=malloc((size_t)count*sizeof(*back));
				if(back==NULL)status=HUF_ERR_MEM;
				else if(MultiByteToWideChar(936,MB_ERR_INVALID_CHARS,(const char *)*out,bytes,
				                           back,count)==count&&memcmp(wide,back,(size_t)count*sizeof(*wide))==0){
					*len=(size_t)bytes;status=HUF_OK;
				}
				free(back);
			}
		}
	}
	free(wide);
	return status;
}
#else
static HufStatus convert(const char *to,const char *from,const unsigned char *src,size_t n,
                         unsigned char *out,size_t cap,size_t *len)//【修改】用iconv将src的n字节从from转到to，写入容量cap的out，实际字节数写入len。
{
	iconv_t cd=iconv_open(to,from);
	if(cd==(iconv_t)-1)return HUF_ERR_GBK;
	char *in=(char *)src,*dst=(char *)out;
	size_t left=n,space=cap;
	size_t result=iconv(cd,&in,&left,&dst,&space);
	iconv_close(cd);
	if(result!=0||left!=0)return HUF_ERR_GBK;
	*len=cap-space;
	return HUF_OK;
}

static HufStatus utf8_to_gbk(const unsigned char *src,size_t n,unsigned char **out,size_t *len)//【修改】把src的n个UTF-8字节无损转为GBK，返回out及长度len；out由调用者释放。
{
	if(n>SIZE_MAX/2)return HUF_ERR_MEM;
	*out=malloc(n*2);
	unsigned char *back=malloc(n+1);
	HufStatus status=HUF_ERR_MEM;
	if(*out!=NULL&&back!=NULL){
		status=convert("CP936","UTF-8",src,n,*out,n*2,len);
		if(status==HUF_OK){
			size_t size;
			status=convert("UTF-8","CP936",*out,*len,back,n+1,&size);
			if(status==HUF_OK&&(size!=n||memcmp(back,src,n)!=0))status=HUF_ERR_GBK;
		}
	}
	free(back);
	return status;
}
#endif

HufStatus open_gbk(const char *path,SourceEncoding from,FILE **result)//【修改】按from检测或转换path到GBK临时文件；result返回定位在开头的流，由调用者关闭，原文不改。
{
	*result=NULL;
	unsigned char *data=NULL,*converted=NULL;
	size_t n=0;
	HufStatus status=read_source(path,&data,&n);
	if(status!=HUF_OK)goto done;
	int bom=n>=3&&memcmp(data,"\xEF\xBB\xBF",3)==0;
	int ascii=1;
	for(size_t i=0;i<n;i++)if(data[i]>=0x80){ascii=0;break;}
	const unsigned char *bytes=data;
	size_t len=n;
	if(from==FROM_AUTO){
		if(ascii)from=FROM_GBK;
		else if(bom)from=FROM_UTF8;
		else{
			int utf8=valid_utf8(data,n),gbk=valid_gbk(data,n);
			if(utf8&&gbk){status=HUF_ERR_AMBIG;goto done;}
			if(!utf8&&!gbk){status=HUF_ERR_GBK;goto done;}
			from=utf8?FROM_UTF8:FROM_GBK;
		}
	}
	if(from==FROM_UTF8){
		if(bom){bytes+=3;len-=3;}
		if(!valid_utf8(bytes,len)){status=HUF_ERR_GBK;goto done;}
		if(!ascii&&len!=0){
			status=utf8_to_gbk(bytes,len,&converted,&len);
			if(status!=HUF_OK)goto done;
			bytes=converted;
		}
	}
	if(!valid_gbk(bytes,len)){status=HUF_ERR_GBK;goto done;}

	status=ensure_dir("temp");
	if(status!=HUF_OK)goto done;
	//独占创建，输入即使在temp里也不会被覆盖。
	char temp[64];
	FILE *fp=NULL;
	for(unsigned int i=0;i<10000;i++){
		snprintf(temp,sizeof(temp),"temp/source-%u.gbk",i);
		fp=new_temp(temp);
		if(fp!=NULL||errno!=EEXIST)break;
	}
	if(fp==NULL){status=HUF_ERR_IO;goto done;}
	if(fwrite(bytes,1,len,fp)!=len||fseek(fp,0,SEEK_SET)!=0){
		fclose(fp);remove(temp);status=HUF_ERR_IO;
	}else *result=fp;
done:
	free(converted);free(data);
	return status;
}
