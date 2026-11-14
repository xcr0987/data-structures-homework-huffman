#include "huffman.h"

//【修改】新增表加裸位流、表加文本位流的解码入口。

HufStatus decode_table(const char *table,const char *input,const char *output,int text)//【修改】读取table建立解码树，将input恢复到output；text选择文本或裸二进制，内部管理文件和树。
{
	if(same_file(input,output)||same_file(table,output))return HUF_ERR_ARG;
	DecodeTree tree={0};
	uint64_t bits;
	HufStatus status=read_table(table,&tree,&bits);
	FILE *in=NULL,*out=NULL;
	if(status==HUF_OK){
		in=fopen(input,"rb");
		if(in==NULL)status=HUF_ERR_IO;
	}
	if(status==HUF_OK){
		out=fopen(output,"wb");
		if(out==NULL)status=HUF_ERR_IO;
	}
	if(status==HUF_OK)status=decode_stream(in,out,&tree,bits,text);
	if(in!=NULL&&fclose(in)!=0&&status==HUF_OK)status=HUF_ERR_IO;
	if(out!=NULL){
		if(fclose(out)!=0&&status==HUF_OK)status=HUF_ERR_IO;
		if(status!=HUF_OK)remove(output);
	}
	free(tree.nodes);
	return status;
}
