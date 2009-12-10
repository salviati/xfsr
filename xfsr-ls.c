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
#include <getopt.h>
#include <regex.h>


void set_dump_opts(int preserve);
int dump(FILE *devfp, const char *outfile, uint64_t iadr);
void restore_stats(const char *outfile, xfs_dinode_t *dinode);
int ls(FILE *fp, uint64_t iadr);

static int g_dump=0, g_recurse=0, g_recurse_cur=0,g_preserve=0, g_incasesensitive;
static int show_hidden = 1, minimal_list=0;
static const char *g_progname = "xfsr-ls";
static FILE *g_outfp;
static char *g_pattern;
static regex_t compiled;

void print_entry(FILE *devfp, uint64_t ino, xfs_dinode_t *dinode, const char *name)
{
	if(name[0] == '.' && !show_hidden) return;
	if(g_outfp == NULL) g_outfp = stdout;

	//if(g_pattern && fnmatch(g_pattern, name, FNM_FILE_NAME | FNM_EXTMATCH | (g_incasesensitive ? FNM_CASEFOLD : 0) ) )
	if(g_pattern && regexec(&compiled, name, 0, NULL, 0) )
		return;

	uint64_t iadr = ino_to_iadr(ino);
	uint16_t mode = GET16(dinode->di_core.di_mode);
	if(minimal_list) {
		fprintf(g_outfp, "0x%08llx\t%s\n", ino, name);
	} else {
		fprintf(g_outfp, "[ENTRY]\t0x%08llx\t0x%08llx\t%08llu\t%o\t%u\t%u\t%s\n", iadr, ino, GET64(dinode->di_core.di_size),
			mode, GET32(dinode->di_core.di_uid),
			GET32(dinode->di_core.di_gid), name);
	}

	if(g_recurse > g_recurse_cur && S_ISDIR(mode) && strcmp(name, ".") && strcmp(name, "..") ) {

		if(g_dump) {
			if(mkdir(name,mode)) { eprintf(ERR, "mkdir() failed:"); return; }
			if(g_preserve) restore_stats(name,dinode);
			if(chdir(name)) { eprintf(ERR, "chdir() failed:"); return; }
		}
		g_recurse_cur++;
		ls(devfp, iadr);
		g_recurse_cur--;
		if(chdir("..")) { eprintf(ERR, "chdir() failed:"); return; }
	} else if(S_ISREG(mode) || S_ISLNK(mode)) {
		if(g_dump && dump(devfp, name, iadr) != 0) eprintf(ERR, "Failed to dump %s", name);
	}
}

static int ls_local(FILE *devfp, xfs_dinode_t *dinode, uint64_t g_iadr)
{
	off_t off = ftello(devfp);
	seek_iadr(devfp, g_iadr);
	unsigned inodesize = GET16(g_sb.sb_inodesize);
	unsigned char inode[inodesize];
	fread(inode,inodesize,1,devfp);
	fseeko(devfp, off, SEEK_SET);
	xfs_dir2_sf_hdr_t *dir2_hdr = (xfs_dir2_sf_hdr_t*)&inode[INO_DATA_FORK_OFFSET];
	unsigned count=0, inolen=0;
	if(dir2_hdr->count) count=dir2_hdr->count, inolen = 4;
	else if(dir2_hdr->i8count) count=dir2_hdr->i8count, inolen = 8;
	else { eprintf(ERR, "Corrupted local dir at iadr=0%Lx", g_iadr); return -1; }

	unsigned char *p = &inode[INO_DATA_FORK_OFFSET] + 1+1;
	unsigned i;

	uint64_t parent_ino = inolen==4 ? GET32(*((uint32_t*)p)) : GET64(*((uint64_t*)p));
	p+=inolen;

	uint64_t g_ino = iadr_to_ino(g_iadr);

	print_entry(devfp,g_ino,dinode,".");
	xfs_dinode_t parent_dinode;
	read_inode(devfp,&parent_dinode,ino_to_iadr(parent_ino));
	print_entry(devfp,parent_ino,&parent_dinode,"..");

	for(i=0; i<count; i++) {
			uint8_t namelen = *p++;
			//uint16_t offset = GET16(*((uint16_t*)p)); //FIXME: what the heck is this offset?
			p+=2;
			char name[namelen+1];
			memcpy(name,p,namelen);
			p+=namelen;
			name[namelen] = '\0';
			uint64_t ino;
			ino = inolen==4 ? GET32P(p) : GET64P(p);
			p+=inolen;
			xfs_dinode_t einode;
			if( read_inode(devfp,&einode,ino_to_iadr(ino)) ) {
				eprintf(ERR, "Invalid local");
			}
			print_entry(devfp,ino,&einode,name);
	}
	return 0;
}

