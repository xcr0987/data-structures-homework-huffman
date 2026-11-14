#include "huffman.h"
static const char *hufStatusStr(HufStatus status)//【修改】把status错误码转换成供main显示的英文提示，返回常量字符串。
{
    switch (status) {
        case HUF_OK:		return "Success";
        case HUF_EOF:		return "End of file";
        case HUF_ERR_IO:	return "File I/O error";
        //【修改】错误提示同时覆盖UTF-8到GBK的无损转换。
        case HUF_ERR_GBK:	return "Invalid source encoding or cannot convert losslessly to GBK";
        case HUF_ERR_MEM:	return "Memory allocation failed";
        //【修改】自定义输出路径可能和输入指向同一文件。
        case HUF_ERR_ARG:	return "Invalid argument or output aliases an input";
        //【修改】新入口需要区分格式错误和源编码歧义。
        case HUF_ERR_FORMAT:return "Invalid code table or bit stream";
        case HUF_ERR_AMBIG:	return "Ambiguous source encoding; choose --from utf8 or --from gbk";
        default:			return "Unknown error";
    }
}

//【修改】新增菜单和参数解析，三种操作仍使用同一个main。
typedef enum{OP_ENCODE,OP_DECODE,OP_COMPARE}Operation;
//【修改】一次操作的参数集合：菜单或arguments填写，run只读使用；所有字符串均借用，不在此释放。
typedef struct{
	Operation operation;//【修改】本次执行编码、解码还是比较。
	HufMode mode;//【修改】压缩/解码格式；比较操作不使用此字段。
	SourceEncoding from;//【修改】原文编码选择；解码操作不需要原文编码。
	const char *input,*output,*table,*name,*left,*right;//【修改】依次为输入、输出、外部表、产物基名、比较左/右文件；NULL交给调度补默认值，指向argv或菜单values。
}Options;

static void usage(FILE *fp)//【修改】向fp输出命令行用法；fp由调用者管理，不在这里关闭。
{
	//【修改】启用evil编码和解码，不增加算法配置参数。
	fputs("huffman encode  [--input FILE] [--name STEM] [--from auto|utf8|gbk] [--mode delivery|single|evil]\n"
	      "huffman decode  [--mode delivery|single|text|evil] [--input FILE] [--table FILE] [--output FILE]\n"
	      "huffman compare [--left FILE] [--right FILE] [--from auto|utf8|gbk] [--output FILE]\n"
	      "Run without arguments for the interactive menu.\n",fp);
}

static int source_encoding(const char *s,SourceEncoding *from)//【修改】把s中的auto/utf8/gbk写成枚举from；识别成功返回1，否则返回0。
{
	if(strcmp(s,"auto")==0)*from=FROM_AUTO;
	else if(strcmp(s,"utf8")==0)*from=FROM_UTF8;
	else if(strcmp(s,"gbk")==0)*from=FROM_GBK;
	else return 0;
	return 1;
}

static int parse_mode(const char *s,HufMode *mode)//【修改】把s中的模式名写入mode；识别成功返回1，否则返回0。
{
	if(strcmp(s,"delivery")==0)*mode=MODE_DELIVERY;
	else if(strcmp(s,"single")==0)*mode=MODE_SINGLE;
	else if(strcmp(s,"text")==0)*mode=MODE_TEXT;
	else if(strcmp(s,"evil")==0)*mode=MODE_EVIL;
	else return 0;
	return 1;
}

static const char *default_input(const Options *opt)//【修改】根据opt中的操作和模式返回默认输入路径，不创建文件。
{
	if(opt->operation==OP_ENCODE)return "SourceDoc.txt";
	if(opt->mode==MODE_SINGLE)return "decode-input/packed.bin";
	//【修改】evil解码只需一个原始DEFLATE文件。
	if(opt->mode==MODE_EVIL)return "decode-input/evil.deflate";
	if(opt->mode==MODE_TEXT)return "encode-output/encoded.txt";
	return "decode-input/encoded.bin";
}

static const char *default_table(HufMode mode)//【修改】按mode返回默认码表路径；text读取编码目录，其余读取解码目录。
{
	return mode==MODE_TEXT?"encode-output/table.txt":"decode-input/table.txt";
}

static int read_value(const char *prompt,const char *fallback,char *line,int size)//【修改】按整行读取输入到line，size为缓冲区容量；留空使用fallback，成功返回1。
{
	printf("%s [%s]: ",prompt,fallback);
	fflush(stdout);
	if(fgets(line,size,stdin)==NULL)return 0;
	size_t n=strlen(line);
	if(n>0&&line[n-1]=='\n')line[--n]=0;
	else if(!feof(stdin))return 0;
	if(n>0&&line[n-1]=='\r')line[--n]=0;
	if(n==0)snprintf(line,(size_t)size,"%s",fallback);
	return 1;
}

