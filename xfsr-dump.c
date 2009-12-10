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
#include <sys/types.h>
#include <unistd.h>

static const char *g_progname = "xfsr-dump";
static int g_preserve = 0;
static uint64_t g_iadr = 0, g_ino=0;

void set_dump_opts(int preserve)
{
	g_preserve = preserve;
}

static int dump_symlink_local(FILE *devfp, xfs_dinode_t *dinode, const char *outfile)
{
	off_t off = ftello(devfp);
	seek_iadr(devfp, g_iadr);
	fseek(devfp, INO_DATA_FORK_OFFSET, SEEK_CUR);
	unsigned len = GET64(dinode->di_core.di_size);
	assert(len <= GET32(g_sb.sb_blocksize) - INO_DATA_FORK_OFFSET);
	char name[len+1];
	fread(&name, len, 1, devfp);
	name[len] = '\0';
	fseeko(devfp, off, SEEK_SET);
	if(symlink(name,outfile) != 0) { eprintf(ERR, "symlink() failed:"); return -1; }
	return 0;
}

static int dump_symlink_extents(FILE *devfp, xfs_dinode_t *dinode, const char *outfile)
{
	eprintf(ERR, "Symlinks with extents are not implemented yet");
	return -1;
}

static int dump_symlink(FILE *devfp, xfs_dinode_t *dinode, const char *outfile)
{
	switch (dinode->di_core.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		return dump_symlink_extents(devfp,dinode,outfile);
	case XFS_DINODE_FMT_LOCAL:
		return dump_symlink_local(devfp,dinode,outfile);
	default:
		eprintf(ERR, "Unhandled/unknown inode format: %d", dinode->di_core.di_format);
		return -1;
	}
}

static uint64_t handle_extents(FILE *devfp, uint64_t fsize, xfs_bmbt_rec_64_t *recs, uint16_t numrecs, FILE *outfp)
{
	uint64_t dumped = 0;
	uint32_t blocksize = GET32(g_sb.sb_blocksize);
	uint64_t rem = fsize;
	char buffer[blocksize];

	int i;
	for(i=0; i<numrecs; i++) {
		xfs_bmbt_irec_t irec;
		xfs_bmbt_disk_get_all(&recs[i] , &irec);
		eprintf(INFO, "  startoff = 0x%llx", irec.br_startoff);
		eprintf(INFO, "  startblock = 0x%llx", irec.br_startblock);
		eprintf(INFO, "  blockcount = 0x%llx", irec.br_blockcount);
		seek_blkno(devfp,irec.br_startblock);
		unsigned blockcount = irec.br_blockcount;
		int j;
		for(j=0; j<blockcount && rem>=blocksize; j++, rem-=blocksize) {
			fread(buffer, blocksize, 1, devfp);
			uint64_t written_bytes;
			written_bytes = fwrite(buffer, blocksize, 1, outfp)*blocksize;
			if(written_bytes != blocksize)
				eprintf(WARN, "Written bytes do not match blocksize");
			dumped += written_bytes;
		}
	}

	if(rem+dumped > fsize) {
		eprintf(ERR, "Dumped bytes + remaining bytes exceeds the file size!");
		return dumped;
	}
	if(rem > blocksize)
	{
		eprintf(ERR, "Remaining bytes exceeds blocksize!");
		return dumped;
	}

	if(dumped < fsize) {
		dumped += fread(buffer, rem, 1, devfp)*rem;
		fwrite(buffer, 1, rem, outfp);
	}

	if(dumped != fsize)
		eprintf(WARN, "Dumped bytes do not match the file size");

	return dumped;
}

