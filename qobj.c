#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "qnx_file.h"

#define MAXFILELIMIT 1024*1024	/* 1 Meg should suffice */

/* read whole fn into a (NULL terminated) buffer
 * returns number of bytes read or -1 for all errors */
ssize_t file2lbuf(char *fn, char **dbuf)
{
	if(fn==NULL)
	{
		fprintf(stderr,"file2lbuf: File name is NULL!\n");
		return -1;
	}
	struct stat finfos;
	char *buf;
	int fd=open(fn,O_RDONLY);
	if(fd<1)
	{
		fprintf(stderr,"file2lbuf: Unable to open %s\n",fn);
		return -1;
	}
	if(fstat(fd,&finfos)!=0)
	{
		fprintf(stderr,"file2lbuf: Unable to stat %s\n",fn);
		return -1;
	}
	off_t isize=finfos.st_size;
	if(isize>MAXFILELIMIT)
	{
		fprintf(stderr,"file2lbuf: %s file size above limit.\n(MAXFILELIMIT=%d)",fn,MAXFILELIMIT);
		return -1;
	}
	if(isize==0)
	{
		fprintf(stderr,"file2lbuf: %s file size is 0\n",fn);
		close(fd);
		return 0;
	}
	buf=malloc(isize+1);
	off_t r=read(fd,buf,isize);
	close(fd);
	if(r!=isize)
	{
		fprintf(stderr,"file2lbuf: Unable to read (whole?) %s\n",fn);
		free(buf);
		return -1;
	}
	buf[isize]=0;
	*dbuf=buf;
	return isize;
}

/* write buf to file, passing of and cm to open(2) 
 * returns 0 on success */
int buf2file(void *buf, char* fn, int l, int of, int cm)
{
	int r;
	size_t rb=l;
	char *dbuf=(char *)buf;
	if(fn==NULL)
	{
		fprintf(stderr,"File name is NULL in buf2file!\n");
		return -1;
	}
	int fd=open(fn,of,cm);
	if(fd<1)
	{
		fprintf(stderr,"Unable to open or create %s\n",fn);
		return -1;
	}
	while(rb)
	{
		r=write(fd,dbuf,l);
		if(r<0)
		{
			fprintf(stderr,"Write error for %s\n",fn);
			close(fd);
			return -1;
		}

		rb-=r;
		dbuf+=r;
	}
	close(fd);
	return 0;
}


void usage(char *pn, int rv)
{
	printf("Usage: %s <qnx_file> <qnx_code> <qnx_data>\n",pn);
	exit(rv);
}

void disp_main_header(struct load_hdr_record *h)
{
	printf("Code_flags:\t  %02x\n",h->Code_flags);
	printf("Code_size:\t%04x (%u)\n",h->Code_size,h->Code_size);
	printf("Init_data_size:\t%04x (%u)\n",h->Init_data_size,h->Init_data_size);
	printf("Const_size:\t%04x (%u)\n",h->Const_size,h->Const_size);
	printf("Const_checksum:\t%04x (%u)\n",h->Const_checksum,h->Const_checksum);
	printf("Stack_size:\t%04x (%u)\n",h->Stack_size,h->Stack_size);
	printf("Code_spare:\t%04x (%u)\n",h->Code_spare,h->Code_spare);
}

/* TODO: bounds checking */
uint32_t load_record_header(struct load_data_record *rh,char *cs, char *ds)
{
	char *dst=NULL;
	printf("Load type:\t%02x\n",rh->Load_type);
	printf("Offset:\t%04x (%u)\n",rh->Offset,rh->Offset);
	printf("Length:\t%04x (%u)\n",rh->Length,rh->Length);
	switch(rh->Load_type)
	{
		case 0:
			dst=cs;
			break;
		case 1:
			dst=ds;
			break;
		default:
			fprintf(stderr,"** ERROR ** Unknown load type %x\n",rh->Load_type);
			break;
	}
	if(dst)
		memcpy(dst+rh->Offset,rh->data,rh->Length);

	return rh->Length+5;
}

int main(int argc, char *argv[])
{
	int rv=0;
	ssize_t fl;
	uint32_t bpos=0;
	char *src=NULL, *cs=NULL, *ds=NULL;
	struct load_hdr_record *h;
	if(argc!=4)
		usage(argv[0],EXIT_FAILURE);
	fl=file2lbuf(argv[1],&src);
	if(fl < sizeof(struct load_hdr_record))
	{
		rv=1;
		fprintf(stderr,"File not found or too small: %s\n",argv[1]);
		goto eofunc;
	}
	if(src[0]!=1)
	{
		rv=1;
		fprintf(stderr,"Missing SOH\n");
		goto eofunc;
	}
	h=(struct load_hdr_record *)(src+1);
	disp_main_header(h);

	cs=calloc(h->Code_size,1);
	ds=calloc(h->Init_data_size,1);

	bpos=1+sizeof(struct load_hdr_record);

	while(bpos<fl)
	{
		if(src[bpos]!=2)
		{
			fprintf(stderr,"Missing STX at %x (%u)\n",bpos,bpos);
			break;
		}
		bpos++;
		bpos+=load_record_header((struct load_data_record *)(src+bpos),cs,ds);
	}

	if(buf2file(cs,argv[2],h->Code_size,O_CREAT | O_EXCL | O_WRONLY,0644))
	{
		fprintf(stderr,"Error writing code segment to %s\n",argv[2]);
		rv=2;
	}
	if(buf2file(ds,argv[3],h->Init_data_size,O_CREAT | O_EXCL | O_WRONLY,0644))
	{
		fprintf(stderr,"Error writing data segment to %s\n",argv[3]);
		rv=3;
	}
eofunc:
	free(src); free(cs); free(ds);
	return rv;
}
