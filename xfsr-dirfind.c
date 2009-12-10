/*
 *      XFS Rescue - tools for saving files from damaged XFS partitions
 *
 *      Copyright 2008 Utkan Gungordu <salviati@freeconsole.org>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#include "xfsr.h"
#include <string.h>

static const char *g_progname = "xfsr-dirfind";

static inline int _isdir(FILE *devfp, uint64_t inode)
{
	xfs_dinode_t dinode;
	if(read_inode(devfp, &dinode, inode) < 0) return 0;
	return dinode_isdir(&dinode);
}

#ifdef BUILDPROGDIRFIND
void usage()
{

}

int main(int argc, char *argv[])
{
	int c;
	char *devfile=NULL;
	uint64_t inode=0;
	setlocale(LC_ALL, "");

	while( (c=getopt(argc,argv,"vI")) != EOF ) {

		switch(c) {
		case 'I':
			inode= strtoull(optarg,0,10);
			break;
		case 'v':
			g_verbose++;
			break;
		default:
			usage();
			exit(0);
		}
	}

	if(optind>=argc) {
		usage();
		exit(0);
	}

	devfile = argv[optind];

	FILE *devfp = fopen(devfile, "r");
	if(!devfp) { perror(strerror(errno)); exit(errno); }

	if(read_sb(devfp) <0) {
		eprintf(ERR, "Not a valid superblock");
		exit(2);
	}
	uint32_t inodesize = 1<< g_sb.sb_inodelog;
	seek_iadr(devfp, inode);

	while( (c=fgetc(devfp)) != EOF ) {
		if(c=='I') {
			if( (c=fgetc(devfp)) == 'N' && _isdir(devfp, inode)) { printf("0x%llx\n", inode); }
			fseek(devfp,inodesize-2,SEEK_CUR);
		} else {
			fseek(devfp,inodesize-1,SEEK_CUR);
		}
		inode++;
	}

	return 0;
}
#endif