static int menu(Options *opt,char values[3][1024])//【修改】显示交互菜单并填写opt；路径存于调用者提供的values中，成功返回1。
{
	char choice[32];
	if(!read_value("1 Encode / 2 Decode / 3 Compare","1",choice,sizeof(choice)))return 0;
	if(strcmp(choice,"1")==0)opt->operation=OP_ENCODE;
	else if(strcmp(choice,"2")==0)opt->operation=OP_DECODE;
	else if(strcmp(choice,"3")==0)opt->operation=OP_COMPARE;
	else return 0;

	if(opt->operation!=OP_COMPARE){
		//【修改】菜单解码同样开放evil。
		const char *prompt=opt->operation==OP_ENCODE?"Mode (delivery/single/evil)":"Mode (delivery/single/text/evil)";
		if(!read_value(prompt,"delivery",choice,sizeof(choice))||!parse_mode(choice,&opt->mode))return 0;
	}
	if(opt->operation!=OP_DECODE){
		if(!read_value("Source encoding (auto/utf8/gbk)","auto",choice,sizeof(choice))||
		   !source_encoding(choice,&opt->from))return 0;
	}
	if(opt->operation==OP_COMPARE){
		if(!read_value("Original file","SourceDoc.txt",values[0],1024)||
		   !read_value("Decoded file","decoded.txt",values[1],1024)||
		   !read_value("Report file","compare.txt",values[2],1024))return 0;
		opt->left=values[0];opt->right=values[1];opt->output=values[2];
	}else{
		if(!read_value("Input file",default_input(opt),values[0],1024))return 0;
		opt->input=values[0];
		if(opt->operation==OP_ENCODE){
			if(!read_value("Output name (leave blank for standard names)","",values[1],1024))return 0;
			opt->name=values[1];
		}else{
			//【修改】evil和single都不需要外部表。
			if(opt->mode!=MODE_SINGLE&&opt->mode!=MODE_EVIL){
				if(!read_value("Table file",default_table(opt->mode),values[1],1024))return 0;
				opt->table=values[1];
			}
			if(!read_value("Decoded file","decoded.txt",values[2],1024))return 0;
			opt->output=values[2];
		}
	}
	return 1;
}

static int arguments(Options *opt,int argc,char **argv)//【修改】解析argc/argv并填写opt；路径直接引用argv，参数合法返回1。
{
	if(strcmp(argv[1],"encode")==0)opt->operation=OP_ENCODE;
	else if(strcmp(argv[1],"decode")==0)opt->operation=OP_DECODE;
	else if(strcmp(argv[1],"compare")==0)opt->operation=OP_COMPARE;
	else return 0;
	static const char *const keys[]={"--input","--output","--table","--name","--left","--right","--mode","--from"};
	unsigned int seen=0;
	for(int i=2;i<argc;i+=2){
		size_t k=0;
		while(k<sizeof(keys)/sizeof(*keys)&&strcmp(argv[i],keys[k])!=0)++k;
		if(k==sizeof(keys)/sizeof(*keys)||i+1==argc||seen&(1u<<k))return 0;
		seen|=1u<<k;
		const char *value=argv[i+1];
		switch(k){
			case 0:if(opt->operation==OP_COMPARE)return 0;opt->input=value;break;
			case 1:if(opt->operation==OP_ENCODE)return 0;opt->output=value;break;
			case 2:if(opt->operation!=OP_DECODE)return 0;opt->table=value;break;
			case 3:if(opt->operation!=OP_ENCODE)return 0;opt->name=value;break;
			case 4:if(opt->operation!=OP_COMPARE)return 0;opt->left=value;break;
			case 5:if(opt->operation!=OP_COMPARE)return 0;opt->right=value;break;
			case 6:if(opt->operation==OP_COMPARE||!parse_mode(value,&opt->mode))return 0;break;
			case 7:if(opt->operation==OP_DECODE||!source_encoding(value,&opt->from))return 0;break;
		}
	}
	return 1;
}

static HufStatus run(const Options *opt)//【修改】按opt调度编码、解码或比较，并补齐未填写的默认路径；返回操作状态。
{
	if(opt->operation==OP_COMPARE)
		return compare_files(opt->left?opt->left:"SourceDoc.txt",opt->right?opt->right:"decoded.txt",
		                     opt->output?opt->output:"compare.txt",opt->from);
	const char *input=opt->input?opt->input:default_input(opt);
	if(opt->operation==OP_ENCODE)return encode_file(input,opt->name?opt->name:"",opt->from,opt->mode);
	const char *output=opt->output?opt->output:"decoded.txt";
	HufStatus status;
	//【修改】evil独立解码，不通过普通字符码表。
	if(opt->mode==MODE_SINGLE||opt->mode==MODE_EVIL){
		if(opt->table!=NULL)return HUF_ERR_ARG;
		if(opt->mode==MODE_EVIL)status=decode_evil(input,output);
		else status=decode_packed(input,output);
	}else status=decode_table(opt->table?opt->table:default_table(opt->mode),input,output,opt->mode==MODE_TEXT);
	if(status==HUF_OK)printf("Wrote: %s\n",output);
	return status;
}

//【修改】原main里的编码流程移至encode.c，此处只解析参数并调度。
int main(int argc,char **argv)//【修改】统一处理菜单或命令行；退出码0为成功、1为比较不同、2为运行失败。
{
	if(argc==2&&strcmp(argv[1],"--help")==0){usage(stdout);return 0;}
	Options opt={0};
	char values[3][1024];
	if(!(argc==1?menu(&opt,values):arguments(&opt,argc,argv))){
		usage(stderr);return 2;
	}
	//【修改】移除evil预留拦截，由run调度实际编解码。
	HufStatus status=run(&opt);
	if(argc==1&&status==HUF_ERR_AMBIG){
		char choice[32];
		fputs("Both UTF-8 and GBK are possible.\n",stderr);
		if(read_value("Source encoding (utf8/gbk)","",choice,sizeof(choice))&&
		   source_encoding(choice,&opt.from)&&opt.from!=FROM_AUTO)status=run(&opt);
	}
	if(status==HUF_OK)return 0;
	if(status==HUF_DIFFERENT)return 1;
	fprintf(stderr,"%s\n",hufStatusStr(status));
	return 2;
}
