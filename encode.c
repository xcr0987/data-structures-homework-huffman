#include "huffman.h"

//【修改】从第一次验收的main.c移入编码流程，便于同一入口调度。

HufStatus encode_file(const char *input,const char *name,SourceEncoding from,HufMode mode)//【修改】按from读取input并完成统计、建树和编码；name为产物基名，mode决定附加压缩文件，内部统一释放资源。
{
	//【修改】根据模式和文件基名生成输出路径，避免覆盖原文。
	static const char *const files[]={"table.txt","encoded.txt","encoded.bin","packed.bin"};
	char paths[4][512],stage[4][512];
	//【修改】evil和single都在原有三个产物之外增加一个独立压缩文件。
	size_t count=mode==MODE_SINGLE||mode==MODE_EVIL?4:3;
	if(mode!=MODE_DELIVERY&&mode!=MODE_SINGLE&&mode!=MODE_EVIL)return HUF_ERR_ARG;
	if(strpbrk(name,"/\\:<>|\"?*")!=NULL)return HUF_ERR_ARG;
	for(size_t i=0;i<count;i++){
		const char *dot=*name?"." : "";
		//【修改】仅evil的第四个文件改为evil.deflate，仍沿用原基名规则。
		const char *file=mode==MODE_EVIL&&i==3?"evil.deflate":files[i];
		int a=snprintf(paths[i],sizeof(paths[i]),"encode-output/%s%s%s",name,dot,file);
		int b=snprintf(stage[i],sizeof(stage[i]),"decode-input/%s%s%s",name,dot,file);
		if(a<0||b<0||(size_t)a>=sizeof(paths[i])||(size_t)b>=sizeof(stage[i]))return HUF_ERR_ARG;
		if(same_file(input,paths[i])||same_file(input,stage[i]))return HUF_ERR_ARG;
	}
	FILE *fp=NULL;
	uint64_t *freq=NULL;
	CharFreq *sorted=NULL;
	HuffTree tree={0};
	HuffmanCode *codes=NULL;
	size_t n=0;

	//【修改】读取并转成GBK，错误状态统一交给main显示。
	HufStatus status=open_gbk(input,from,&fp);
	if(status!=HUF_OK)goto cleanup;

	freq=(uint64_t*)calloc(GBK_SIZE,sizeof(*freq));
	if(freq==NULL){status=HUF_ERR_MEM;goto cleanup;}
	status=make_freq(fp,freq);
	if(status!=HUF_OK)goto cleanup;

	status=sort_freq(freq,&sorted,&n);
	if(status!=HUF_OK)goto cleanup;

	if(n>0){
		tree.nodes=(HuffNode*)calloc(n<<1,sizeof(*tree.nodes));
		if(tree.nodes==NULL){status=HUF_ERR_MEM;goto cleanup;}
		status=make_tree(sorted,&tree,n);
		if(status!=HUF_OK)goto cleanup;

		status=make_codes(&tree,&codes,n);
		if(status!=HUF_OK)goto cleanup;
	}

	//【修改】创建两处交付目录，写表格式不变，只换输出路径。
	status=ensure_dir("encode-output");
	if(status==HUF_OK)status=ensure_dir("decode-input");
	if(status==HUF_OK)status=write_table(paths[0],sorted,n,codes);
	if(status!=HUF_OK)goto cleanup;

	//【修改】统计有效位数，供单文件头和结果提示使用。
	uint64_t bits=0;
	for(size_t i=0;i<n;i++){
		if(sorted[i].freq>(UINT64_MAX-bits)/codes[i].len){
			status=HUF_ERR_FORMAT;goto cleanup;
		}
		bits+=sorted[i].freq*codes[i].len;
	}

	//复用freq保存字符到下标的索引。
	for(size_t i=0;i<n;i++)freq[sorted[i].ch]=i+1;
	//【修改】文本编码仍调用原函数，只换输出路径。
	status=encode_text(fp,paths[1],freq,codes);
	if(status!=HUF_OK)goto cleanup;

	//【修改】调用方打开和关闭裸位流文件，写入时不加头。
	FILE *out=fopen(paths[2],"wb");
	if(out==NULL){status=HUF_ERR_IO;goto cleanup;}
	status=encode_bits(fp,out,freq,codes);
	if(fclose(out)!=0&&status==HUF_OK)status=HUF_ERR_IO;
	if(status!=HUF_OK){remove(paths[2]);goto cleanup;}
	//【修改】单文件模式另写packed.bin；交付型仍由表和裸位流解码。
	//【修改】evil只替换附加压缩文件的生成，前三个文件仍走原实现。
	if(mode==MODE_SINGLE||mode==MODE_EVIL){
		if(mode==MODE_EVIL)status=encode_evil(fp,paths[3]);
		else status=write_packed(fp,paths[3],sorted,n,codes,freq,bits);
		if(status==HUF_OK)status=copy_file(paths[3],stage[3]);
		if(status==HUF_OK)printf("Decode input: %s\n",stage[3]);
	}else{
		status=copy_file(paths[0],stage[0]);
		if(status==HUF_OK)status=copy_file(paths[2],stage[2]);
		if(status==HUF_OK)printf("Decode inputs: %s, %s\n",stage[0],stage[2]);
	}
	if(status==HUF_OK){
		for(size_t i=0;i<count;i++)printf("Wrote: %s\n",paths[i]);
		printf("Valid bits: %" PRIu64 "\n",bits);
	}
cleanup:
	free_codes(codes,n);
	free(freq);free(sorted);free(tree.nodes);
	//【修改】关闭失败也通过返回值交给main。
	if(fp!=NULL&&fclose(fp)!=0&&status==HUF_OK)status=HUF_ERR_IO;
	return status;
}
