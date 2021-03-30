#include "cheeze_block.h"
#include "queue.h"
#include "interface.h"
#include "threading.h"
#include "../bench/bench.h"
#include "vectored_interface.h"
#include "../include/utils/crc32.h"
#include <pthread.h>

#define PHYS_ADDR 0x3280000000
#define TOTAL_SIZE (1024L * 1024L * 1024L)

pthread_t t_id;
extern master_processor mp;

int chrfd;

bool cheeze_end_req(request *const req);
void *ack_to_dev(void*);
queue *ack_queue;
char *null_value;
#ifdef CHECKINGDATA
uint32_t* CRCMAP;
#endif

void init_cheeze(){
	chrfd = open("/dev/cheeze_chr", O_RDWR);
	if (chrfd < 0) {
		perror("Failed to open /dev/cheeze_chr");
		abort();
		return;
	}

	null_value=(char*)malloc(PAGESIZE);
	memset(null_value,0,PAGESIZE);
	q_init(&ack_queue, 128);
#ifdef CHECKINGDATA
	CRCMAP=(uint32_t*)malloc(sizeof(uint32_t) * RANGE);
	memset(CRCMAP, 0, sizeof(uint32_t) *RANGE);
#endif
	pthread_create(&t_id, NULL, &ack_to_dev, NULL);
}

inline void error_check(cheeze_req *creq){
	if(unlikely(creq->len%LPAGESIZE)){
		printf("size not align %s:%d\n", __FILE__, __LINE__);
		abort();
	}
	/*
	if(unlikely(creq->pos%LPAGESIZE)){
		printf("pos not align %s:%d\n", __FILE__, __LINE__);
		abort();
	}*/
}

inline FSTYPE decode_type(int op){
	switch(op){
		case REQ_OP_READ: return FS_GET_T;
		case REQ_OP_WRITE: return FS_SET_T;
		case REQ_OP_FLUSH: return FS_FLUSH_T;
		case REQ_OP_DISCARD: return FS_DELETE_T;
		default:
			printf("not valid type!\n");
			abort();
	}
	return 1;
}

const char *type_to_str(uint32_t type){
	switch(type){
		case FS_GET_T: return "FS_GET_T";
		case FS_SET_T: return "FS_SET_T";
		case FS_FLUSH_T: return "FS_FLUSH_T";
		case FS_DELETE_T: return "FS_DELETE_T";
	}
	return NULL;
}

vec_request *get_vectored_request(){
	static bool isstart=false;
	vec_request *res=(vec_request *)calloc(1, sizeof(vec_request));
	cheeze_req *creq=(cheeze_req*)malloc(sizeof(cheeze_req));
	
	if(!isstart){
		isstart=true;
		printf("now waiting req!!\n");
		fsync(1);
		fsync(2);
	}
	ssize_t r=read(chrfd, creq, sizeof(cheeze_req));
	if(r<0){
		free(res);
		return NULL;
	}

	error_check(creq);

	res->origin_req=(void*)creq;
	res->size=creq->len/LPAGESIZE;
	res->req_array=(request*)calloc(res->size,sizeof(request));
	res->end_req=NULL;
	res->mark=0;

	FSTYPE type=decode_type(creq->op);
	if(type!=FS_GET_T && type!=FS_SET_T){
		res->buf=NULL;
		r=write(chrfd, creq, sizeof(cheeze_req));
		if(r<0){
			printf("ack error!\n");
			free(res);
			return NULL;
		}
	}
	else{
		res->buf=(char*)malloc(creq->len);
		creq->buf=res->buf;
		if(type==FS_SET_T){
			r=write(chrfd, creq, sizeof(cheeze_req));
			if(r<0){
				printf("ack error!\n");
				free(res);
				return NULL;
			}
		}
	}


	if(res->size > QSIZE){
		printf("----------------number of reqs is over %u < %u\n", QSIZE, res->size);
		abort();
		return NULL;
	}

	for(uint32_t i=0; i<res->size; i++){
		request *temp=&res->req_array[i];
		temp->parents=res;
		temp->type=type;
		temp->end_req=cheeze_end_req;
		temp->isAsync=ASYNC;
		temp->seq=i;
		switch(type){
			case FS_GET_T:
				temp->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
				break;
			case FS_SET_T:
				temp->value=inf_get_valueset(&res->buf[LPAGESIZE*i],FS_MALLOC_W,LPAGESIZE);
				break;	
			case FS_FLUSH_T:
				break;
			case FS_DELETE_T:
				break;
			default:
				printf("error type!\n");
				abort();
				break;
		}
		temp->key=creq->pos+i;

#ifdef CHECKINGDATA
		if(temp->type==FS_SET_T){
			CRCMAP[temp->key]=crc32(&res->buf[LPAGESIZE*i],LPAGESIZE);	
		}
		else if(temp->type==FS_DELETE_T){
			CRCMAP[temp->key]=crc32(null_value, LPAGESIZE);
		}
#endif
		DPRINTF("REQ-TYPE:%s INFO(%d:%d) LBA: %u\n", type_to_str(temp->type),creq->id, i, temp->key);
	}

	return res;
}

