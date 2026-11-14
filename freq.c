#include "huffman.h"
/*
GBK编码：
单字节合法范围 0x00-0x7F
0x80：Windows CP936编码中用于表示欧元符号€的扩展，在这里进行了兼容
双字节第一字节 0x81-0xFE
双字节第二字节 0x40-0xFE
*/
HufStatus readGBK(FILE *fp,uint16_t *ch)
{
	if(fp==NULL||ch==NULL)return HUF_ERR_ARG;
	int a=fgetc(fp);
	if(a==EOF)return ferror(fp)?HUF_ERR_IO:HUF_EOF;
	if(a<=0x7F||a==0x80){//单字符
		*ch=(uint16_t)a;
		return HUF_OK;
	}
	if(a<0x81||a>0xFE)return HUF_ERR_GBK;
	int b=fgetc(fp);
	if(b==EOF)return ferror(fp)?HUF_ERR_IO:HUF_ERR_GBK;
	if(b<0x40||b>0xFE||b==0x7f)return HUF_ERR_GBK;
	//GBK 双字节的第二字节合法范围中专门排除了0x7F。它在ASCII中是DEL控制字符。
	*ch=(uint16_t)(((unsigned int)a<<8)|(unsigned int)b);//汉字
	return HUF_OK;
}
HufStatus make_freq(FILE *fp,uint64_t freq[GBK_SIZE])
{
	if(fp==NULL||freq==NULL)return HUF_ERR_ARG;//每个函数进入之后检查其参数是否正确
	uint16_t ch;
	HufStatus status=HUF_OK;
	while((status=readGBK(fp,&ch))==HUF_OK){
		++freq[ch];
	}
	return status==HUF_EOF?HUF_OK:status;
}

static int cmp_freq(const CharFreq *x,const CharFreq *y)
{
	return x->freq==y->freq?x->ch<y->ch:x->freq>y->freq;//第一关键字频数降序 第二关键字字符序号 排序
}
static void merge_sort(CharFreq *l,CharFreq *r,CharFreq *tmp)//[l,r)区间的归并排序
{
	size_t len=r-l;
	if(len<=1)return;
	CharFreq *mid=l+(len>>1);
	merge_sort(l,mid,tmp),merge_sort(mid,r,tmp);
	CharFreq *i=l,*j=mid;
	size_t k=0;
	while(i<mid&&j<r){
		if(cmp_freq(i,j))tmp[k++]=*i++;
		else tmp[k++]=*j++;
	}
    while(i<mid)tmp[k++]=*i++;
    while(j<r)tmp[k++]=*j++;
	for(size_t m=0;m<len;m++)l[m]=tmp[m];
}

HufStatus sort_freq(const uint64_t freq[GBK_SIZE],CharFreq **sorted,size_t *n)
{
	if(freq==NULL||sorted==NULL||n==NULL)return HUF_ERR_ARG;
	*sorted=NULL;
	*n=0;
	size_t cnt=0;
	
	//统计有元素个数
	for(size_t i=0;i<GBK_SIZE;i++){
		if(freq[i])cnt++;
	}
	if(cnt==0)return HUF_OK;
	
	//搬到arr里用来排序
	CharFreq *arr=(CharFreq*)malloc(cnt*sizeof(*arr));
	CharFreq *tmp=(CharFreq*)malloc(cnt*sizeof(*tmp));
	if(arr==NULL||tmp==NULL){free(arr);free(tmp);return HUF_ERR_MEM;}
	size_t t=0;
	for(size_t i=0;i<GBK_SIZE;i++){
		if(freq[i]){
			arr[t].ch=(uint16_t)i;
			arr[t].freq=freq[i];
			t++;
		}
	}
	merge_sort(arr,arr+cnt,tmp);

	free(tmp);
	*sorted=arr;
	*n=cnt;
	return HUF_OK;
}