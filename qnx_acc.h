/* qnx_acc.h - QNX (1.2) filesystem access structures and function definitions
 *
 * Copyright 2020 Mihai Gaitos, mihaig@hawk.ro
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */


/* definitions required for qnx filesystem access */

#define Q_BLOCKSIZE 512

#define QNX_MAXFNLEN 16	/* see direntry */

/* internal flags (i.e. related to qnx_acc functions)) */
#define QIF_ATEOF	1<<0
#define QIF_ERR		1<<1

/* qnx file attributes */
#define QFA_DIRECTORY	0x20

/* helper macros */
#define func_abort(msg, args...) \
	do { fprintf(stderr,"%s: " msg "\n",__func__, ## args); return -1; } while (0)

#define func_msg(msg, args...) \
	do { fprintf(stderr,"%s: " msg "\n",__func__, ## args); } while (0)

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
# define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif


/* internal qnx structures */
#pragma pack(1) /* avoid alignment - match disk structure */
struct q_xtnt_header
{
	uint32_t prev_xtnt;
	uint32_t next_xtnt;
	uint32_t size_xtnt;
	uint32_t bound_xtnt;
};

struct q_xtnt
{
	struct q_xtnt_header header;
	uint8_t xtnt_data[];
};

struct q_dir_entry	/* based on QNX 2.1 tehnical notes */
{
	uint8_t fstat;
	int32_t ffirst_xtnt;
	int32_t flast_xtnt;
	int32_t fnum_blks;
	uint16_t fnum_xtnt;
	uint8_t fowner;
	uint8_t fgroup;
	uint16_t fnum_chars_free;
	int32_t fseconds;
	uint8_t ftype;
	uint8_t fgperms;
	uint8_t fperms;
	uint8_t fattr;
	uint16_t fdate[2];
	uint8_t fname[QNX_MAXFNLEN+1];
};

struct q_dir_cont
{
	uint16_t	parent_xtnt;
	uint16_t	dir_index;
	struct q_dir_entry de[];
};

struct q_block1	/* superblock */
{
	struct q_xtnt_header header;
	uint8_t	res1[4];	/* reserved/unknown */
	uint16_t creatdate[2];
	uint8_t res2[44];	/* reserved/unknown */
	struct q_dir_entry root_dir;
};

#pragma pack()

/* internal structures used by the qnx_acc functions */

typedef struct qnx_disk
{
	int fd;
	char sbuf[Q_BLOCKSIZE];	/* sector buffer */
	size_t isize;			/* image size */
	uint32_t ioff;			/* image offset, used to read partitions */
} qnx_disk;

typedef struct qnx_file
{
	qnx_disk *	qd;
	uint32_t	attrs;
	uint32_t	iflags;	/* internal flags */
	uint32_t	fsize;
	uint32_t	fpos;	/* absolute position into file */
	uint32_t	firstx;	/* block number of first extent of file */
	uint32_t	crtx;	/* block number of current extent */
	uint32_t	prvx;
	uint32_t	nxtx;
	uint32_t	xpos;	/* position into current extent */
	uint32_t	xsize;	/* size (in bytes) of current extent */
} qnx_file;

/* function definitions */

/**************
 * disk image *
 **************/

int qd_close(qnx_disk *qd);

/* open disk image at path and fills qd info; ioff is optional offset
 * e.g. for partitions inside hdd images */
int qd_open(qnx_disk *qd, char *path, uint32_t ioff);

/* (0-based) absolute sector number into buf */
int qd_read_sector(qnx_disk *qd, uint32_t sn, void *buf);

/* absolute (byte granularity) read from disk image 
 * (calls qd_read_sector) */
int32_t qd_read(qnx_disk *qd, void *buf, uint32_t offset, uint32_t count);


/***************
 * extent/data *
 ***************/

/* read count bytes from offset in extent
 * trims (offset+)count to extent size 
 * returns number of bytes read or -1 on error */
int32_t qnx_read_xtnt_data(qnx_disk *qd,uint32_t bn,uint32_t offset,void *buf,uint32_t count);

/* read extent header at block number bn into h */
int qnx_read_xh(qnx_disk *qd,uint32_t bn,struct q_xtnt_header *h);


/*************
 * directory *
 *************/

/* calculate file size - QNX 1.2 directory entry does not include
 * bytes count, only blocks count, and only counts full blocks */
int32_t qnx_filesize(qnx_disk *qd, struct q_dir_entry *de);

/* file open helper - initializes fd with file date (mostly from de) */
int qnx_de2fd(qnx_disk *qd,struct q_dir_entry *de,qnx_file *fd);

/* open root directory (memory-based buffer access only) */
int qnx_open_root(qnx_disk *qd, qnx_file *fd);

/* init directory (call after opening fd, before calling qnx_dir_nextentry)
 * maybe add a flag in fd to avoid having to manually call this? */
int qnx_dir_init(qnx_file *fd);

/* read next direntry from fd (returns 0 on success) */
int qnx_dir_nextentry(qnx_file *fd, struct q_dir_entry *d);

/* search fd (directory) for name. If found, fill dde with its directory entry
 * return 0 if found */
int qnx_search_dir(qnx_file *fd, char *name, struct q_dir_entry *dde);


/********
 * file *
 ********/

/* set fd fields corresponding to xtnt at bn */
int qnx_set_fd_xtnt(qnx_file *fd, uint32_t bn);

/* advance fd so that xpos is *inside* extent except at EOF */
int qnx_advance_xtnt(qnx_file *fd);

/* seek fd to offset (no support for other seek modes)
 * returns resulting offset (can be truncated to file size)
 * or -1 on failure */
int32_t qnx_seek(qnx_file *fd, int32_t offset);

/* similar to read(2) except fd is pointer */
int32_t qnx_read(qnx_file *fd, void *buf, uint32_t count);

/* open file at path (initializes fd). returns 0 on success */
int q_open_file(qnx_disk *qd, char *path, qnx_file *fd);