bool cheeze_end_req(request *const req){
	vectored_request *preq=req->parents;
	switch(req->type){
		case FS_NOTFOUND_T:
			bench_reap_data(req, mp.li);
			DPRINTF("%u not found!\n",req->key);
#ifdef CHECKINGDATA
			if(CRCMAP[req->key]){
				printf("\n");
				printf("\t\tcrc checking error in key:%u\n", req->key);	
				printf("\n");		
			}
#endif
			memcpy(&preq->buf[req->seq*LPAGESIZE], null_value,LPAGESIZE);
			inf_free_valueset(req->value,FS_MALLOC_R);
			break;
		case FS_GET_T:
			bench_reap_data(req, mp.li);
			if(req->value){
				memcpy(&preq->buf[req->seq*LPAGESIZE], req->value->value,LPAGESIZE);
#ifdef CHECKINGDATA
				if(CRCMAP[req->key]!=crc32(&preq->buf[req->seq*LPAGESIZE], LPAGESIZE)){
					printf("\n");
					printf("\t\tcrc checking error in key:%u\n", req->key);	
					printf("\n");
				}	
#endif
				inf_free_valueset(req->value,FS_MALLOC_R);
			}
			break;
		case FS_SET_T:
			bench_reap_data(req, mp.li);
			if(req->value) inf_free_valueset(req->value, FS_MALLOC_W);
			break;
		case FS_FLUSH_T:
		case FS_DELETE_T:
			break;
		default:
			abort();
	}
	preq->done_cnt++;
	release_each_req(req);

	if(preq->size==preq->done_cnt){
		if(req->type!=FS_GET_T && req->type!=FS_NOTFOUND_T){
			free(preq->buf);
			free(preq->origin_req);
			free(preq->req_array);
			free(preq);
		}
		else{
			while(!q_enqueue((void*)preq, ack_queue)){}
		}
	}

	return true;
}


void *ack_to_dev(void* a){
	vec_request *vec=NULL;
	ssize_t r;
	while(1){
		if(!(vec=(vec_request*)q_dequeue(ack_queue))) continue;

#if defined(DEBUG) || defined(CHECKINGDATA)
		cheeze_req *creq=(cheeze_req*)vec->origin_req;
#endif

#ifdef CHECKINGDATA
		for(uint32_t i=0; i<creq->len/LPAGESIZE; i++){
			if(CRCMAP[creq->pos+i] && CRCMAP[creq->pos+i]!=crc32((char*)&creq->buf[LPAGESIZE*i], LPAGESIZE)){
					printf("\n");
					printf("\t\tcrc checking error in key:%u, it maybe copy error!\n", creq->pos);	
					printf("\n");
			}
		}
#endif

		r=write(chrfd, vec->origin_req, sizeof(cheeze_req));

		DPRINTF("[DONE] REQ INFO(%d) LBA: %u ~ %u\n", creq->id, creq->pos, creq->pos+creq->len/LPAGESIZE-1);
		if(r<0){
			break;
		}
		free(vec->buf);
		free(vec->origin_req);
		free(vec->req_array);
		free(vec);
	}
	return NULL;
}

void free_cheeze(){
#ifdef CHECKINGDATA
	free(CRCMAP);
#endif
	return;
}
