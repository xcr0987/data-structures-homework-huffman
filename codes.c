#include "huffman.h"

//【修改】下面的位操作、DFS和码表释放从第一次验收的table.c原样移入。

static uint8_t path[(GBK_SIZE+7)/8];//很小+全局就静态了

static void set_bit(uint8_t *bits,size_t i,unsigned int value)
{
	uint8_t mask=(uint8_t)(1u<<(7-i%8));
	if(value)bits[i/8]|=mask;
	else bits[i/8]&=(uint8_t)~mask;
}

//path[0,depth)是当前路径
static HufStatus dfs(const HuffNode *node,HuffmanCode *codes,size_t depth)
{
	if(node->ls==NULL&&node->rs==NULL){//叶子节点
		HuffmanCode *code=&codes[node->index];
		code->len=depth==0?1:depth;//单字符编码为0
		if(depth==0)set_bit(path,0,0);
		size_t bytes=(code->len+7)/8;
		code->bits=(uint8_t*)malloc(bytes);
		if(code->bits==NULL)return HUF_ERR_MEM;
		for(size_t i=0;i<bytes;i++){
			code->bits[i]=path[i];
		}
		//清除最后一个字节中残留的无效位
		if(code->len%8)code->bits[bytes-1]&=(uint8_t)(0xFFu<<(8-code->len%8));
		return HUF_OK;
	}
	set_bit(path,depth,0);
	HufStatus status=dfs(node->ls,codes,depth+1);
	if(status!=HUF_OK)return status;
	set_bit(path,depth,1);
	return dfs(node->rs,codes,depth+1);
}

void free_codes(HuffmanCode *codes,size_t n)
{
	if(codes==NULL)return;
	for(size_t i=0;i<n;i++)free(codes[i].bits);
	free(codes);
}

HufStatus make_codes(const HuffTree *tree,HuffmanCode **codes,size_t n)
{
	if(codes==NULL)return HUF_ERR_ARG;
	*codes=NULL;
	if(tree==NULL||tree->head==NULL||tree->siz==0)return HUF_ERR_ARG;
	HuffmanCode *table=(HuffmanCode*)calloc(n,sizeof(*table));
	if(table==NULL)return HUF_ERR_MEM;
	HufStatus status=dfs(tree->head,table,0);
	if(status!=HUF_OK){free_codes(table,n);return status;}
	*codes=table;
	return HUF_OK;
}

//【修改】规范码生成供single和evil共用，沿用传入的码长。
//【修改】规范码排序辅助项：仅在make_canonical内部使用，让排序不改变原symbols/codes的对应关系。
typedef struct{
	size_t index,len;//【修改】index记录原数组下标，len为该项码长；排序后仍按index回填结果。
	uint16_t ch;//【修改】字符值或DEFLATE符号编号，用于同码长时升序排列。
}CodeOrder;

static int cmp_code_order(const void *a,const void *b)//【修改】供qsort使用，按码长升序、同码长按字符值升序比较两个CodeOrder。
{
	const CodeOrder *x=a,*y=b;
	if(x->len!=y->len)return x->len<y->len?-1:1;
	return (x->ch>y->ch)-(x->ch<y->ch);
}

//码长不变；规范码按(码长,字符)递增，不把码字限制在一个整数中。
HufStatus make_canonical(const CharFreq *symbols,const HuffmanCode *lengths,
                         size_t n,HuffmanCode **result)//【修改】按symbols字符值及lengths码长生成n项规范码，索引保持不变；result由调用者用free_codes释放。
{
	*result=NULL;
	if(n==0)return HUF_OK;
	if(n>GBK_SIZE)return HUF_ERR_FORMAT;
	CodeOrder *order=malloc(n*sizeof(*order));
	HuffmanCode *codes=calloc(n,sizeof(*codes));
	if(order==NULL||codes==NULL){free(order);free(codes);return HUF_ERR_MEM;}
	HufStatus status=HUF_OK;
	for(size_t i=0;i<n;i++){
		if(lengths[i].len==0||lengths[i].len>(n>1?n-1:1)){
			status=HUF_ERR_FORMAT;goto done;
		}
		order[i]=(CodeOrder){i,lengths[i].len,symbols[i].ch};
	}
	qsort(order,n,sizeof(*order),cmp_code_order);
	for(size_t i=0;i<n;i++){
		HuffmanCode *code=&codes[order[i].index];
		code->len=order[i].len;
		code->bits=calloc((code->len+7)/8,1);
		if(code->bits==NULL){status=HUF_ERR_MEM;goto done;}
		if(i==0)continue;
		const HuffmanCode *prev=&codes[order[i-1].index];
		memcpy(code->bits,prev->bits,(prev->len+7)/8);
		size_t j=prev->len;
		while(j>0&&code_bit(code,j-1)){set_bit(code->bits,j-1,0);--j;}
		if(j==0){status=HUF_ERR_FORMAT;goto done;}
		set_bit(code->bits,j-1,1);
	}
done:
	free(order);
	if(status==HUF_OK)*result=codes;
	else free_codes(codes,n);
	return status;
}
