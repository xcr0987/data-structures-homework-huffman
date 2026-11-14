#include "huffman.h"

//【修改】新增evil：只写、只读RFC 1951动态块，不依赖zlib。
//窗口、块大小、查找次数固定；其余模式的树和位流格式不变。
#define WINDOW 32768
#define BLOCK 65535
#define HASH_SIZE 65536
#define MAX_CHAIN 512

//【修改】DEFLATE内部读/写位流状态：只借用文件流，调用方负责打开和关闭；同一实例只用于读或写。
typedef struct{
	FILE *fp;//【修改】压缩输入流或输出流，不是原始文本流。
	unsigned int byte,used;//【修改】写时byte缓存待写字节，used为已填位数；读时byte存右移后的剩余位，used为剩余位数。
	HufStatus status;//【修改】累计读写状态；出错后位操作停止，由外层统一处理。
}BitStream;
//【修改】LZ77中间项：每项表示一个原文字节，或一段可从历史复制的匹配；不持有动态内存。
typedef struct{
	uint16_t value,dist;//【修改】dist=0时value为字面量0~255；否则value为长度3~258，dist为回溯距离1~32768。
}Token;
//【修改】动态头中的码长游程项：用于压缩正文树和距离树的码长序列，不是正文Token。
typedef struct{
	uint8_t symbol,extra;//【修改】symbol为码长0~15或重复标记16/17/18；extra为重复次数减去对应基数后的附加值。
}Repeat;

