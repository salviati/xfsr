#include "xfsr.h"

#include <string.h>
#include <ctype.h>

#define BLOCK_SIZE 4096 /* FIXME: Get the blocksize from the damn FS!  */

char *progname;

void usage()
{
	printf("usage: %s file seekstr [skipbytes]\n", progname);
	printf("seekstr may be a hexadecimal byte array like 7a4453, if it's a string, prepend an 's' character to the string.\n");
}

int hex2n(char c)
{
	c=tolower(c);
	if(c >= '0' && c<= '9') return c-'0';
	else if(c >= 'a' && c <= 'f') return c-'a'+0xa;
	else { dprintf(ERR, "Invalid hexadecimal character '%c' in hex2n()\n", c); exit(1); }
}


int main(int argc, char *argv[])
{
	int c;
	FILE *fp;
	char *s,*sarg;
	char *p;
	char *fname;
	unsigned nmatch=0;
	unsigned long long nblocks=0;
	int nread=0;

	progname = (argv[0]);
	fname = argv[1];
	sarg = argv[2];

	if(argc != 3 && argc != 4) { usage(); exit(0); }
	if(sarg[0] == 's') {
		sarg = &sarg[1];
		s = sarg;
	} else {
		s = malloc(strlen(sarg)/2);
		p=s;
		unsigned i;
		for(i=0; i<strlen(sarg); i+=2) *p++ = hex2n(sarg[i])*16 + hex2n(sarg[i+1]);
	}

	p = s;

	fp = fopen(fname, "r");
	if(!fp) { perror(strerror(errno)); exit(errno); }

	if(argc==4) {
		int i;
		for(i=0; i<strtoull(argv[3],0,16); i++)
			fseeko(fp, BLOCK_SIZE*1024, SEEK_CUR);
	}
	dprintf(INFO, "Seeking for \"%s\" in file \"%s\"\n", sarg, fname);
	dprintf(INFO, "Block size = %d\n", BLOCK_SIZE);
	fflush(stdout);
	while((c=fgetc(fp)) != EOF) {
		nread++;
		if(nread == BLOCK_SIZE) {
			nread=0;
			nblocks++;
			if(nblocks % (1024*1024) == 0) dprintf(INFO, "Current block = %llu\n", nblocks);
			fflush(stdout);
		}
		if(c == *p) {
			p++;
			if(*p == '\0') {
				printf("%lld:%d\n", nblocks, nread-strlen(s)), nmatch++;
				fflush(stdout);
			}
		} else {
			p=s;
		}
	}

	dprintf(INFO, "Found %u matches.\n", nmatch);

	return 0;
}
