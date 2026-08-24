#include "deus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char*load(const char*path,size_t*n){FILE*f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long z=ftell(f);rewind(f);if(z<0){fclose(f);return NULL;}char*b=(char*)malloc((size_t)z+1);if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}fclose(f);b[z]=0;*n=(size_t)z;return b;}
static void usage(void){fprintf(stderr,"usage: deusc check <file.deus>\n       deusc build <file.deus> -o <file.deusb>\n");}
int main(int argc,char**argv){if(argc<3){usage();return 64;}int build=!strcmp(argv[1],"build"),check=!strcmp(argv[1],"check");if(!build&&!check){usage();return 64;}const char*outpath=NULL;if(build){if(argc!=5||strcmp(argv[3],"-o")){usage();return 64;}outpath=argv[4];}size_t n;char*source=load(argv[2],&n);if(!source){fprintf(stderr,"deusc: cannot read %s\n",argv[2]);return 66;}DeusProgram p;DeusDiagnostic d={0};if(!deus_parse_source(source,n,&p,&d)){fprintf(stderr,"%s:%u:%u: error: %s\n",argv[2],d.line,d.column,d.message);free(source);return 65;}free(source);if(build){char e[192];if(!deus_write_binary(&p,outpath,e,sizeof(e))){fprintf(stderr,"deusc: %s\n",e);deus_program_free(&p);return 74;}printf("forged %s (%u instructions, %u strings)\n",outpath,p.code_count,p.string_count);}else printf("%s: clean\n",argv[2]);deus_program_free(&p);return 0;}
