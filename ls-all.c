#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	FILE *fp = fopen(argv[1], "r");
	assert(fp);
	uint64_t iadr;
	char cmd[256];
	for(; !feof(fp);) {
		if(fscanf(fp, "%llx", &iadr) == 0) break;
		printf("[INODE] 0x%llx\n", iadr);
		fflush(stdout);
		sprintf(cmd, "xfsr-ls -A %llu -m /dev/sdc1\n", iadr);
		system(cmd);
	}
	return 0;
}
