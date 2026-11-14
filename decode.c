#include "huffman.h"

//【修改】新增由码字建立解码树和逐位解码，不读取原文。

static HufStatus new_node(DecodeTree *tree,size_t *id)//【修改】给tree追加一个清零节点并把下标写入id，容量不足时扩容；节点数组仍归tree持有。
{
	if(tree->count==tree->capacity){
		if(tree->capacity>SIZE_MAX/2/sizeof(*tree->nodes))return HUF_ERR_MEM;
		size_t capacity=tree->capacity==0?16:tree->capacity*2;
		DecodeNode *nodes=realloc(tree->nodes,capacity*sizeof(*nodes));
		if(nodes==NULL)return HUF_ERR_MEM;
		tree->nodes=nodes;tree->capacity=capacity;
	}
	*id=tree->count++;
	tree->nodes[*id]=(DecodeNode){0};
	return HUF_OK;
}

HufStatus add_decode_code(DecodeTree *tree,uint16_t ch,const HuffmanCode *code)//【修改】把ch的code逐位插入tree；已有前缀或码字冲突时返回格式错误，节点数组由调用者释放。
{
	size_t node=0;
	if(code->len==0)return HUF_ERR_FORMAT;
	if(tree->count==0){
		HufStatus status=new_node(tree,&node);
		if(status!=HUF_OK)return status;
	}
	for(size_t i=0;i<code->len;i++){
		if(tree->nodes[node].leaf)return HUF_ERR_FORMAT;
		unsigned int bit=code_bit(code,i);
		size_t next=tree->nodes[node].child[bit];
		if(next==0){
			HufStatus status=new_node(tree,&next);
			if(status!=HUF_OK)return status;
			tree->nodes[node].child[bit]=next;
		}
		node=next;
	}
	DecodeNode *leaf=&tree->nodes[node];
	if(leaf->leaf||leaf->child[0]||leaf->child[1])return HUF_ERR_FORMAT;
	leaf->ch=ch;leaf->leaf=1;
	return HUF_OK;
}

HufStatus decode_stream(FILE *in,FILE *out,const DecodeTree *tree,uint64_t bits,int text)//【修改】用tree将in的bits个有效位还原到out；text非0读ASCII位，否则读高位优先正文；不关闭流。
{
	size_t node=0;
	int byte=0;
	for(uint64_t i=0;i<bits;i++){
		unsigned int bit;
		if(text){
			byte=fgetc(in);
			if(byte==EOF)return ferror(in)?HUF_ERR_IO:HUF_ERR_FORMAT;
			if(byte!='0'&&byte!='1')return HUF_ERR_FORMAT;
			bit=(unsigned int)(byte-'0');
		}else{
			HufStatus status=read_bit(in,i,&byte,&bit);
			if(status!=HUF_OK)return status;
		}
		if(tree->count==0)return HUF_ERR_FORMAT;
		node=tree->nodes[node].child[bit];
		if(node==0)return HUF_ERR_FORMAT;
		if(tree->nodes[node].leaf){
			uint16_t ch=tree->nodes[node].ch;
			if(ch>0xFF&&fputc(ch>>8,out)==EOF)return HUF_ERR_IO;
			if(fputc(ch&0xFF,out)==EOF)return HUF_ERR_IO;
			node=0;
		}
	}
	if(node!=0)return HUF_ERR_FORMAT;
	if(!text&&bits%8!=0&&(byte&((1u<<(8-bits%8))-1)))return HUF_ERR_FORMAT;
	if(fgetc(in)!=EOF)return HUF_ERR_FORMAT;
	return ferror(in)?HUF_ERR_IO:HUF_OK;
}
