#include "map.h"
#include "gc.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <execinfo.h>

extern algorithm page_ftl;


void handler(char *caller) {
  void *array[10];
  size_t size;
  printf("Stack Trace Start for %s\n",caller);
  size = backtrace(array, 10);
  backtrace_symbols_fd(array, size, 2);
  printf("Stack Trace End\n");
}

void page_map_create(){
	pm_body *p=(pm_body*)calloc(sizeof(pm_body),1);
	p->mapping=(uint32_t*)malloc(sizeof(uint32_t)*_NOP*L2PGAP);
	for(int i=0;i<_NOP*L2PGAP; i++){
		p->mapping[i]=UINT_MAX;
	}
	
	p->reserve=page_ftl.bm->get_segment(page_ftl.bm,true); //reserve for GC
	p->active=page_ftl.bm->get_segment(page_ftl.bm,false); //now active block for inserted request.
	page_ftl.algo_body=p; //you can assign your data structure in algorithm structure
}

uint32_t page_map_assign(KEYT* lba){
	uint32_t res=0;

	res=get_ppa(lba);
	pm_body *p=(pm_body*)page_ftl.algo_body;
	for(uint32_t i=0; i<L2PGAP; i++){
		KEYT t_lba=lba[i];
		if(p->mapping[t_lba]!=UINT_MAX){
			/*when mapping was updated, the old one is checked as a inavlid*/
			
			invalidate_ppa(p->mapping[t_lba]);
		}
		else{
			p->valid_lba_num++;
		}
		/*mapping update*/
		p->mapping[t_lba]=res*L2PGAP+i;
	}

	return res;
}

uint32_t page_map_pick(uint32_t lba){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	res=p->mapping[lba];
	return res;
}


uint32_t page_map_trim(uint32_t lba){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	res=p->mapping[lba];
	if(res==UINT32_MAX){
		return 0;
	}
	else{
		p->valid_lba_num--;
		invalidate_ppa(res);
		p->mapping[lba]=UINT32_MAX;
		return 1;
	}
}

uint32_t page_map_gc_update(KEYT *lba, uint32_t idx){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;

	/*when the gc phase, It should get a page from the reserved block*/
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->reserve);
	uint32_t old_ppa, new_ppa;
	for(uint32_t i=0; i<idx; i++){
		KEYT t_lba=lba[i];

		if(p->mapping[t_lba]!=UINT_MAX){
			/*when mapping was updated, the old one is checked as a inavlid*/
			//invalidate_ppa(p->mapping[t_lba]);
		}
		/*mapping update*/
		old_ppa=p->mapping[t_lba];
		p->mapping[t_lba]=res*L2PGAP+i;
	}

	for(uint32_t i=idx; i<L2PGAP; i++){
		invalidate_ppa(res*L2PGAP+idx);
	}

	return res;
}

void page_map_free(){
	pm_body *p=(pm_body*)page_ftl.algo_body;
	free(p->mapping);
}


