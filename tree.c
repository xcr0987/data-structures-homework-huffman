#include "huffman.h"
//队列方法，因为是内部静态实现，所以没做状态码。
//但是做了pop和push的判空和判满退出。
static HuffNode *front(const Queue *q)
{
	return q->data[q->front];
}
static void pop(Queue *q)
{
	if(q->front==q->rear)return;
	q->front=(q->front+1)%q->MAXSIZE;
}
static void push(Queue *q,HuffNode *node)
{
	if(q->front==(q->rear+1)%q->MAXSIZE)return;
	q->data[q->rear]=node;
	q->rear=(q->rear+1)%q->MAXSIZE;
}
static int get_min(const Queue *x,const Queue *y)//弹1返回1 弹2返回0
{
	if(x->front==x->rear)return 0;
	if(y->front==y->rear)return 1;
	return front(x)->wei<=front(y)->wei;
}
/*
树的根节点为head，对每个树的节点：
index存sorted下标（如果不是叶子就不管）ls rs分别是左子树和右子树的节点指针（最后通过均空判叶子） wei为权重
两个升序队列，A队列存原本的数据
B队列存新建的父节点
对队列的每个元素：指向节点的指针
*/
HufStatus make_tree(const CharFreq *sorted,HuffTree *tree,size_t n)
{
	Queue queA,queB;
	queA.data=(HuffNode* *)calloc(n+5,sizeof(*queA.data));
	queB.data=(HuffNode* *)calloc(n+5,sizeof(*queB.data));
	if(queA.data==NULL||queB.data==NULL){free(queA.data);free(queB.data);return HUF_ERR_MEM;}
	queA.MAXSIZE=queB.MAXSIZE=n+1;
	queA.front=queA.rear=queB.front=queB.rear=0;

	for(size_t i=0;i<n;i++){
		tree->nodes[i].index=i;
		tree->nodes[i].wei=sorted[i].freq;
		++tree->siz;
	}//给sorted分配叶子节点
	for(size_t i=n;i>0;i--)push(&queA,tree->nodes+i-1);//倒序入队，保证权重升序

	for(size_t i=1;i<n;i++){
		HuffNode *left,*right;
		if(get_min(&queA,&queB)){
			left=front(&queA);
			pop(&queA);
		}else{
			left=front(&queB);
			pop(&queB);
		}

		if(get_min(&queA,&queB)){
			right=front(&queA);
			pop(&queA);
		}else{
			right=front(&queB);
			pop(&queB);
		}
		tree->nodes[tree->siz].ls=left;
		tree->nodes[tree->siz].rs=right;
		tree->nodes[tree->siz].wei=left->wei+right->wei;
		push(&queB,tree->nodes+tree->siz);
		++tree->siz;
	}
	
	if(n==1)//坑啊。
	tree->head=tree->nodes;
	else tree->head=front(&queB);

	free(queA.data);free(queB.data);
	return HUF_OK;
}
