#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <getopt.h>
#include <signal.h>
#include "../include/lsm_settings.h"
#include "../include/FS.h"
#include "../include/settings.h"
#include "../include/types.h"
#include "../bench/bench.h"
#include "interface.h"
#include "vectored_interface.h"
#include "../include/utils/kvssd.h"
extern int req_cnt_test;
extern uint64_t dm_intr_cnt;
extern int LOCALITY;
extern float TARGETRATIO;
extern int KEYLENGTH;
extern int VALUESIZE;
extern uint32_t INPUTREQNUM;
extern master *_master;
extern bool force_write_start;
extern int seq_padding_opt;
MeasureTime write_opt_time[11];
extern master_processor mp;
extern uint64_t cumulative_type_cnt[LREQ_TYPE_NUM];
void log_lower_print(int sig){
    printf("-------------lower print!!!!-------------\n");
    inf_lower_log_print();
    printf("-------------lower print end-------------\n");
}
int main(int argc,char* argv[]){
	//int temp_cnt=bench_set_params(argc,argv,temp_argv);

    struct sigaction sa2;
    sa2.sa_handler = log_lower_print;
    sigaction(SIGUSR1, &sa2, NULL);

	//while(1){}

	inf_init(0,0,argc,argv);
	bench_init();
	bench_vectored_configure();
	//bench_add(VECTOREDRSET,0,RANGE,RANGE);
	//bench_add(VECTOREDSSET,0,RANGE,RANGE);
	//bench_add(VECTOREDRSET,0,RANGE,RANGE*4);
	//bench_add(VECTOREDRGET,0,RANGE/100*99,RANGE/100*99);
	printf("range: %lu!\n",RANGE);
	bench_add(VECTOREDRW,0,RANGE,RANGE*2);


	char *value;
	uint32_t mark;
	while((value=get_vectored_bench(&mark))){
		inf_vector_make_req(value, bench_transaction_end_req, mark);
	}

	force_write_start=true;
	
	printf("bench finish\n");
	while(!bench_is_finish()){
#ifdef LEAKCHECK
		sleep(1);
#endif
	}

	inf_free();
	bench_custom_print(write_opt_time,11);
	return 0;
}