static int dump_file_extents(FILE *devfp, xfs_dinode_t *dinode, const char *outfile)
{
	FILE *outfp;
	outfp = fopen(outfile, "w");
	if(!outfp) { perror(strerror(errno)); exit(errno); }

	unsigned nextents = GET32(dinode->di_core.di_nextents);
	eprintf(INFO, "Number of extents = 0x%x", nextents);

	//Read extent records
	off_t off = ftello(devfp);
	seek_iadr(devfp, g_iadr);
	fseek(devfp, INO_DATA_FORK_OFFSET, SEEK_CUR);
	xfs_bmbt_rec_64_t recs[nextents];
	fread(recs, sizeof(xfs_bmbt_rec_64_t), nextents, devfp);

	uint64_t fsize = GET64(dinode->di_core.di_size);
	uint64_t dumped = handle_extents(devfp,fsize,recs,nextents,outfp);

	fseeko(devfp, off, SEEK_SET);
	fclose(outfp);
	eprintf(INFO, "Dumped all blocks, %llu bytes in total.", dumped);
	if(dumped!=fsize) { eprintf(ERR, "Sanity check failure: dumped size doesn't match the file size!"); return -1; }
	return 0;
}

static int handle_btree_record(FILE *devfp, uint64_t blkno, uint64_t fsize, FILE *outfp)
{
	uint32_t blocksize = GET32(g_sb.sb_blocksize);
	unsigned char block[blocksize];

	uint64_t blkadr = blkno_to_blkadr(blkno);
	eprintf(INFO, "B+ record; blockno=0x%0llx (blkadr=0x%0llx)", blkno, blkadr);
	fseeko(devfp, blkno, SEEK_SET);
	seek_blkno(devfp, blkno);
	fread(block, blocksize, 1, devfp);
	uint32_t magic = GET32P(&block[0]);
	if(magic != XFS_BMAP_MAGIC) {
		eprintf(ERR, "BMAP magic failed: 0x%x", magic);
		return -1;
	}
	if(GET16P(&block[4]) != 0) {
		eprintf(ERR, "Deep block!");
		return -80;
	}
	uint16_t numrecs = GET16P(&block[6]);
	uint64_t left =  GET64P(&block[8]);
	uint64_t right = GET64P(&block[0x10]);

	if(left != 0xffffffffffffffffLL || right != 0xffffffffffffffffLL) {
			eprintf(ERR, "Left & right siblings must be NULL pointers!");
			return -80;
	}

	xfs_bmbt_rec_64_t *recs = (xfs_bmbt_rec_64_t*)&block[0x18];
	uint64_t dumped = handle_extents(devfp,fsize,recs,numrecs,outfp);
	if(dumped!=fsize) { eprintf(ERR, "Sanity check failure: dumped size doesn't match the file size!"); return -1; }
	return 0;

}

static int dump_file_btree(FILE *devfp, xfs_dinode_t *dinode, const char *outfile)
{
	FILE *outfp;
	outfp = fopen(outfile, "w");
	if(!outfp) { perror(strerror(errno)); exit(errno); }

	off_t off = ftello(devfp);

	seek_iadr(devfp, g_iadr);

	unsigned inodesize = GET16(g_sb.sb_inodesize);
	unsigned char inode[inodesize];

	fread(inode, inodesize, 1, devfp);
	xfs_bmdr_block_t *bmdr_block = (xfs_bmdr_block_t *) &inode[INO_DATA_FORK_OFFSET];

	if(GET16(bmdr_block->bb_level) != 1) {
		eprintf(ERR, "B+ directories with bb_level > 1 are not handled yet\n");
		exit(80);
	}

	uint64_t *recs = (uint64_t*)(&inode[INO_DATA_FORK_OFFSET+4+0x48]);
	uint64_t fsize = GET64(dinode->di_core.di_size);

	eprintf(INFO, "Numrecs: 0x%0x", GET16(bmdr_block->bb_numrecs));

	int i;
	for(i=0; i<GET16(bmdr_block->bb_numrecs); i++)
		handle_btree_record(devfp, GET64(recs[i]), fsize, outfp);

	fclose(outfp);
	fseeko(devfp, off, SEEK_SET);
	return 0;
}

