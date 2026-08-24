#include "deus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t MAGIC[8] = {'D','E','U','S','B',0,1,0};
static void put16(uint8_t *p, uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p, uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void put64(uint8_t *p, uint64_t v){put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));}
static uint16_t get16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t get64(const uint8_t *p){return (uint64_t)get32(p)|((uint64_t)get32(p+4)<<32);}
static uint32_t crc32(const uint8_t *p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(int k=0;k<8;k++)c=(c>>1)^(0xEDB88320u&((uint32_t)-(int32_t)(c&1)));}return ~c;}
static int operand_opcode(uint8_t op){return op==DEUS_OMNI||op==DEUS_HUNT||op==DEUS_REAP||op==DEUS_FORK||op==DEUS_JOIN||op==DEUS_LIMIT||op==DEUS_RETRY||op==DEUS_BACKOFF||op==DEUS_RATE||op==DEUS_CONST||op==DEUS_BIND||op==DEUS_LOAD||op==DEUS_CONST_BOOL||op==DEUS_JSON_PATH||op==DEUS_RECORD_SET||op==DEUS_RECORD_GET||op==DEUS_LIST_AT||op==DEUS_RECORD_GET_OPTIONAL||op==DEUS_LIST_AT_OPTIONAL;}
static int immediate_opcode(uint8_t op){return op==DEUS_CONST_I64;}
static int string_operand(uint8_t op){return op==DEUS_OMNI||op==DEUS_HUNT||op==DEUS_REAP||op==DEUS_FORK||op==DEUS_CONST||op==DEUS_JSON_PATH||op==DEUS_RECORD_SET||op==DEUS_RECORD_GET||op==DEUS_RECORD_GET_OPTIONAL;}
static int fail(char *e,size_t cap,const char *m){if(cap)snprintf(e,cap,"%s",m);return 0;}

void deus_program_free(DeusProgram *p){if(!p)return;for(uint32_t i=0;i<p->string_count;i++)free(p->strings[i].data);free(p->strings);free(p->code);memset(p,0,sizeof(*p));}

int deus_write_binary(const DeusProgram *p,const char *path,char *e,size_t cap){
    uint64_t slen=0,clen=0;for(uint32_t i=0;i<p->string_count;i++)slen+=4u+p->strings[i].len;
    for(uint32_t i=0;i<p->code_count;i++)clen+=1u+(operand_opcode(p->code[i].opcode)?4u:0u)+(immediate_opcode(p->code[i].opcode)?8u:0u);
    if(slen>DEUS_MAX_SECTION||clen>DEUS_MAX_SECTION)return fail(e,cap,"section exceeds 64 MiB");
    size_t total=DEUS_HEADER_SIZE+(size_t)slen+(size_t)clen;uint8_t *b=(uint8_t*)calloc(1,total);if(!b)return fail(e,cap,"out of memory");
    memcpy(b,MAGIC,8);put16(b+8,DEUS_ABI_VERSION);put32(b+12,p->string_count);put32(b+16,DEUS_HEADER_SIZE);put32(b+20,(uint32_t)slen);put32(b+24,DEUS_HEADER_SIZE+(uint32_t)slen);put32(b+28,(uint32_t)clen);
    size_t at=DEUS_HEADER_SIZE;for(uint32_t i=0;i<p->string_count;i++){put32(b+at,p->strings[i].len);at+=4;memcpy(b+at,p->strings[i].data,p->strings[i].len);at+=p->strings[i].len;}
    for(uint32_t i=0;i<p->code_count;i++){b[at++]=p->code[i].opcode;if(operand_opcode(p->code[i].opcode)){if(string_operand(p->code[i].opcode)&&p->code[i].operand>=p->string_count){free(b);return fail(e,cap,"bad string operand");}if((p->code[i].opcode==DEUS_BIND||p->code[i].opcode==DEUS_LOAD)&&p->code[i].operand>=DEUS_MAX_LOCALS){free(b);return fail(e,cap,"bad local operand");}if(p->code[i].opcode==DEUS_CONST_BOOL&&p->code[i].operand>1u){free(b);return fail(e,cap,"bad boolean operand");}put32(b+at,p->code[i].operand);at+=4;}else if(immediate_opcode(p->code[i].opcode)){put64(b+at,(uint64_t)p->code[i].immediate);at+=8;}}
    put32(b+32,crc32(b+DEUS_HEADER_SIZE,total-DEUS_HEADER_SIZE));FILE *f=fopen(path,"wb");if(!f){free(b);return fail(e,cap,"cannot create output");}int ok=fwrite(b,1,total,f)==total&&fclose(f)==0;free(b);return ok?1:fail(e,cap,"binary write failed");
}

