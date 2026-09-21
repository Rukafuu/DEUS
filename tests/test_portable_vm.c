#include "deus.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned hunts, releases; } State;
static int mock_hunt(void *context,const char *url,size_t length,DeusHostDocument *document,char *error,size_t error_cap){static const char body[]="<h1>X</h1>";State *state=(State*)context;(void)error;(void)error_cap;if(length!=11u||memcmp(url,"deus://test",11u))return 0;state->hunts++;document->data=body;document->length=sizeof(body)-1u;document->status=200u;return 1;}
static void mock_release(void *context,DeusHostDocument *document){State *state=(State*)context;state->releases++;memset(document,0,sizeof(*document));}
static int output_is(FILE *file,const char *expected){char text[64]={0};size_t length;rewind(file);length=fread(text,1u,sizeof(text)-1u,file);return length==strlen(expected)&&!memcmp(text,expected,length);}
static int checked_arithmetic(void){
    DeusInstruction success_code[]={
        {DEUS_GENESIS,0u,0},
        {DEUS_CONST_I64,0u,7},{DEUS_CONST_I64,0u,3},{DEUS_ADD_I64,0u,0},{DEUS_EMIT,0u,0},
        {DEUS_CONST_I64,0u,7},{DEUS_CONST_I64,0u,3},{DEUS_SUB_I64,0u,0},{DEUS_EMIT,0u,0},
        {DEUS_CONST_I64,0u,7},{DEUS_CONST_I64,0u,3},{DEUS_MUL_I64,0u,0},{DEUS_EMIT,0u,0},
        {DEUS_CONST_I64,0u,-7},{DEUS_CONST_I64,0u,3},{DEUS_DIV_I64,0u,0},{DEUS_EMIT,0u,0},
        {DEUS_CONST_I64,0u,-7},{DEUS_CONST_I64,0u,3},{DEUS_MOD_I64,0u,0},{DEUS_EMIT,0u,0},
        {DEUS_HALT,0u,0}
    };
    DeusProgram success={NULL,0u,success_code,(uint32_t)(sizeof(success_code)/sizeof(success_code[0]))};
    struct { uint8_t opcode; int64_t left,right; } failures[]={
        {DEUS_ADD_I64,INT64_MAX,1},{DEUS_SUB_I64,INT64_MIN,1},{DEUS_MUL_I64,INT64_MIN,-1},
        {DEUS_DIV_I64,INT64_MIN,-1},{DEUS_MOD_I64,INT64_MIN,-1},{DEUS_DIV_I64,1,0},{DEUS_MOD_I64,1,0}
    };
    size_t index;
    FILE *output=tmpfile();
    if(!output)return 0;
    if(deus_vm_execute_program(&success,output)||!output_is(output,"10421-2-1")){fclose(output);return 0;}
    fclose(output);
    for(index=0u;index<sizeof(failures)/sizeof(failures[0]);index++){
        DeusInstruction code[]={
            {DEUS_GENESIS,0u,0},{DEUS_CONST_I64,0u,failures[index].left},
            {DEUS_CONST_I64,0u,failures[index].right},{failures[index].opcode,0u,0},{DEUS_HALT,0u,0}
        };
        DeusProgram program={NULL,0u,code,5u};
        output=tmpfile();
        if(!output)return 0;
        if(!deus_vm_execute_program(&program,output)){fclose(output);return 0;}
        fclose(output);
    }
    return 1;
}
int main(void){if(!checked_arithmetic()){fputs("portable checked I64 arithmetic failed\n",stderr);return 1;}DeusInstruction pure_code[]={{DEUS_GENESIS,0u,0},{DEUS_CONST_I64,0u,42},{DEUS_EMIT,0u,0},{DEUS_CONST_BOOL,1u,0},{DEUS_EMIT,0u,0},{DEUS_CONST_I64,0u,99},{DEUS_DEBUG,0u,0},{DEUS_HALT,0u,0}};DeusProgram pure={NULL,0u,pure_code,8u};DeusString strings[]={{"deus://test",11u},{"h1",2u}};DeusInstruction host_code[]={{DEUS_GENESIS,0u,0},{DEUS_FORK,0u,0},{DEUS_FORK,0u,0},{DEUS_JOIN,2u,0},{DEUS_REAP,1u,0},{DEUS_EMIT,0u,0},{DEUS_REAP,1u,0},{DEUS_EMIT,0u,0},{DEUS_HALT,0u,0}};DeusProgram retrieval={strings,2u,host_code,9u};State state={0};DeusHost host={DEUS_HOST_ABI_VERSION,DEUS_HOST_CAP_NETWORK,&state,mock_hunt,mock_release,NULL};FILE *output=tmpfile();if(!output)return 1;if(deus_vm_execute_program(&pure,output)||!output_is(output,"42true")){fclose(output);return 1;}fclose(output);output=tmpfile();if(!output)return 1;if(deus_vm_execute_program_with_host(&retrieval,output,&host)||!output_is(output,"X\nX\n")||state.hunts!=2u||state.releases!=2u){fclose(output);return 1;}fclose(output);puts("portable VM pure and synchronous host execution passed");return 0;}