static const unsigned int length_base[29]={
	3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const unsigned int length_extra[29]={
	0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const unsigned int dist_base[30]={
	1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const unsigned int dist_extra[30]={
	0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
static const unsigned int order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

//DEFLATE按字节低位到高位填充，普通数值的低位先写。
static void put_bits(BitStream *out,unsigned int value,unsigned int len)//【修改】把value的低len位按低位先写填入out；写错误记入out->status，不关闭流。
{
	for(unsigned int i=0;i<len&&out->status==HUF_OK;i++){
		out->byte|=((value>>i)&1u)<<out->used;
		if(++out->used==8){
			if(fputc((int)out->byte,out->fp)==EOF)out->status=HUF_ERR_IO;
			out->byte=out->used=0;
		}
	}
}

static unsigned int get_bits(BitStream *in,unsigned int len)//【修改】从in按低位先读取len位并返回数值；读错误记入in->status，已有缓存继续复用。
{
	unsigned int value=0;
	for(unsigned int i=0;i<len&&in->status==HUF_OK;i++){
		if(in->used==0){
			int byte=fgetc(in->fp);
			if(byte==EOF){in->status=ferror(in->fp)?HUF_ERR_IO:HUF_ERR_FORMAT;break;}
			in->byte=(unsigned int)byte;in->used=8;
		}
		value|=(in->byte&1u)<<i;
		in->byte>>=1;--in->used;
	}
	return value;
}

//哈夫曼码本身高位先写；复用项目已有的紧凑编码和code_bit。
static void put_code(BitStream *out,const HuffmanCode *code)//【修改】把code按码字从高位到低位写入out；字节仍遵守DEFLATE低位先填的规则。
{
	for(size_t i=0;i<code->len;i++)put_bits(out,code_bit(code,i),1);
}

//由符号编号顺序的码长恢复规范码。kind：0正文、1码长树、2距离树。
static HufStatus canonical_codes(const uint8_t *lens,size_t n,int kind,HuffmanCode **result)//【修改】将n项lens恢复为符号编号索引的规范码；kind为0正文/1码长/2距离，result由调用者释放。
{
	CharFreq symbols[288];
	HuffmanCode lengths[288]={0},*compact=NULL;
	size_t count=0;
	unsigned int counts[16]={0};
	*result=NULL;
	for(size_t i=0;i<n;i++){
		if(lens[i]>(kind==1?7:15))return HUF_ERR_FORMAT;
		if(lens[i]==0)continue;
		symbols[count]=(CharFreq){(uint16_t)i,0};
		lengths[count++].len=lens[i];
		++counts[lens[i]];
	}
	int left=1;
	for(size_t i=1;i<=15;i++){
		left=left*2-(int)counts[i];
		if(left<0)return HUF_ERR_FORMAT;//码字数量超过可用分支
	}
	if(count==0&&kind!=2)return HUF_ERR_FORMAT;
	if(count>0&&left!=0&&(kind==1||count!=1||counts[1]!=1))return HUF_ERR_FORMAT;
	HufStatus status=make_canonical(symbols,lengths,count,&compact);
	if(status!=HUF_OK)return status;
	HuffmanCode *codes=calloc(n,sizeof(*codes));
	if(codes==NULL){free_codes(compact,count);return HUF_ERR_MEM;}
	for(size_t i=0;i<count;i++)codes[symbols[i].ch]=compact[i];
	free(compact);//位数组已交给codes，不能在这里free_codes
	*result=codes;
	return HUF_OK;
}

static int cmp_freq(const void *a,const void *b)//【修改】供qsort使用，按频数降序、同频按符号编号升序比较两个CharFreq。
{
	const CharFreq *x=a,*y=b;
	if(x->freq!=y->freq)return x->freq>y->freq?-1:1;
	return (x->ch>y->ch)-(x->ch<y->ch);
}

//复用原双队列建树、DFS取码。超长时仅折半本轮权重，直到符合标准限长。
static HufStatus make_deflate_codes(const uint64_t *freq,size_t n,int kind,HuffmanCode **result)//【修改】复用原建树和DFS，由freq生成n项规范码；kind决定15/7位限长，超长折半权重重试。
{
	uint64_t weights[288];
	for(size_t i=0;i<n;i++)weights[i]=freq[i];
	size_t count=0;
	for(size_t i=0;i<n;i++)if(weights[i]>0)++count;
	if(count==0){weights[0]=1;count=1;}
	if(kind==1&&count==1){//码长树必须完整，补一个不会输出的符号
		for(size_t i=0;i<n;i++)if(weights[i]==0){weights[i]=1;break;}
	}
	for(;;){
		CharFreq sorted[288];
		uint8_t lens[288]={0};
		count=0;
		for(size_t i=0;i<n;i++)if(weights[i]>0)sorted[count++]=(CharFreq){(uint16_t)i,weights[i]};
		qsort(sorted,count,sizeof(*sorted),cmp_freq);
		HuffTree tree={0};
		HuffmanCode *codes=NULL;
		tree.nodes=calloc(count*2,sizeof(*tree.nodes));
		if(tree.nodes==NULL)return HUF_ERR_MEM;
		HufStatus status=make_tree(sorted,&tree,count);
		if(status==HUF_OK)status=make_codes(&tree,&codes,count);
		int too_long=0;
		if(status==HUF_OK){
			for(size_t i=0;i<count;i++){
				if(codes[i].len>(kind==1?7u:15u))too_long=1;
				else lens[sorted[i].ch]=(uint8_t)codes[i].len;
			}
		}
		free(tree.nodes);free_codes(codes,count);
		if(status!=HUF_OK)return status;
		if(!too_long)return canonical_codes(lens,n,kind,result);
		for(size_t i=0;i<n;i++)if(weights[i]>0)weights[i]=(weights[i]+1)/2;
	}
}

static unsigned int length_symbol(unsigned int len)//【修改】将匹配长度len映射为length_base的下标；实际DEFLATE长度符号为257加该下标。
{
	unsigned int i=0;
	while(i<28&&len>=length_base[i+1])++i;
	return i;
}

static unsigned int distance_symbol(unsigned int dist)//【修改】将匹配距离dist映射为dist_base的下标，也是DEFLATE距离符号。
{
	unsigned int i=0;
	while(i<29&&dist>=dist_base[i+1])++i;
	return i;
}

static unsigned int hash3(const uint8_t *data,size_t i)//【修改】计算data中从i开始的三个字节的哈希值；调用者保证还有至少三个字节。
{
	return ((unsigned int)data[i]*251u*251u+(unsigned int)data[i+1]*251u+data[i+2])%HASH_SIZE;
}

static void insert_position(const uint8_t *data,size_t i,size_t end,int *head,int *prev)//【修改】若[i,end)至少有三个字节，把位置i插入head/prev组成的哈希链。
{
	if(end-i<3)return;
	unsigned int h=hash3(data,i);
	prev[i]=head[h];head[h]=(int)i;
}

//候选按新到旧排列，优先保留同长度时距离较近的匹配。
static Token find_match(const uint8_t *data,size_t i,size_t end,const int *head,const int *prev)//【修改】在历史哈希链中查找data的[i,end)处最长匹配；最多512个候选，无匹配返回单字节Token。
{
	Token best={data[i],0};
	if(end-i<3)return best;
	size_t max=end-i<258?end-i:258;
	unsigned int len=2;
	int candidate=head[hash3(data,i)];
	for(size_t tries=0;candidate>=0&&tries<MAX_CHAIN;candidate=prev[candidate],tries++){
		size_t j=(size_t)candidate;
		if(i-j>WINDOW)break;
		if(data[j]!=data[i]||data[j+len]!=data[i+len])continue;
		size_t k=0;
		while(k<max&&data[j+k]==data[i+k])++k;
		if(k>len){
			len=(unsigned int)k;best=(Token){(uint16_t)k,(uint16_t)(i-j)};
			if(k==max)break;
		}
	}
	return best;
}

static size_t make_tokens(const uint8_t *data,size_t start,size_t end,Token *tokens,
                          int *head,int *prev,uint64_t *litfreq,uint64_t *distfreq)//【修改】将data的[start,end)转成tokens，使用前start字节作历史；填写两类频数，返回Token数量。
{
	for(size_t i=0;i<HASH_SIZE;i++)head[i]=-1;
	for(size_t i=0;i<start;i++)insert_position(data,i,end,head,prev);
	size_t count=0;
	for(size_t i=start;i<end;){
		Token token=find_match(data,i,end,head,prev);
		insert_position(data,i,end,head,prev);
		if(token.dist>0&&i+1<end){
			Token next=find_match(data,i+1,end,head,prev);
			if(next.dist>0&&next.value>token.value)token=(Token){data[i],0};
		}
		tokens[count++]=token;
		if(token.dist==0){++litfreq[token.value];++i;}
		else{
			++litfreq[257+length_symbol(token.value)];
			++distfreq[distance_symbol(token.dist)];
			for(size_t j=1;j<token.value;j++)insert_position(data,i+j,end,head,prev);
			i+=token.value;
		}
	}
	++litfreq[256];//块结束符
	return count;
}

//串联两棵树的码长，用16重复前一码长，17/18重复零码长。
static size_t make_repeats(const uint8_t *lens,size_t n,Repeat *repeats,uint64_t *freq)//【修改】将n项lens游程编码成repeats并累计freq；使用16/17/18标记，返回标记数量。
{
	size_t count=0;
	for(size_t i=0;i<n;){
		size_t end=i+1;
		while(end<n&&lens[end]==lens[i])++end;
		size_t run=end-i;
		if(lens[i]!=0){
			repeats[count++]=(Repeat){lens[i],0};++freq[lens[i]];--run;
			while(run>=3){
				size_t take=run>6?6:run;
				repeats[count++]=(Repeat){16,(uint8_t)(take-3)};++freq[16];run-=take;
			}
		}else{
			while(run>=3){
				unsigned int symbol=run>=11?18:17;
				size_t max=symbol==18?138:10;
				size_t take=run>max?max:run;
				repeats[count++]=(Repeat){(uint8_t)symbol,(uint8_t)(take-(symbol==18?11:3))};
				++freq[symbol];run-=take;
			}
		}
		while(run>0){repeats[count++]=(Repeat){lens[i],0};++freq[lens[i]];--run;}
		i=end;
	}
	return count;
}

static HufStatus write_block(BitStream *out,const Token *tokens,size_t count,
                              const uint64_t *litfreq,const uint64_t *distfreq,int final)//【修改】按两类频数生成三棵树，将count项tokens写成动态块；final表示末块，内部释放码表。
{
	HuffmanCode *lit=NULL,*dist=NULL,*cl=NULL;
	HufStatus status=make_deflate_codes(litfreq,286,0,&lit);
	if(status==HUF_OK)status=make_deflate_codes(distfreq,30,2,&dist);
	if(status!=HUF_OK)goto cleanup;
	size_t nl=286,nd=30,nc=19;
	while(nl>257&&lit[nl-1].len==0)--nl;
	while(nd>1&&dist[nd-1].len==0)--nd;
	uint8_t lens[316];
	for(size_t i=0;i<nl;i++)lens[i]=(uint8_t)lit[i].len;
	for(size_t i=0;i<nd;i++)lens[nl+i]=(uint8_t)dist[i].len;
	Repeat repeats[316];
	uint64_t clfreq[19]={0};
	size_t nr=make_repeats(lens,nl+nd,repeats,clfreq);
	status=make_deflate_codes(clfreq,19,1,&cl);
	if(status!=HUF_OK)goto cleanup;
	while(nc>4&&cl[order[nc-1]].len==0)--nc;
	put_bits(out,(unsigned int)final,1);put_bits(out,2,2);//BTYPE=10：动态块
	put_bits(out,(unsigned int)(nl-257),5);put_bits(out,(unsigned int)(nd-1),5);
	put_bits(out,(unsigned int)(nc-4),4);
	for(size_t i=0;i<nc;i++)put_bits(out,(unsigned int)cl[order[i]].len,3);
	for(size_t i=0;i<nr;i++){
		unsigned int symbol=repeats[i].symbol;
		put_code(out,&cl[symbol]);
		if(symbol>=16)put_bits(out,repeats[i].extra,symbol==16?2:symbol==17?3:7);
	}
	for(size_t i=0;i<count;i++){
		if(tokens[i].dist==0)put_code(out,&lit[tokens[i].value]);
		else{
			unsigned int l=length_symbol(tokens[i].value),d=distance_symbol(tokens[i].dist);
			put_code(out,&lit[257+l]);
			put_bits(out,tokens[i].value-length_base[l],length_extra[l]);
			put_code(out,&dist[d]);
			put_bits(out,tokens[i].dist-dist_base[d],dist_extra[d]);
		}
	}
	put_code(out,&lit[256]);
	status=out->status;
cleanup:
	free_codes(lit,286);free_codes(dist,30);free_codes(cl,19);
	return status;
}

HufStatus encode_evil(FILE *fp,const char *output)//【修改】重读fp并以LZ77加动态哈夫曼块写入output；跨块保留32KiB历史，fp由调用者关闭。
{
	uint8_t *data=malloc(WINDOW+BLOCK);
	Token *tokens=malloc(BLOCK*sizeof(*tokens));
	int *head=malloc(HASH_SIZE*sizeof(*head)),*prev=malloc((WINDOW+BLOCK)*sizeof(*prev));
	FILE *out=NULL;
	HufStatus status=HUF_OK;
	if(data==NULL||tokens==NULL||head==NULL||prev==NULL){status=HUF_ERR_MEM;goto cleanup;}
	if(fseek(fp,0,SEEK_SET)!=0){status=HUF_ERR_IO;goto cleanup;}
	out=fopen(output,"wb");
	if(out==NULL){status=HUF_ERR_IO;goto cleanup;}
	BitStream bits={out,0,0,HUF_OK};
	size_t history=0;
	for(;;){
		size_t got=fread(data+history,1,BLOCK,fp);
		if(ferror(fp)){status=HUF_ERR_IO;break;}
		int final=got<BLOCK;
		if(!final){//探测最后一块，避免长度恰好整块时额外写一个空块
			int next=fgetc(fp);
			if(next==EOF){
				if(ferror(fp)){status=HUF_ERR_IO;break;}
				final=1;
			}else if(ungetc(next,fp)==EOF){status=HUF_ERR_IO;break;}
		}
		uint64_t litfreq[286]={0},distfreq[30]={0};
		size_t count=make_tokens(data,history,history+got,tokens,head,prev,litfreq,distfreq);
		status=write_block(&bits,tokens,count,litfreq,distfreq,final);
		if(status!=HUF_OK||final)break;
		size_t end=history+got;
		history=end<WINDOW?end:WINDOW;
		for(size_t i=0;i<history;i++)data[i]=data[end-history+i];
	}
	if(status==HUF_OK&&bits.used>0&&fputc((int)bits.byte,out)==EOF)status=HUF_ERR_IO;
cleanup:
	free(data);free(tokens);free(head);free(prev);
	if(out!=NULL){
		if(fclose(out)!=0&&status==HUF_OK)status=HUF_ERR_IO;
		if(status!=HUF_OK)remove(output);
	}
	return status;
}

//以下为解码：复用DecodeTree，只将读位改成DEFLATE的顺序。
static HufStatus make_decode_tree(const uint8_t *lens,size_t n,int kind,DecodeTree *tree)//【修改】按lens的n项码长和kind恢复规范码，再插入tree；临时码表内部释放，tree由调用者释放。
{
	HuffmanCode *codes=NULL;
	HufStatus status=canonical_codes(lens,n,kind,&codes);
	if(status!=HUF_OK)return status;
	for(size_t i=0;i<n&&status==HUF_OK;i++){
		if(codes[i].len>0)status=add_decode_code(tree,(uint16_t)i,&codes[i]);
	}
	free_codes(codes,n);
	return status;
}

static unsigned int get_symbol(BitStream *in,const DecodeTree *tree)//【修改】从in逐位走tree并返回叶子符号；遇到坏分支把错误记入in->status。
{
	size_t node=0;
	if(tree->count==0){in->status=HUF_ERR_FORMAT;return 0;}
	while(!tree->nodes[node].leaf&&in->status==HUF_OK){
		unsigned int bit=get_bits(in,1);
		if(in->status!=HUF_OK)return 0;
		node=tree->nodes[node].child[bit];
		if(node==0){in->status=HUF_ERR_FORMAT;return 0;}
	}
	return tree->nodes[node].ch;
}

static HufStatus read_trees(BitStream *in,DecodeTree *lit,DecodeTree *dist)//【修改】读取动态块头和码长游程，恢复lit正文树及dist距离树；两棵树的节点由调用者释放。
{
	unsigned int nl=get_bits(in,5)+257,nd=get_bits(in,5)+1,nc=get_bits(in,4)+4;
	if(in->status!=HUF_OK)return in->status;
	if(nl>286)return HUF_ERR_FORMAT;
	uint8_t cllens[19]={0},lens[318]={0};
	for(size_t i=0;i<nc;i++)cllens[order[i]]=(uint8_t)get_bits(in,3);
	if(in->status!=HUF_OK)return in->status;
	DecodeTree cl={0};
	HufStatus status=make_decode_tree(cllens,19,1,&cl);
	if(status!=HUF_OK){free(cl.nodes);return status;}
	size_t count=0;
	while(count<nl+nd&&in->status==HUF_OK){
		unsigned int symbol=get_symbol(in,&cl),value=symbol,repeat=1;
		if(in->status!=HUF_OK)break;
		if(symbol==16){
			if(count==0){in->status=HUF_ERR_FORMAT;break;}
			value=lens[count-1];repeat=get_bits(in,2)+3;
		}else if(symbol>=17){
			value=0;repeat=get_bits(in,symbol==17?3:7)+(symbol==17?3:11);
		}
		if(in->status!=HUF_OK)break;
		if(repeat>nl+nd-count){in->status=HUF_ERR_FORMAT;break;}
		for(size_t i=0;i<repeat;i++)lens[count++]=(uint8_t)value;
	}
	free(cl.nodes);
	if(in->status!=HUF_OK)return in->status;
	if(lens[256]==0)return HUF_ERR_FORMAT;
	status=make_decode_tree(lens,nl,0,lit);
	if(status==HUF_OK)status=make_decode_tree(lens+nl,nd,2,dist);
	return status;
}

HufStatus decode_evil(const char *input,const char *output)//【修改】读取input的原始DEFLATE动态块，通过环形窗口恢复output；内部关闭文件并释放树和窗口。
{
	if(same_file(input,output))return HUF_ERR_ARG;
	FILE *in=fopen(input,"rb"),*out=NULL;
	uint8_t *window=NULL;
	HufStatus status=HUF_OK;
	if(in==NULL)return HUF_ERR_IO;
	out=fopen(output,"wb");
	if(out==NULL){status=HUF_ERR_IO;goto cleanup;}
	window=malloc(WINDOW);
	if(window==NULL){status=HUF_ERR_MEM;goto cleanup;}
	BitStream bits={in,0,0,HUF_OK};
	size_t pos=0,history=0;
	int final=0;
	while(!final&&status==HUF_OK){
		final=(int)get_bits(&bits,1);
		unsigned int type=get_bits(&bits,2);
		if(bits.status!=HUF_OK){status=bits.status;break;}
		if(type!=2){status=HUF_ERR_FORMAT;break;}//约定只支持动态块
		DecodeTree lit={0},dist={0};
		status=read_trees(&bits,&lit,&dist);
		while(status==HUF_OK){
			unsigned int symbol=get_symbol(&bits,&lit);
			if(bits.status!=HUF_OK){status=bits.status;break;}
			if(symbol==256)break;
			unsigned int len=1,distance=0;
			if(symbol>256){
				if(symbol>285){status=HUF_ERR_FORMAT;break;}
				unsigned int l=symbol-257;
				len=length_base[l]+get_bits(&bits,length_extra[l]);
				unsigned int d=get_symbol(&bits,&dist);
				if(bits.status!=HUF_OK){status=bits.status;break;}
				if(d>=30){status=HUF_ERR_FORMAT;break;}
				distance=dist_base[d]+get_bits(&bits,dist_extra[d]);
				if(bits.status!=HUF_OK){status=bits.status;break;}
				if(distance>history){status=HUF_ERR_FORMAT;break;}
			}
			for(size_t i=0;i<len;i++){
				uint8_t byte=distance==0?(uint8_t)symbol:window[(pos+WINDOW-distance)%WINDOW];
				if(fputc(byte,out)==EOF){status=HUF_ERR_IO;break;}
				window[pos]=byte;pos=(pos+1)%WINDOW;
				if(history<WINDOW)++history;
			}
		}
		free(lit.nodes);free(dist.nodes);
	}
	//末字节未用位按标准忽略，但不接受多出的完整字节。
	if(status==HUF_OK){
		if(fgetc(in)!=EOF)status=HUF_ERR_FORMAT;
		else if(ferror(in))status=HUF_ERR_IO;
	}
cleanup:
	free(window);
	if(fclose(in)!=0&&status==HUF_OK)status=HUF_ERR_IO;
	if(out!=NULL){
		if(fclose(out)!=0&&status==HUF_OK)status=HUF_ERR_IO;
		if(status!=HUF_OK)remove(output);
	}
	return status;
}