static int dump_file(FILE *devfp, xfs_dinode_t *dinode, const char *outfile)
{
	uint64_t fsize = GET64(dinode->di_core.di_size);
	eprintf(INFO, "File size = %lld",  fsize);

	eprintf(INFO, "Dumping to file \"%s\"", outfile);
	/* Dump inode */

	switch (dinode->di_core.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		return dump_file_extents(devfp,dinode,outfile);
	case XFS_DINODE_FMT_BTREE:
		return dump_file_btree(devfp,dinode,outfile);
	default:
		eprintf(ERR, "Unhandled/unknown inode format: %d", dinode->di_core.di_format);
		return -1;
	}
}

void restore_stats(const char *outfile, xfs_dinode_t *dinode)
{
	uint16_t mode = GET16(dinode->di_core.di_mode);
	if( chown(outfile, GET32(dinode->di_core.di_uid), GET32(dinode->di_core.di_gid)) != 0)
		eprintf(WARN, "chown() failed:");
	/* Handle uid,gid,mode,atime.ctime.mtime */
	/* FIXME: todo */
	if(chmod(outfile, mode) != 0)
		eprintf(WARN, "chmod() failed:");

	struct timeval tv[2];
	tv[0].tv_sec = GET32(dinode->di_core.di_atime.t_sec);
	tv[0].tv_usec = GET32(dinode->di_core.di_atime.t_nsec)/1000;
	tv[1].tv_sec = GET32(dinode->di_core.di_mtime.t_sec);
	tv[1].tv_usec = GET32(dinode->di_core.di_mtime.t_nsec)/1000;
	if(utimes(outfile,tv) != 0)

	eprintf(WARN, "utimes() failed:");
}

int dump(FILE *devfp, const char *outfile, uint64_t iadr)
{
	xfs_dinode_t dinode;
	eprintf(INFO, "Reading inode at iadr=0x%llx", iadr);

	g_iadr = iadr;
	g_ino = iadr_to_ino(g_iadr);

	if(read_inode(devfp, &dinode, iadr) < 0) {
		eprintf(ERR, "Not a valid inode");
		return -1;
	}

	dinode_di_core_print(&dinode);

	int err = 0;
	uint16_t mode = GET16(dinode.di_core.di_mode);
	if(S_ISREG(mode)) err = dump_file(devfp,&dinode,outfile);
	else if(S_ISLNK(mode)) err = dump_symlink(devfp,&dinode,outfile);
	else { 	eprintf(ERR, "Not a regular file or symlink (mode=0%o)", mode); return -2; }

	if(g_preserve) restore_stats(outfile, &dinode);
	if(err) eprintf(ERR, "Dump of %s failed", outfile);
	return err;
}

#ifdef BUILDPROGDUMP
void usage()
{
	printf("Dump a regular file or symlink at a given ino/iadr\n");
	printf("%s [-v -p -L logfile] -o outfile (-A iadr | -N ino) devfile\n", g_progname);
}

int main(int argc, char *argv[])
{
	int c;
	char *devfile=NULL, *outfile=NULL;
	setlocale(LC_ALL, "");

	while( (c=getopt(argc,argv,"vN:A:o:pL:")) != EOF ) {

		switch(c) {
		case 'N':
			g_ino = strtoull(optarg,0,10);
			break;
		case 'A':
			g_iadr = strtoull(optarg,0,10);
			break;
		case 'o':
			outfile = optarg;
			break;
		case 'p':
			if(geteuid() != 0) {
				eprintf(WARN, "You *might* need to be root to use -p.");
			}
			g_preserve = 1;
			break;
		case 'v':
			g_verbose++;
			break;
		case 'L':
			g_logfile = optarg;
			break;
		default:
			usage();
			exit(0);
		}
	}

	if(optind>=argc || outfile == NULL || (g_ino == 0 && g_iadr == 0) ) {
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
	sb_print();

	if(g_iadr == 0) g_iadr = ino_to_iadr(g_ino);
	else g_ino = iadr_to_ino(g_iadr);

	if(g_iadr == 0) {
		eprintf(ERR, "Can't proceed: iadr=0");
		exit(2);
	}

	int err = dump(devfp, outfile, g_iadr);
	if(err) eprintf(ERR, "Failure");
	return -err;
}
#endif
