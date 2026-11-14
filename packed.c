#include "huffman.h"

//【修改】新增单文件变长头、规范码长表和位流的读写。

static const unsigned char magic[]={'H','U','F',1};

static HufStatus put_number(FILE *fp,uint64_t value)//【修改】将value按ULEB128变长整数写入fp；每字节低7位存数值，最高位表示是否继续。
{
	do{
		unsigned int byte=(unsigned int)(value&0x7F);
		value>>=7;
		if(fputc(byte|(value!=0?0x80:0),fp)==EOF)return HUF_ERR_IO;
	}while(value!=0);
	return HUF_OK;
}

static HufStatus get_number(FILE *fp,uint64_t *value)//【修改】从fp读取一个ULEB128整数到value；截断或超过uint64_t范围返回格式错误。
{
	*value=0;
	for(unsigned int shift=0;shift<64;shift+=7){
		int byte=fgetc(fp);
		if(byte==EOF)return ferror(fp)?HUF_ERR_IO:HUF_ERR_FORMAT;
		if(shift==63&&byte>1)return HUF_ERR_FORMAT;
		*value|=(uint64_t)(byte&0x7F)<<shift;
		if((byte&0x80)==0)return HUF_OK;
	}
	return HUF_ERR_FORMAT;
}

HufStatus write_packed(FILE *fp,const char *output,const CharFreq *sorted,size_t n,
                       const HuffmanCode *codes,const uint64_t *index,uint64_t bits)//【修改】将fp写成output单文件：头部、字符码长表和规范码正文；bits为有效位数，内部释放临时码表。
{
	HuffmanCode *canonical=NULL;
	HufStatus status=make_canonical(sorted,codes,n,&canonical);
	if(status!=HUF_OK)return status;
	FILE *out=fopen(output,"wb");
	if(out==NULL){free_codes(canonical,n);return HUF_ERR_IO;}
	if(fwrite(magic,1,sizeof(magic),out)!=sizeof(magic))status=HUF_ERR_IO;
	if(status==HUF_OK)status=put_number(out,bits);
	if(status==HUF_OK)status=put_number(out,n);
	for(unsigned int ch=0;ch<GBK_SIZE&&status==HUF_OK;ch++){
		if(index[ch]==0)continue;
		if((ch>0xFF&&fputc(ch>>8,out)==EOF)||fputc(ch&0xFF,out)==EOF)status=HUF_ERR_IO;
		else status=put_number(out,canonical[index[ch]-1].len);
	}
	if(status==HUF_OK)status=encode_bits(fp,out,index,canonical);
	if(fclose(out)!=0&&status==HUF_OK)status=HUF_ERR_IO;
	if(status!=HUF_OK)remove(output);
	free_codes(canonical,n);
	return status;
}

static HufStatus read_packed(FILE *in,DecodeTree *tree,uint64_t *bits)//【修改】从in读取单文件头和码长表，生成tree并返回有效位数bits；流停在正文，tree.nodes由调用者释放。
{
	unsigned char header[sizeof(magic)];
	if(fread(header,1,sizeof(header),in)!=sizeof(header))
		return ferror(in)?HUF_ERR_IO:HUF_ERR_FORMAT;
	if(memcmp(header,magic,sizeof(magic))!=0)return HUF_ERR_FORMAT;
	uint64_t count;
	HufStatus status=get_number(in,bits);
	if(status==HUF_OK)status=get_number(in,&count);
	if(status!=HUF_OK)return status;
	if(count>GBK_SIZE||count>*bits||(*bits!=0&&count==0))return HUF_ERR_FORMAT;
	size_t n=(size_t)count;
	if(n==0)return HUF_OK;
	CharFreq *symbols=calloc(n,sizeof(*symbols));
	HuffmanCode *lengths=calloc(n,sizeof(*lengths)),*codes=NULL;
	if(symbols==NULL||lengths==NULL){status=HUF_ERR_MEM;goto done;}
	for(size_t i=0;i<n;i++){
		uint64_t len;
		status=readGBK(in,&symbols[i].ch);
		if(status!=HUF_OK){if(status!=HUF_ERR_IO)status=HUF_ERR_FORMAT;goto done;}
		if(i>0&&symbols[i-1].ch>=symbols[i].ch){status=HUF_ERR_FORMAT;goto done;}
		status=get_number(in,&len);
		if(status!=HUF_OK)goto done;
		if(len==0||len>(n>1?n-1:1)){status=HUF_ERR_FORMAT;goto done;}
		lengths[i].len=(size_t)len;
	}
	status=make_canonical(symbols,lengths,n,&codes);
	for(size_t i=0;i<n&&status==HUF_OK;i++)status=add_decode_code(tree,symbols[i].ch,&codes[i]);
	if(status==HUF_OK&&n>1&&tree->count!=2*n-1)status=HUF_ERR_FORMAT;
done:
	free(symbols);free(lengths);free_codes(codes,n);
	return status;
}

HufStatus decode_packed(const char *input,const char *output)//【修改】从input的内置码长表恢复规范码并解码到output；内部关闭文件并释放解码树。
{
	if(same_file(input,output))return HUF_ERR_ARG;
	FILE *in=fopen(input,"rb"),*out=NULL;
	if(in==NULL)return HUF_ERR_IO;
	DecodeTree tree={0};
	uint64_t bits;
	HufStatus status=read_packed(in,&tree,&bits);
	if(status==HUF_OK){
		out=fopen(output,"wb");
		if(out==NULL)status=HUF_ERR_IO;
	}
	if(status==HUF_OK)status=decode_stream(in,out,&tree,bits,0);
	if(fclose(in)!=0&&status==HUF_OK)status=HUF_ERR_IO;
	if(out!=NULL){
		if(fclose(out)!=0&&status==HUF_OK)status=HUF_ERR_IO;
		if(status!=HUF_OK)remove(output);
	}
	free(tree.nodes);
	return status;
}