static int read_dir2_block(unsigned offset, char *blockp, char *name, uint64_t *inop)
{
	uint64_t *blockp64 = (uint64_t*)blockp;
	uint8_t *ublockp = (uint8_t*)blockp;
	*inop = GET64(*blockp64);

	unsigned len;
	unsigned size;
	unsigned tag;
	if(*inop>>48 == 0xffff) {
		len = (*inop>>32) & 0xffff;
		size = len;
		tag = blockp[size-2];
	} else {
		len = ublockp[8];
		memcpy(name, &ublockp[9], len);
		name[len] = '\0';

		size=8+1+len+2; // 8 for inode adr, 1 for strlen, len for name, 2 for tag
		unsigned size_raw = size;
		if(size&7) size += 8-(size&7); // align to 8-bytes boundary

	}

	tag = (((unsigned)ublockp[size-2])<<8) + ublockp[size-1];

	if(offset != tag && len != 0) {
		eprintf(WARN,"Block dir tag doesn't match the offset (tag=0x%x,offset=0x%x,size=0x%x)\n",
			tag, offset, size);
	}
	return tag==0 || len == 0 ? 0 :size;

}

static int ls_extents_handle_extent(unsigned nblocks, FILE *fp, xfs_bmbt_irec_t *irec, uint64_t g_iadr)
{
	eprintf(INFO, "Begin extent block (blkadr=0x%llx, blkno=0x%llx)",
		blkno_to_blkadr(irec->br_startblock), irec->br_startblock);

	seek_blkno(fp, irec->br_startblock);

	if(irec->br_startoff == 1LL<<(35-g_sb.sb_blocklog)) {
		eprintf(WARN, "Extent dirs' leaves are not handled");
		return 0;
	}

	// Verify magic
	uint32_t magic;
	fread(&magic, 4, 1, fp);
	magic = GET32(magic);
	if(magic != (nblocks==1 ? XFS_DIR2_BLOCK_MAGIC : XFS_DIR2_DATA_MAGIC) ) {
		eprintf(ERR, "Dir block magic failed: 0x%x", magic);
		return -1;
	}

	uint32_t blocksize = GET32(g_sb.sb_blocksize);
	// Read rest of the block
	char block[blocksize];
	fread(&block[4],blocksize-4,1,fp);

	// Read & print out entries
	char name[255+1];
	char *p = &block[0x10];
	uint64_t ino;
	unsigned nentries=0;

	do {
		int size = read_dir2_block((unsigned long)p - (unsigned long)block, p,name, &ino);
		if(size == 0) break;
		p += size;

		if(p>=&block[blocksize]) // end of block
			break;

		if( (ino>>48)==0xffff ) // unlinked entry
			continue;

		uint64_t iadr = ino_to_iadr(ino);

		xfs_dinode_t dinode;
		if(!read_inode(fp,&dinode,iadr)) {
			print_entry(fp,ino,&dinode,name);

			if(nentries == 0 && !strcmp(".", name) && iadr != g_iadr) {
				eprintf(ERR,"Entry . doesnt point to itself (g_iadr=0x%llx)", iadr);
				return -2;
			}
			nentries++;
		}
	} while(p<&block[blocksize]);

	return (int)nentries;
}