int deus_read_binary(const char *path,DeusProgram *out,char *e,size_t cap){
    memset(out,0,sizeof(*out));FILE *f=fopen(path,"rb");if(!f)return fail(e,cap,"cannot open bytecode");if(fseek(f,0,SEEK_END)||ftell(f)<0){fclose(f);return fail(e,cap,"cannot size bytecode");}long z=ftell(f);rewind(f);if(z<(long)DEUS_HEADER_SIZE){fclose(f);return fail(e,cap,"truncated header");}uint8_t *b=(uint8_t*)malloc((size_t)z);if(!b){fclose(f);return fail(e,cap,"out of memory");}if(fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return fail(e,cap,"read failed");}fclose(f);
    if(memcmp(b,MAGIC,8)){free(b);return fail(e,cap,"invalid DEUSB magic");}if(get16(b+8)!=DEUS_ABI_VERSION){free(b);return fail(e,cap,"unsupported ABI");}
    uint32_t count=get32(b+12),so=get32(b+16),sl=get32(b+20),co=get32(b+24),cl=get32(b+28);if(count>DEUS_MAX_STRINGS||sl>DEUS_MAX_SECTION||cl>DEUS_MAX_SECTION||so!=DEUS_HEADER_SIZE||so+sl!=co||(uint64_t)co+cl!=(uint64_t)z){free(b);return fail(e,cap,"invalid section layout");}if(crc32(b+so,(size_t)sl+cl)!=get32(b+32)){free(b);return fail(e,cap,"CRC32 mismatch");}
    out->strings=(DeusString*)calloc(count,sizeof(*out->strings));out->string_count=count;size_t at=so,end=so+sl;if(count&&!out->strings){free(b);return fail(e,cap,"out of memory");}
    for(uint32_t i=0;i<count;i++){if(at+4>end)goto corrupt;uint32_t n=get32(b+at);at+=4;if(at+n>end)goto corrupt;out->strings[i].data=(char*)malloc((size_t)n+1);if(!out->strings[i].data)goto oom;memcpy(out->strings[i].data,b+at,n);out->strings[i].data[n]=0;out->strings[i].len=n;at+=n;}if(at!=end)goto corrupt;
    at=co;while(at<co+cl){uint8_t op=b[at++];if(op<DEUS_OMNI||op>DEUS_TO_BOOL)goto corrupt;uint32_t arg=0;int64_t immediate=0;if(operand_opcode(op)){if(at+4>co+cl)goto corrupt;arg=get32(b+at);at+=4;if((string_operand(op)&&arg>=count)||((op==DEUS_BIND||op==DEUS_LOAD)&&arg>=DEUS_MAX_LOCALS)||(op==DEUS_CONST_BOOL&&arg>1u))goto corrupt;}else if(immediate_opcode(op)){if(at+8>co+cl)goto corrupt;immediate=(int64_t)get64(b+at);at+=8;}DeusInstruction *next=(DeusInstruction*)realloc(out->code,(out->code_count+1u)*sizeof(*next));if(!next)goto oom;out->code=next;out->code[out->code_count++]=(DeusInstruction){op,arg,immediate};}free(b);return 1;
corrupt: free(b);deus_program_free(out);return fail(e,cap,"malformed bytecode");
oom: free(b);deus_program_free(out);return fail(e,cap,"out of memory");
}
