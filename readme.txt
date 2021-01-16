QNX filesystem extract tool
by Mihai Gaitos, mihaig@hawk.ro, 2020-12
Released under BSD License

Contents:
    qnx_acc.h   - Filesystem and program structures
    qnx_acc.c   - Image file and filesystem access functions
    qdump.c     - Filesystem extract tool

	qobj.c		- QNX binary extract tool (extract code and data segments)
	qnx_file.h	- QNX executable (binary) header structures

Use 'make' to build the tools

Usage:
qobj <qnx_binary> <code_out> <data_out>

qdump <disk_image> {-d|-x|-r} path [-a] [-o offset] [-l local_path]
    -d  list directory at path (must be directory)
    -x  extract file (or directory contents, recursive) from path
    -r  read (dump) file to stdout
    -a  ASCII file (convert RS to LF)
    -o  Offset (in bytes) into image file (e.g. for partition)
    -l  Local destination for -x (file(s) extracted to local_path)

Notes:
Since QNX uses a different character for newline (0x1e - RS) instead of the
usual ones (0x0a - LF and/or 0x0d - CR), to easily read text files -a converts
all instances of RS to LF. While this is convenient for viewing text files, it
is not desired in binary files. If extracting multiple files avoid -a option
and use tr to convert:
cat <file> | tr '\036' '\012'

To read hdd images, first determine QNX partition start using another tool
(e.g. fdisk -l). Since usually the partition start is given in sectors,
multiply that by sector size (512) and use the resulting value for -o option
(see example below).

Known bugs/limitations
 - If multiple -r/d/x options are given, only last one is used
 - Option -a affects all files (binary ones would be mangled!)
 - (probably) Doesn't work correctly with deleted files or files with 0 length

Example runs:

List directory:

$ ./qdump qnx12_cc.img -d/
 bitmap                     90
+cmds                     3028
+lib                      5620
+mathlib                  1012

As in QNX, directories are prefixed with a +

View ASCII file:

$ ./qdump qnx12_cc.img -r/lib/math.h -a
extern long atol(), ftell();
extern double atof();
extern double sqrt(), fabs();
extern double exp(), exp2(), exp10(), cbrt(), pow();
...


Extract an entire image:

$ ./qdump qnx12-hd-xt-harddisk.img -o 512 -x/ -l qfiles/
qfiles/bitmap
qfiles/cmds/sh
qfiles/cmds/login
qfiles/cmds/comm
...

(the program displays the local name of each file that it writes)