static int ls_extents(FILE *fp, xfs_dinode_t *dinode, uint64_t g_iadr)
{
	unsigned nextents = GET32(dinode->di_core.di_nextents);
	off_t off = ftello(fp);

	seek_iadr(fp, g_iadr);
	fseek(fp, INO_DATA_FORK_OFFSET, SEEK_CUR);

	xfs_bmbt_rec_64_t rec[nextents];
	fread(rec, sizeof(xfs_bmbt_rec_64_t), nextents, fp);
	fseeko(fp, off, SEEK_SET);

	unsigned nentries = 0, i;
	for(i=0; i<nextents; i++) {
		xfs_bmbt_irec_t irec;
		xfs_bmbt_disk_get_all(&rec[i] , &irec);
		int count = ls_extents_handle_extent(nextents,fp,&irec,g_iadr);
		if(count < 0) return -1;
		nentries += (unsigned)count;
	}

	if(nentries <2 ) {
		eprintf(ERR,"A directory must have at least 2 entries");
		return -1;
	}

	eprintf(INFO, "%u entries in total.", nentries);
	return 0;
}

static int ls_btree(FILE *fp, xfs_dinode_t *dinode)
{
	eprintf(ERR, "B+ tree directories are not handled yet.");
	return -80;
}

int ls(FILE *fp, uint64_t iadr)
{
	xfs_dinode_t dinode;

	eprintf(INFO, "Reading inode at iadr=0x%llx", iadr);

	uint64_t g_ino = iadr_to_ino(iadr);

	if(read_inode(fp, &dinode, iadr) < 0) {
		eprintf(ERR, "Not a valid inode (iadr=0x%llx, ino=0x%llx)", iadr, g_ino);
		exit(1);
	}

	if(!dinode_isdir(&dinode)) {
		eprintf(ERR, "Not a directory");
		exit(1);
	}

	dinode_di_core_print(&dinode);
	eprintf(INFO, "Listing entries");
	eprintf(INFO, "\tiadr\t\tino\t\tsize\t\tmode\tuid\tgid\tname");

	int err;

	switch(dinode.di_core.di_format)
	{
		case XFS_DINODE_FMT_LOCAL:
			err = ls_local(fp,&dinode,iadr);
		break;

		case XFS_DINODE_FMT_EXTENTS:
			err = ls_extents(fp,&dinode,iadr);
		break;

		case XFS_DINODE_FMT_BTREE:
			err = ls_btree(fp,&dinode);
		break;

		default:
			eprintf(ERR, "Unknown/unhandled dir format");
			exit(1);
	}

	return -err;
}

#ifdef BUILDPROGLS
void usage()
{
	printf("List a directory at a given ino/iadr\n");
	printf("usage: %s [-v -H -m -L logfile -p -D dumpdir -R recurselevel] (-A iadr | -N ino) devfile\n", g_progname);
}

int main(int argc, char *argv[])
{
	int c;
	char *devfile = NULL;
	setlocale(LC_ALL, "");
	uint64_t g_iadr=0, g_ino=0;

	while( (c=getopt(argc,argv,"R:D:vmHN:A:L:pP:")) != EOF ) {

		switch(c) {
		case 'N':
			g_ino = strtoull(optarg,0,10);
			break;
		case 'A':
			g_iadr = strtoull(optarg,0,10);
			break;
		case 'v':
			g_verbose++;
			break;
		case 'H':
			show_hidden = 0;
			break;
		case 'm':
			minimal_list = 1;
			break;
		case 'L':
			g_logfile = optarg;
			break;
		case 'D':
			g_dump = 1;
			if(chdir(optarg)) { eprintf(ERR, "chdir() failed:"); exit(1); }
			break;
		case 'R':
			g_recurse = atoi(optarg);
			break;
		case 'p':
			set_dump_opts(1);
			g_preserve = 1;
			break;
		case 'P':
			g_pattern = optarg;
			break;
		case 'i':
			g_incasesensitive = 1;
			break;
		default:
			usage();
			exit(0);
		}
	}

	if(optind>=argc || (g_ino == 0 && g_iadr == 0) ) {
		usage();
		exit(0);
	}

	devfile = argv[optind];

	FILE *fp = fopen(devfile, "r");
	if(!fp) { perror(strerror(errno)); exit(errno); }

	if(read_sb(fp) <0) {
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

	if(g_pattern) regcomp(&compiled, g_pattern, REG_NOSUB | (g_incasesensitive ? REG_ICASE : 0));

	int err = ls(fp, g_iadr);
	if(err) eprintf(ERR, "Failure");
	return -err;
}
#endif
