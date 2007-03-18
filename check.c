/*
 * check - check on checked out RCS files
 *
 * @(#) $Revision: 3.11 $
 * @(#) $Id: check.c,v 3.11 2007/03/18 11:44:27 chongo Exp chongo $
 * @(#) $Source: /usr/local/src/cmd/check/RCS/check.c,v $
 *
 * Please do not copyright this code.  This code is in the public domain.
 *
 * LANDON CURT NOLL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO
 * EVENT SHALL LANDON CURT NOLL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Long ago, there was an old version of this code that was written
 * by Kipp Hickman.
 *
 * The many bug fixes, the many security fixes, the major code cleanup,
 * a complete code rewrite, the change of -l to prints lock info (not
 * filenames), and rest of the flags: these were all done by:
 *
 * 	chongo (Landon Curt Noll) /\oo/\
 * 	http://www.isthe.com/chongo/index.html
 *
 * Share and Enjoy!  :-)
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <stdarg.h>

#define MAX_OWNER_LEN 20	/* longest owner printed */
#define MAX_REVIS_LEN 20	/* longest owner revision printed */
static char owner[MAX_OWNER_LEN+1];	/* owner of file co'd */
static char revision[MAX_REVIS_LEN+1];	/* revision of file co'd */
static int cflag = 0;		/* print 1 word comment before filenames */
static int dflag = 0;		/* note when file and RCS differ */
static int hflag = 0;		/* print help and exit */
static int lflag = 0;		/* print RCS lock info */
static int mflag = 0;		/* report missing files under RCS */
static int pflag = 0;		/* print the real path of a file */
static int qflag = 0;		/* do not report locked filenames */
static int rflag = 0;		/* recursive search for checked files */
static int Rflag = 0;		/* report on *.rpm{orig,init,save,new} files */
static int tflag = 0;		/* print RCS mod date timestamp */
static int vflag = 0;		/* verbosity level */
static int xflag = 0;		/* do not cross filesystem when recursing */

/*
 * -r mode related static variables
 */
static dev_t arg_dev;		/* device number of argument (for -x) */
struct skip {
    struct skip *next;	/* next skip string or NULL */
    char *path;		/* the path to skip */
    size_t len;		/* length of path */
};
static struct skip *skip = NULL;

/*
 * various special devices to avoid that might be found on various systems
 *
 * NOTE: We only set exists to 1 if the mount point exists AND the its device
 *	 number differs from the device number if the dirname.  I.e., only
 *	 if the mount point exists and the device number changes while going
 *	 into it.
 *
 * NOTE: This list need not be exhaustive, just enough to avoid taking a
 *	 long time walking into places where recursive checking for RCS
 *	 does not belong.
 */
struct avoid {		/* devices to avoid recursing into under -r */
    char *path;		/* mount point */
    int exists;		/* 0 ==> does not exist, 1 ==> exists, ignore */
    dev_t device;	/* mount point device number if exists == 1 */
};
static struct avoid avoid[] = {
    /* common */
    { "/proc", 0, 0 },
    { "/dev", 0, 0 },
    /* RedHat Linux */
    { "/sys", 0, 0 },
    { "/dev/pts", 0, 0 },
    { "/proc/bus/usb", 0, 0 },
    { "/dev/shm", 0, 0 },
    { "/proc/sys/fs/binfmt_misc", 0, 0 },
    /* Mac OS X */
    { "/.vol", 0, 0 },
    { "/Network", 0, 0 },
    { "/automount/Servers", 0, 0 },
    { "/automount/static", 0, 0 },
    /* FreeBSD */
    { "/var/named/dev", 0, 0 },
    /* misc */
    { "/afs", 0, 0 },
    /* end of list */
    { NULL, 0, 0 }
};
static int avoid_setup = 0;	/* avoid table has been setup */


/*
 * exit codes
 *
 * NOTE: Format errors are reserved for mal-formed ,v files that we can
 *	 read.  Access errors are reserved for not being able to read/access
 *	 files and directories.  Encountering a ,v inode that is not a
 *	 regular file or encountering an RCS inode that is not a directory
 *	 will not cause exitcode to change.
 */
#define EXIT_MASK_LOCK		0x01	/* set bit if lock found */
#define EXIT_MASK_MISSING	0x02	/* set bit if RCS missing file */
#define EXIT_MASK_DIFF		0x04	/* set bit if file and RCS differ */
#define EXIT_MASK_ACCESS	0x08	/* set bit if access errors */
#define EXIT_MASK_RPM		0x10	/* set bit *.rpm{orig,init,save,new} */
#define EXIT_FATAL		0x20	/* fatal error encountered */
static int exitcode = 0;		/* how we will/should exit */

static char *program;		/* our name */
static char *prog;		/* basename of program */

static void parse_args(int argc, char **argv);
static void process_arg(char *arg);
static void scan_rcsfile(char *filename, char *arg);
static void scan_rcsdir(char *dir, char *dir2, int recurse);
static size_t strendstr(char *str1, size_t *len1p, char *str2, size_t *len2p);
static char *rcs_2_pathname(char *rcsname);
static char *dir_2_rcsdir(char *dirname);
static char *file_2_filev(char *filename);
static char *filev_2_file(char *filename);
static char *pathname_2_rcs(char *pathname);
static char *skipblanks(char *cp);
static int check_rcs_hdr(char *p, char *msg, char *fname, char *end_rcs_header);
static int readrcs(char *f, struct stat *statPtr);
static char *base_name(char *path);
static char *dir_name(char *path);
static int ok_to_recurse(char *path, char *name, struct stat *sbuf);
static void avoid_init(void);
static void set_exitcode_mask(int mask);
static void dbg(int level, char *fmt, ...);
static void warn(char *warn1, char *warn2, int err);
static void fatal(char *msg1, char *msg2, int err);


/*
 * main - parse args, process args, exit with the proper exit code
 */
int
main(int argc, char *argv[])
{
    extern int optind;		/* argv index of the next arg */
    char *arg;			/* arg being processed */

    /* parse args */
    fclose(stdin);
    parse_args(argc, argv);
    argc -= optind;
    argv += optind;

    /*
     * process all the remaining files and dirs on the command line
     */
    arg = argv[0];
    do {
	/* process this argument (or the default RCS argument) */
	process_arg(arg);
    } while (--argc > 0 && (arg = (++argv)[0]) != NULL);

    /*
     * exit according to what was found and/or errors
     */
    dbg(1, "exit(%d)", exitcode);
    exit(exitcode);
}


/*
 * parse_args - parse and report on command line args
 */
static void
parse_args(int argc, char **argv)
{
    extern char *optarg;	/* option argument */
    extern int optind;		/* argv index of the next arg */
    struct skip *s;		/* new skip string */
    char *skippath;		/* resolved path to skip */
    int i;

    program = argv[0];
    prog = base_name(program);
    if (prog[0] == 'r') {
	/* if our program basename starts with r, then assume -p -r */
	pflag = 1;
	rflag = 1;
    }
    while ((i = getopt(argc, argv, "cdlmpqrRs:tv:xh")) != -1) {
	switch (i) {
	case 'c':
	    cflag = 1;
	    break;
	case 'd':
	    dflag = 1;
	    break;
	case 'l':
	    lflag = 1;
	    break;
	case 'm':
	    mflag = 1;
	    break;
	case 'p':
	    pflag = 1;
	    break;
	case 'q':
	    qflag = 1;
	    break;
	case 'r':
	    rflag = 1;
	    break;
	case 'R':
	    Rflag = 1;
	    break;
	case 's':
	    s = malloc(sizeof(struct skip));
	    if (s == NULL) {
		fatal("cannot allocate memory", "1", errno);
		/*NOTREACHED*/
	    }
	    skippath = malloc(PATH_MAX+1);
	    if (skippath == NULL) {
		fatal("cannot allocate memory", "2", errno);
		/*NOTREACHED*/
	    }
	    if (realpath(optarg, skippath) == NULL) {
		/* -s /skip does not exist, use only if it starts with / */
		skippath[0] = '\0';
		if (optarg[0] == '/') {
		    strcpy(skippath, optarg);
		}
	    }
	    /* use if we have an absolute skip path */
	    if (skippath[0] == '/') {
		s->next = skip;
		s->path = skippath;
		s->len = strlen(skippath);
		skip = s;
		pflag = 1;	/* -s implies -p */
	    } else {
		free(s);
		free(skippath);
		fatal(optarg, "if a -s arg does not start with /, "
			      "then it must exist", 0);
		/*NOTREACHED*/
	    }
	    break;
	case 't':
	    tflag = 1;
	    break;
	case 'v':
	    vflag = atoi(optarg);
	    break;
	case 'x':
	    xflag = 1;
	    break;
	case 'h':
	    hflag = 1;
	    /*FALLTHRU*/
	default:
	    fprintf(stderr,
	"usage: %s [-c] [-d] [-l] [-m] [-p] [-r] [-R] [-t] [-x] [-s /dir]...\n"
	    "\t\t[-h] [-v level] [path ...]\n"
	    "\t-c\t\tprint 1-word comment before each filename (def: don't)\n"
	    "\t-d\t\tnote when file and RCS differ (def: don't)\n"
	    "\t-h\t\tprint help and exit 0 (def: don't)\n"
	    "\t-l\t\tprint RCS lock information (def: don't)\n"
	    "\t-m\t\treport missing files under RCS control (def: don't)\n"
	    "\t-p\t\tprint absolute paths (def: don't unless using rcheck)\n"
	    "\t-q\t\tdo not report locked filenames (def: do)\n"
	    "\t-r\t\trecursive search (def: don't unless using rcheck)\n"
	    "\t-R\t\treport on *.rpm{orig,init,save,new} files (def: don't)\n"
	    "\t-s /dir\t\tskip dirs starting with /dir, sets -p (def: don't)\n"
	    "\t-t\t\tprint RCS modification timestamp (def: don't)\n"
	    "\t-x\t\tdo not cross filesystems when -r (def: do)\n"
	    "\t-v level\tdebugging level (def: 0)\n"
	    "exit 0 ==> all OK\n"
	    "exit bit 0 ==> locked file (1, 3, 5, 7, 9, 11, 13, 15,\n"
	    "                            17, 19, 21, 23, 25, 27, 29, 31)\n"
	    "exit bit 1 ==> -m & file not checked out (2-3, 6-7, 10-11, 14-15,\n"
	    "                                          18-19, 22-23, 26-27, 30-31)\n"
	    "exit bit 2 ==> -d and file different from RCS (8-15, 24-31)\n"
	    "exit bit 3 ==> -R and *.rpm{orig,init,save,new} found (16-31)\n"
	    "exit 32 ==> fatal error\n",
	    program);
	    dbg(1, "exit(%d)", hflag ? 0 : EXIT_FATAL);
	    exit(hflag ? 0 : EXIT_FATAL);
	}
    }
    /* NOTE: if no args, arg will be NULL which is OK for process_arg() */
    if (cflag) {
	dbg(1, "-c: print 1-word comment before each filename");
    }
    if (dflag) {
	dbg(1, "-d: note when file and RCS differ");
    }
    if (lflag) {
	dbg(1, "-l: print RCS lock information");
    }
    if (mflag) {
	dbg(1, "-m: report missing files under RCS control");
    }
    if (pflag) {
	dbg(1, "-p: print resolved absolute paths");
    }
    if (qflag) {
	dbg(1, "-q: do not report locked filenames");
    }
    if (rflag) {
	dbg(1, "-r: recursive search");
    }
    if (skip != NULL) {
    	for (s=skip; s != NULL; s = s->next) {
	    dbg(1, "-s: skip paths under: %s", s->path);
	}
    }
    if (tflag) {
	dbg(1, "-t: print RCS modification timestamp");
    }
    if (xflag) {
	dbg(1, "-x: do not cross filesystems when -r");
    }
    return;
}


/*
 * process_arg - process an argument looking for locked RCS files
 *
 * given:
 *	name	name of the directory or file to examine, NULL ==> .
 *
 * NOTE: This function will NOT alter exitcode if it encounters a
 *	 ,v file that is not a regular file nor will if if finds an
 *	RCS inode that is not a directory.  Format errors are reserved
 *	for mal-formed ,v files that we can read.  Access errors are
 *	reserved for not being able to read/access files and directories.
 */
static void
process_arg(char *arg)
{
    struct stat sbuf;	/* file status */
    struct stat rbuf;	/* RCS status */
    int statret;	/* 0 ==> arg exists, -1 ==> arg does not exist */
    size_t len;		/* arg length */
    char *modarg;	/* malloced and modified copy of arg */
    int hasrcs;		/* 0 ==> dir has RCS subdir, -1 ==> no RCS subdir */
    char *tmp;

    /*
     * firewall
     */
    if (arg == NULL) {
	/*
	 * When this program is not given args, the loop in main
	 * calls this function once with a NULL pointer.  We treat
	 * this special no arg case as if . were given.
	 */
	dbg(1, "arg was NULL");
	arg = ".";
    }
    if (arg[0] == '\0') {
	/* empty strings are considered . as well */
	dbg(1, "arg was empty");
	arg = ".";
    }
    dbg(1, "processing arg: %s", arg);

    /*
     * case: arg ends in ,v
     *
     * We must deal with the ending in ,v case first before considering
     * normal cases of directories and files because of the way RCS
     * tools process such ,v ending arguments.
     *
     * NOTE: I didn't make the rules below, the RCS tools did!
     */
    statret = stat(arg, &sbuf);
    arg_dev = (statret >= 0) ? sbuf.st_dev : 0;
    if (strendstr(arg, &len, ",v", NULL) > 0) {

	/*
	 * case: arg ends in ,v and has NO / in the path
	 *
	 * The RCS tools, when given just "foo,v" will first look for
	 * a RCS/foo,v file (even when ./foo,v exists).  If RCS/foo,v
	 * does not exist, then the RCS tool will look for the foo,v file.
	 *
	 * If we are processing "foo,v" and RCS/foo,v exists but is
	 * not a file, then the RCS tool report an error.  We consider
	 * foo to not be locked when RCS/foo,v exists and is not a file.
	 *
	 * If we are processing "foo,v" and RCS/foo,v does NOT exist,
	 * then "./foo,v is examined for locking.
	 */
	dbg(3, ",v arg processing");
	if (strchr(arg, '/') == NULL) {

	    /*
	     * case: arg ends in ,v and has NO / and RCS as a ,v file
	     *
	     * NOTE: We have to check the RCS sub-directory even when
	     * 	     the file, arg, exists.
	     */
	    tmp = filev_2_file(arg);
	    modarg = pathname_2_rcs(tmp);
	    hasrcs = stat(modarg, &rbuf);
	    if (hasrcs >= 0 && S_ISREG(rbuf.st_mode)) {
		/* found RCS/arg,v file, scan it */
		dbg(5, "RCS *,v file found: %s", modarg);
		scan_rcsfile(modarg, tmp);

	    /*
	     * case: arg ends in ,v and has NO / and RCS has a non-file ,v
	     *
	     * NOTE: The RCS tools will ignore arg (an existing ,v file)
	     *	     when they discover that the RCS ,v exists but is
	     *	     not a file.
	     */
	    } else if (hasrcs >= 0) {
		/* found RCS/arg,v but it is not a regular file, so no lock */
		dbg(5, "RCS *,v non-file found: %s", modarg);

	    /*
	     * case: arg ends in ,v and has NO /, RCS has no ,v, arg is a file
	     *
	     * NOTE: When given just "foo,v" and no RCS/foo,v exists,
	     *	     the RCS tools will look at "./foo,v".
	     */
	    } else if (statret >= 0 && S_ISREG(sbuf.st_mode)) {

		/* scan the ,v file directly */
		dbg(5, ". *,v file found: %s", arg);
		scan_rcsfile(arg, tmp);

	    /*
	     * case: arg ends in ,v and has NO /, no RCS ,v and no arg file
	     * case: arg ends in ,v and has NO /, no RCS ,v and arg not a file
	     */
	    } else {
		/* no ,v file, no lock */
		dbg(5, "no RCS *,v and no . *,v found");
	    }
	    free(tmp);
	    free(modarg);

	/*
	 * case: arg exists, ends in ,v and has a / in it, and is NOT a file
	 *
	 * If an RCS tool is given "stuff/foo,v" where the arg has a /
	 * in it, then only "stuff/foo,v" is examined.  Unlike the case
	 * where the arg as no /, RCS does not examine "stuff/RCS/foo,v".
	 *
	 * When the ,v arg is not a regular file, the RCS tool returns a
	 * error.  We consider these files to not be locked.
	 */
	} else if (statret >= 0 && !S_ISREG(sbuf.st_mode)) {
	    /* ,v is not an regular file, so it is not locked */
	    dbg(5, "RCS/*,v non-file found: %s", arg);

	/*
	 * case: arg is a ,v file, ends in ,v and has a / in it, and is a file
	 *
	 * Unlike the case of just "foo,v", the RCS tools only look at ./foo,v
	 * when given "./foo,v" for example.
	 */
	} else if (statret >= 0 && S_ISREG(sbuf.st_mode)) {

	    /* scan the ,v file directly */
	    dbg(5, "./*,v file found: %s", arg);
	    scan_rcsfile(arg, NULL);

	/*
	 * case: arg ends in ,v but does not exist
	 */
	} else {
	    char *tmp;

	    /*
	     * case: arg does not exist but the RCS subdir has the ,v
	     */
	    tmp = filev_2_file(arg);
	    modarg = pathname_2_rcs(tmp);
	    hasrcs = stat(modarg, &rbuf);
	    if (hasrcs >= 0 && S_ISREG(rbuf.st_mode)) {
		/* found arg,v file, scan it */
		dbg(5, "RCS/*,v file found: %s", modarg);
		scan_rcsfile(modarg, tmp);

	    /*
	     * case: arg does not exist, and no ,v file in RCS subdir
	     */
	    } else {
		/* no ,v file, no lock */
		dbg(5, "no RCS/*,v and no ./*,v found");
	    }
	    free(tmp);
	    free(modarg);
	}


    /*
     * case: arg does not end in ,v and is a directory (and exists)
     */
    } else if (statret >= 0 && S_ISDIR(sbuf.st_mode)) {

	/*
	 * case: arg is a directory and ends in RCS (or is just RCS)
	 */
	dbg(3, "directory arg processing");
	if (strendstr(arg, &len, "/RCS", NULL) > 0 || strcmp(arg, "RCS") == 0) {

	    /* arg is a RCS directory: only look for for ,v files in it */
	    dbg(5, "arg is an RCS directory: %s", arg);
	    scan_rcsdir(arg, NULL, 0);

	/*
	 * case: arg is a directory and the last path component is not RCS
	 */
	} else {

	    /*
	     * case: arg is a directory and an RCS sub-directory exists
	     */
	    modarg = dir_2_rcsdir(arg);
	    hasrcs = stat(modarg, &rbuf);
	    if (hasrcs >= 0 && S_ISDIR(rbuf.st_mode)) {

		/*
		 * First, only scan RCS sub-directory for ,v files,
		 * then scan the arg directory for ,v files that are
		 * not also under the RCS sub-directory.
		 *
		 * NOTE: If ./foo,v and ./RCS/foo,v both exist, then
		 *	 the RCS tools only process ./RCS/foo,v.  Even
		 *	 when ./RCS/foo,v is a regular file (e.g., a
		 *	 directory), the RCS tools ignore the ./foo.v file.
		 *
		 * The first call to scan_rcsdir() will scan the RCS
		 * sub-directory for ,v files.  The second call to
		 * scan_rcsdir() will ,v files not previously found
		 * in the RCS sub-directory.  If either (or both)
		 * contain locks, ret will be TRUE.
		 */
		dbg(5, "RCS directory found: %s", modarg);
		scan_rcsdir(modarg, arg, rflag);

	    /*
	     * case: arg is a directory without an RCS sub-directory
	     */
	    } else {
		/* just scan the arg directory for ,v files */
		dbg(5, "only arg directory found: %s", arg);
		scan_rcsdir(arg, NULL, rflag);
	    }
	    free(modarg);
	}

    /*
     * case: arg does not end in ,v and is not a directory
     */
    } else {

	/*
	 * case: arg does not end in ,v and RCS sub-dir has a ,v file
	 */
	dbg(3, "file arg processing");
	modarg = pathname_2_rcs(arg);
	hasrcs = stat(modarg, &rbuf);
	if (hasrcs >= 0 && S_ISREG(rbuf.st_mode)) {
	    /* found RCS ,v file, scan it */
	    dbg(5, "found RCS/*,v file: %s for arg: %s", modarg, arg);
	    scan_rcsfile(modarg, arg);

	/*
	 * case: arg does not end in ,v and the RCS ,v exists but is not a file
	 */
	} else if (hasrcs >= 0) {
	    /* ,v is not an regular file, so it is not locked */
	    dbg(5, "found RCS/*,v non-file: %s for arg: %s", modarg, arg);

	/*
	 * case: arg does not end in ,v and no RCS ,v
	 */
	} else {

	    /*
	     * case: arg does not end in ,v and no RCS ,v and arg,v is a file
	     */
	    free(modarg);
	    modarg = file_2_filev(arg);
	    hasrcs = stat(modarg, &rbuf);
	    if (hasrcs >= 0 && S_ISREG(rbuf.st_mode)) {
		/* found arg,v file, scan it */
		dbg(5, "found ./*,v file: %s for arg: %s", modarg, arg);
		scan_rcsfile(modarg, arg);

	    /*
	     * case: arg does not end in ,v and no RCS ,v and arg,v not a file
	     * case: arg does not end in ,v and no RCS ,v and arg,v not exist
	     */
	    } else {
		/* no ,v file (regular or missing), so no lock */
		dbg(5, "no *,v files found");
	    }
	}
	free(modarg);
    }
    return;
}


/*
 * scan_rcsfile - scan an RCS file for a lock
 *
 * This function prints lock results as per the -d and -l flags.
 *
 * If -m was given, missing files are reported if they are locked or not.
 * Unlocked missing files with -l report the locking user as :n/a:
 * and the revision as -1.
 *
 * If -q, then locked files are not reported, unless -m is also given
 * and the file is missing.
 *
 * given:
 *	filename	path/RCS/foo,v filename
 *	arg		path/foo arg to print if locked,
 *			    NULL ==> form from filename
 *
 * NOTE: If arg is NULL, this function will attempt to form the name to
 *	 print from the filename.
 */
static void
scan_rcsfile(char *filename, char *arg)
{
    struct stat sbuf;	/* filename status */
    struct stat fbuf;	/* non-RCS  status */
    int ret;		/* 0 ==> nothing to print, 1 ==> something to print */
    char resolved[PATH_MAX+1];	/* full pathname of a locked file */
    int missing;	/* 1 ==> RCS file not checked out */
    int need_nl = 0;	/* 1 ==> need some newline since we printed something */
    int free_arg = 0;	/* 1 ==> we need to free arg because we alloced it */
    pid_t pid;		/* child pid or 0 ==> is child or -1 ==> error */
    int code;		/* child exit code */
    int devnull_fd;	/* /dev/null file descriptor */

    /*
     * firewall
     */
    /* OK for arg to be NULL */
    if (filename == NULL) {
	fatal("scan_rcsfile", "passed NULL ptr", 0);
	/*NOTREACHED*/
    }
    dbg(2, "scanning file: %s", filename);

    /* compute RCS pathname if not already given */
    if (arg == NULL) {

	/* determine non-RCS name */
	arg = rcs_2_pathname(filename);
	free_arg = 1;
    }

    /*
     * see if the RCS file is locked
     */
    ret = readrcs(filename, &sbuf);
    dbg(3, "scan returned: %d", ret);

    /* determine if the file associated with the RCS file exists */
    if (stat(arg, &fbuf) < 0 || !S_ISREG(fbuf.st_mode)) {
	missing = 1;
	if (mflag) {
	    set_exitcode_mask(EXIT_MASK_MISSING);
        }
    } else {
	missing = 0;
    }

    /*
     * resolve absolute path if -p
     */
    resolved[0] = '\0';
    if (pflag) {

	/* try to resolve the argument */
	if (missing || realpath(arg, resolved) == NULL) {
	    char *missing_base;			/* basename of missing file */
	    char *missing_dir;			/* dirname of missing file */
	    char dir_resolved[PATH_MAX+1];	/* resolved dir of arg */

	    /* because file is missing, we must resolve the dirname */
	    missing_dir = dir_name(arg);
	    missing_base = base_name(arg);
	    if (realpath(missing_dir, dir_resolved) == NULL) {
		warn("cannot resolve missing dir", missing_dir, errno);
		set_exitcode_mask(EXIT_MASK_ACCESS);
		snprintf(resolved, PATH_MAX+1, "%s/%s",
			 strcmp(missing_dir, "/") == 0 ? "" : missing_dir,
			 missing_base);
	    } else {
		snprintf(resolved, PATH_MAX+1, "%s/%s",
			 strcmp(dir_resolved, "/") == 0 ? "" : dir_resolved,
			 missing_base);
	    }
	    free(missing_base);
	    free(missing_dir);
	}
    }

    /*
     * print results if locked
     */
    if (ret != 0) {

	/* print locked filename unless -q */
	if (!qflag) {

	    /* if -c, print 1-word comment */
	    if (cflag) {
		printf("locked\t");
	    }

	    /* print missing locked filename */
	    printf("%s", pflag ? resolved : arg);
	    need_nl = 1;
	}

	/* if -l, print owner and locked version */
	if (need_nl && lflag) {

	    /* be quiet about locked files if -q unless missing and -m */
	    printf("\t%s\t%s", owner, revision);
	}

	/* if -t, print RCS mod date unless -q or missing */
	if (need_nl && tflag) {
	    printf("\t%s", ctime(&(sbuf.st_mtime)));
	    /* ctime string ends in a newline */
	    need_nl = 0;
	    fflush(stdout);
	}
    }

    /*
     * not locked, look for missing files if -m
     */
    if (mflag && missing) {

	/* force newline if previous output */
	if (need_nl) {
	    putchar('\n');
	    need_nl = 0;
	}

	/* if -c, print 1-word comment */
	if (cflag) {
	    printf("gone\t");
	}

	/* print missing filename */
	printf("%s", pflag ? resolved : arg);
	need_nl = 1;

	/* if -l, print fake owner and locked version */
	if (lflag) {
	    printf("\t:n/a:\t-1");
	}

	/* if -t, print RCS mod date */
	if (tflag) {
	    printf("\t%s", ctime(&(sbuf.st_mtime)));
	    /* ctime string ends in a newline */
	    need_nl = 0;
	    fflush(stdout);
	}
    }

    /*
     * determine of file is different from top of RCS if -d
     */
    if (dflag && !missing) {

	/* force newline if previous output */
	if (need_nl) {
	    putchar('\n');
	    need_nl = 0;
	}
	fflush(stdout);

    	/*
	 * Use:
	 *
	 *	rcsdiff -q arg
	 *
	 * If the exit code is 0, there is no difference.  If the
	 * exit code is non-zero, then the file is different from
	 * the top RCS revision.
	 */
	errno = 0;
	pid = fork();
	switch (pid) {
	case 0:		/* child process */
	    /* seal off common standard I/O */
	    devnull_fd = open("/dev/null", 0);
	    if (devnull_fd < 0) {
		exit(EXIT_FATAL);	/* /dev/null failure */
	    }
	    if (dup2(devnull_fd, 1) < 0) {
		exit(EXIT_FATAL);	/* dup2(/dev/null, stdout) failure */
	    }
	    if (dup2(devnull_fd, 2) < 0) {
		exit(EXIT_FATAL);	/* dup2(/dev/null, stdout) failure */
	    }

	    /* launch rcsdiff */
	    execl("/usr/bin/rcsdiff", "/usr/bin/rcsdiff", "-q", arg, NULL);
	    exit(EXIT_FATAL);	/* exec faulure */
	    break;	/* end of child process */

	case -1:	/* fork error */
	    fatal("rcsdiff fork error", NULL, errno);
	    /*NOTREADCHED*/
	    break;
	}

	/*
	 * parent process - examine child exit code
	 */
	errno = 0;
        if (waitpid(pid, &code, 0) < 0) {
	    fatal("waitpid failed", NULL, errno);
	    /*NOTREACHED*/
	}
	if (!WIFEXITED(code)) {
	    fatal("waitpid returned but child did not exit", NULL, errno);
	    /*NOTREACHED*/
	}
	dbg(9, "exit code %d: rcsdiff -q %s", WEXITSTATUS(code), arg);
	switch (WEXITSTATUS(code)) {
	case 0:		/* file is the same */
	    dbg(9, "file same as RCS: %s", arg);
	    break;

	case EXIT_FATAL:	/* exec failed */
	    fatal("exec of rcsdiff failed", NULL, 0);
	    /*NOTREACHED*/
	    break;

	default:	/* file is different */
	    /* if -c, print 1-word comment */
	    if (cflag) {
		printf("diff\t");
	    }

	    /* print different filename */
	    printf("%s", pflag ? resolved : arg);
	    need_nl = 1;

	    /* if -l, print fake owner and locked version */
	    if (lflag) {
		printf("\t:n/a:\t-3");
	    }

	    /* if -t, print file mod date */
	    if (tflag) {
		printf("\t%s", ctime(&(fbuf.st_mtime)));
		/* ctime string ends in a newline */
		need_nl = 0;
		fflush(stdout);
	    }
	    break;
	}
    }

    /* cleanup */
    if (need_nl) {
	putchar('\n');
	fflush(stdout);
    }
    if (free_arg) {
	free(arg);
    }
    return;
}


/*
 * scan_rcsdir - scan a directory for RCS ,v files that may be locked
 *
 * given:
 *	dir1		directory to scan for ,v files
 *	dir2		if != NULL, ignore dir/,v files also found in subdir
 *	recurse		1 ==> recurse non-RCS subdirs, 0 ==> don't
 *
 * NOTE: This function may update exitcode based on errors.
 */
static void
scan_rcsdir(char *dir1, char *dir2, int recurse)
{
    DIR *d;		/* open directory */
    struct dirent *f;	/* file information for a directory entry */
    size_t flen;	/* length of filename referenced by f */
    struct stat fbuf;	/* file status of filename referenced by f */
    size_t dir1len;	/* length of the dir1 name */
    size_t dir2len;	/* length of the dir2 name */
    char *filename;	/* full path to filename referenced by f */
    int comma_v;	/* 1 ==> is a ,v filename */

    /*
     * firewall
     */
    /* it is OK if subdir is NULL */
    if (dir1 == NULL) {
	fatal("scan_rcsdir", "passed NULL ptr", 0);
	/*NOTREACHED*/
    }
    if (dir2 == NULL) {
	dbg(5, "scan_rcsdir(%s, NULL) with%s recursion",
		dir1, (recurse ? "" : "out"));
    } else {
	dbg(5, "scan_rcsdir(%s, %s) with%s recursion",
		dir1, dir2, (recurse ? "" : "out"));
    }

    /*
     * open the directory
     */
    dbg(9, "about to open directory: %s", dir1);
    if ((d = opendir(dir1)) == NULL) {
	/* no a directory, not accessible, etc. */
	warn("cannot open directory", dir1, errno);
	set_exitcode_mask(EXIT_MASK_ACCESS);
	return;
    }
    dir1len = strlen(dir1);

    /*
     * we always scan dir1 for ,v files, regardless of subdir
     */
    errno = 0;
    for (f = readdir(d); f != NULL; f = readdir(d)) {

	/*
	 * determine if we have a ,v file
	 */
	if (strendstr(f->d_name, &flen, ",v", NULL) <= 0) {
	    dbg(9, "2nd dir non-*,v name: %s", f->d_name);
	    comma_v = 0;
	} else {
	    dbg(9, "2nd dir *,v name: %s", f->d_name);
	    comma_v = 1;
	}

	/*
	 * form filename
	 */
	filename = malloc(dir1len + 1 + flen + 1);
	if (filename == NULL) {
	    fatal("cannot allocate memory", "3", errno);
	    /*NOTREACHED*/
	}
	snprintf(filename, dir1len + 1 + flen + 1, "%s/%s",
	    strcmp(dir1, "/") == 0 ? "" : dir1, f->d_name);

	/*
	 * ignore if not a regular file that is readable
	 */
	if (stat(filename, &fbuf) < 0) {
	    dbg(9, "ignoring vanished name: %s", filename);
	    free(filename);
	    errno = 0;
	    continue;
	}

	/*
	 * report if we have a *.rpm{orig,init,save,new} file and -R
	 */
	if (Rflag && (strendstr(f->d_name, &flen, ".rpmorig", NULL) > 0 ||
		      strendstr(f->d_name, &flen, ".rpminit", NULL) > 0 ||
		      strendstr(f->d_name, &flen, ".rpmsave", NULL) > 0 ||
		      strendstr(f->d_name, &flen, ".rpmnew", NULL) > 0)) {

	    char resolved[PATH_MAX+1];	/* resolved filename */

	    /* note in exitcode */
	    set_exitcode_mask(EXIT_MASK_RPM);

	    /* if -c, print 1-word comment */
	    if (cflag) {
		printf("rpm\t");
	    }

	    /* print missing filename */
	    if (pflag && realpath(filename, resolved) != NULL) {
		printf("%s", resolved);
	    } else {
		printf("%s", filename);
	    }

	    /* if -l, print fake owner and locked version */
	    if (lflag) {
		printf("\t:n/a:\t-2");
	    }

	    /* if -t, print RCS mod date */
	    if (tflag) {
		printf("\t%s", ctime(&(fbuf.st_mtime)));
		/* ctime string ends in a newline */
	    } else {
	    	putchar('\n');
	    }
	    fflush(stdout);
	    free(filename);
	    errno = 0;
	    continue;
	}

	/*
	 * if a non-RCS directory and we are recursing, recurse
	 *
	 * NOTE: We do not recurse into or under an RCS directory.
	 *	 We also do not recurse into . and .. directory.
	 *
	 * NOTE: If dir2 is non-NULL, then this means dir1 is an
	 *	 RCS directory and thus we do not recurse.
	 */
	if (rflag && dir2 == NULL && S_ISDIR(fbuf.st_mode)) {

	    /* determine if it is OK to recuse into this sub-directory */
	    if (!ok_to_recurse(filename, f->d_name, &fbuf)) {
		free(filename);
		errno = 0;
		continue;
	    }
	    dbg(9, "recursing on non-RCS dir: %s", filename);

	    /* recurse on the sub-directory */
	    process_arg(filename);
	    dbg(9, "returned after recursion to %s", dir2);
	    free(filename);
	    errno = 0;
	    continue;
	}

	/*
	 * not a recursion directory and not a ,v file
	 */
	if (comma_v == 0) {
	    errno = 0;
	    dbg(9, "ignoring non-*,v name: %s", f->d_name);
	    continue;
	}

	/*
	 * ignore if not a regular file that is readable
	 */
	if (!S_ISREG(fbuf.st_mode)) {
	    dbg(9, "ignoring non-file: %s", filename);
	    free(filename);
	    errno = 0;
	    continue;
	}
	if (access(filename, R_OK) != 0) {
	    dbg(9, "ignoring non-readable file *,v: %s", filename);
	    free(filename);
	    errno = 0;
	    continue;
	}

	/*
	 * scan the ,v file for locks and print as needed
	 */
	dbg(9, "will scan *,v file: %s", filename);
	(void) scan_rcsfile(filename, NULL);
	free(filename);
	errno = 0;
    }
    if (errno != 0) {
	fatal("readdir error on", dir1, errno);
    }
    if (closedir(d) < 0) {
	/* directory error, stop with what was found */
	fatal("cannot close directory", dir1, errno);
	/*NOTREACHED*/
    }
    dbg(9, "closed directory: %s", dir1);

    /*
     * If we were given a 2nd directory, then we need to scan that
     * directory for ,v files that are not found in the 1st directory.
     *
     * A typical example:
     *
     *		scan_rcsdir("/foo/bar/RCS", "/foo/bar", recurse);
     *
     * We have just scanned /foo/bar/RCS for ,v files.  Now we need
     * to scan /foo/bar for ,v files not found under /foo/bar/RCS.
     * The RCS tools will ignore /foo/bar/baz,v if /foo/bar/RCS/baz,v
     * exists.
     */
    if (dir2 != NULL) {

	/*
	 * open the 2nd directory
	 */
	dbg(9, "about to open 2nd directory: %s", dir2);
	if ((d = opendir(dir2)) == NULL) {
	    /* no a directory, not accessible, etc. */
	    warn("cannot open directory", dir2, errno);
	    set_exitcode_mask(EXIT_MASK_ACCESS);
	    return;
	}
	dir2len = strlen(dir2);

	/*
	 * scan dir2 for ,v files while looking for them in dir1
	 *
	 * NOTE: Yes, somebody could create dir1/foo,v between the time
	 *	 we finish scanning dir1 and now causing us to skip
	 *	 dir2/foo,v.  But so what: the RCS tools will skip
	 *	 dir2/foo,v as well.  All we do is make a best effort
	 *	 to look for files that are locked when we view them.
	 */
	errno = 0;
	for (f = readdir(d); f != NULL; f = readdir(d)) {

	    /*
	     * determine if we have a ,v file
	     */
	    if (strendstr(f->d_name, &flen, ",v", NULL) <= 0) {
		dbg(9, "2nd dir non-*,v name: %s", f->d_name);
		comma_v = 0;
	    } else {
		dbg(9, "2nd dir *,v name: %s", f->d_name);
		comma_v = 1;
	    }

	    /*
	     * ignore file foo,v file and dir1/foo,v exists
	     */
	    if (comma_v) {

		/*
		 * form filename as it might exist in dir1
		 */
		filename = malloc(dir1len + 1 + flen + 1);
		if (filename == NULL) {
		    fatal("cannot allocate memory", "4", errno);
		    /*NOTREACHED*/
		}
		snprintf(filename, dir1len + 1 + flen + 1, "%s/%s",
			 strcmp(dir1, "/") == 0 ? "" : dir1, f->d_name);

		/*
		 * ignore ,v file if it exists in dir1
		 */
		if (access(filename, F_OK) == 0) {
		    /* dir1/foo,v exists, ignore dir2/foo,v */
		    free(filename);
		    dbg(9, "ignoring in 2nd dir, 1st dir *,v found: %s",
		    	f->d_name);
		    errno = 0;
		    continue;
		}
		free(filename);
		errno = 0;
	    }

	    /*
	     * form filename in dir2
	     */
	    filename = malloc(dir2len + 1 + flen + 1);
	    if (filename == NULL) {
		fatal("cannot allocate memory", "5", errno);
		/*NOTREACHED*/
	    }
	    snprintf(filename, dir2len + 1 + flen + 1, "%s/%s",
		     strcmp(dir2, "/") == 0 ? "" : dir2, f->d_name);

	    /*
	     * if a non-RCS directory and we are recursing, recurse
	     *
	     * NOTE: We do not recurse into or under an RCS directory.
	     *	     We also do not recurse into . and .. directory.
	     */
	    if (stat(filename, &fbuf) < 0) {
		dbg(9, "ignoring 2nd dir vanished name: %s", filename);
		free(filename);
		errno = 0;
		continue;
	    }
	    if (rflag && S_ISDIR(fbuf.st_mode)) {

		/* determine if it is OK to recuse into this sub-directory */
		if (!ok_to_recurse(filename, f->d_name, &fbuf)) {
		    free(filename);
		    errno = 0;
		    continue;
		}
		dbg(9, "recursing on non-RCS dir: %s", filename);

		/* recurse on the sub-directory */
		process_arg(filename);
		dbg(9, "returned after recursion to %s", dir2);
		free(filename);
		errno = 0;
		continue;
	    }

	    /*
	     * not a recursion directory and not a ,v file
	     */
	    if (comma_v == 0) {
		dbg(9, "ignoring 2nd dir non-*,v name: %s", f->d_name);
		errno = 0;
		continue;
	    }

	    /*
	     * ignore if not a regular file that is readable
	     */
	    if (!S_ISREG(fbuf.st_mode)) {
		dbg(9, "ignoring 2nd dir non-file: %s", filename);
		free(filename);
		errno = 0;
		continue;
	    }
	    if (access(filename, R_OK) != 0) {
		dbg(9, "ignoring 2nd dir non-readable file *,v: %s", filename);
		free(filename);
		errno = 0;
		continue;
	    }

	    /*
	     * scan the ,v file for locks and print as needed
	     */
	    dbg(9, "will scan 2nd dir *,v file: %s", filename);
	    (void) scan_rcsfile(filename, NULL);
	    free(filename);
	    errno = 0;
	}
	if (errno != 0) {
	    fatal("readdir error on", dir2, errno);
	}
	if (closedir(d) < 0) {
	    /* directory error, stop with what was found */
	    fatal("cannot close directory", dir2, errno);
	    /*NOTREACHED*/
	}
	dbg(9, "closed 2nd directory: %s", dir2);
    }
    return;
}


/*
 * strndup - like strdup() but with a limited copy length
 */
static char *
strndup(const char *s1, const size_t sz)
{
    char *s2;

    if ((s2 = malloc((unsigned) sz + 1)) == NULL) {
	fatal("cannot allocate memory", "6", errno);
	/*NOTREACHED*/
    }
    strncpy(s2, s1, sz);
    s2[sz] = '\0';
    return s2;
}


/*
 * strendstr - determine if a string end matches a string
 *
 * given:
 *	str1	look at the end of this string
 *	len1p	where to save length of str1 or, NULL ==> don't save length
 *	str2	potential end string match
 *	len2p	where to save length of str2 or, NULL ==> don't save length
 *
 * returns:
 *	0 ==> str2 is not at the end of string, str{1,2} empty or NULL
 *	>0 ==> str2 is at the end of str1, returns str2 length
 */
static size_t
strendstr(char *str1, size_t *len1p, char *str2, size_t *len2p)
{
    size_t len1;	/* length of str1 */
    size_t len2;	/* length of str2 */

    /*
     * firewall - check for bogus strings
     */
    if (str1 == NULL || str2 == NULL) {
	return 0;
    }

    /*
     * get lengths
     */
    len1 = strlen(str1);
    len2 = strlen(str2);
    /* save lengths if requested */
    if (len1p != NULL) {
	*len1p = len1;
    }
    if (len2p != NULL) {
	*len2p = len2;
    }

    /*
     * sanity check: str2 cannot be longer than str1
     */
    if (len2 > len1) {
	/* str2 too long to be at end of str1 */
	return 0;
    }

    /*
     * check for end of string match
     */
    if (strncmp(str1+len1-len2, str2, len2) == 0) {
	/* found str2 at end of str2 */
	return len2;
    }
    /* str2 not at end of str1 */
    return 0;
}


/*
 * rcs_2_pathname - convert path/RCS/filename,v name into path/filename
 *
 * This function will return a path to the regular filename without
 * any final /RCS (if at end of dirpath) or trailing ,v (if found).
 *
 * given:
 *	rcsname		RCS,v name
 *
 * returns:
 *	malloced pathname without the /RCS or ,v
 */
static char *
rcs_2_pathname(char *rcsname)
{
    char *dir;		/* directory path of rcsname */
    char *base;		/* basename of rcsname */
    char *real;		/* fill regular filename to return */
    size_t dirlen;	/* length of dirname */
    size_t baselen;	/* length of basename */

    /* firewall */
    if (rcsname == NULL) {
	fatal("rcs_2_pathname", "passed NULL ptr", 0);
	/*NOTREACHED*/
    } else if (rcsname[0] == '\0') {
	/* empty string returns . by convention */
	real = strdup(".");
	if (real == NULL) {
	    fatal("cannot allocate memory", "7", errno);
	    /*NOTREACHED*/
	}
	dbg(7, "rcs_2_pathname empty string forced return: %s", real);
	return real;
    }

    /*
     * form directory without trailing /RCS
     */
    if (strchr(rcsname, '/') == NULL) {
	/* no / in path, use empty directory component (instead of .) */
	dirlen = 0;
	dir = NULL;
    } else {
	/* strip off any trailing RCS */
	dir = dir_name(rcsname);
	if (strendstr(dir, &dirlen, "RCS", NULL) > 0) {
	    /* remove trailing RCS */
	    dirlen -= 3;
	    dir[dirlen] = '\0';
	    /* deal with any trailing / */
	    if (dirlen > 0 && dir[dirlen-1] == '/') {
	    	--dirlen;
		dir[dirlen] = '\0';
	    /* if we had just RCS/file,v, assume empty directory path */
	    } else if (dir[0] == '\0') {
		/* as if we had no / in path, use empty directory component */
		free(dir);
		dirlen = 0;
		dir = NULL;
	    }
	}
    }

    /*
     * form basename without ,v
     */
    /* strip off any trailing ,v */
    base = base_name(rcsname);
    if (strendstr(base, &baselen, ",v", NULL) > 0) {
	baselen -= 2;
	base[baselen] = '\0';
    }
    /* if we now have and empty basename, assume "." */
    if (baselen <= 0) {
	strcpy(base, ".");
	baselen = 1;
    }

    /*
     * form the fill path of the real filename
     */
    if (dirlen <= 0 || dir == NULL) {
	/* no directory part, just return new base */
	dbg(11, "rcs_2_pathname path %s: returned: %s", rcsname, base);
	return base;
    }
    /* combine directory and filename */
    real = malloc(dirlen + 1 + baselen + 1);
    if (real == NULL) {
	fatal("cannot allocate memory", "8", errno);
	/*NOTREACHED*/
    }
    snprintf(real, dirlen + 1 + baselen + 1, "%s/%s",
	    strcmp(dir, "/") == 0 ? "" : dir, base);
    free(dir);
    free(base);
    dbg(11, "rcs_2_pathname path %s: is %s", rcsname, real);
    return real;
}


/*
 * dir_2_rcsdir - convert a dirpath into a dirpath/RCS
 *
 * given:
 *	dirname		path to a directory
 *
 * returns:
 *	dirname/RCS as a malloced string
 */
static char *
dir_2_rcsdir(char *dirname)
{
    char *rcs = NULL;	/* malloced directory/RCS to return */
    int dirlen;		/* length of dirname */

    /* firewall */
    if (dirname == NULL) {
	fatal("dir_2_rcsdir", "passed NULL ptr", 0);
	/*NOTREACHED*/

    /* empty dir or just '.' returns RCS by convention */
    } else if (dirname[0] == '\0' || strcmp(dirname, ".") == 0) {
	rcs = strdup("RCS");
	if (rcs == NULL) {
	    fatal("cannot allocate memory", "9", errno);
	    /*NOTREACHED*/
	}

    /* form dirpath/RCS */
    } else {
	dirlen = strlen(dirname);
	rcs = malloc(dirlen + sizeof("/RCS"));
	if (rcs == NULL) {
	    fatal("cannot allocate memory", "10", errno);
	    /*NOTREACHED*/
	}
	snprintf(rcs, dirlen + sizeof("/RCS"), "%s/RCS",
	    strcmp(dirname, "/") == 0 ? "" : dirname);
    }

    /* return malloced dirname/RCS */
    return rcs;
}


/*
 * file_2_filev - convert file into a file,v
 *
 * given:
 *	filename	name of a file
 *
 * returns:
 *	filename,v as a malloced string
 */
static char *
file_2_filev(char *filename)
{
    char *ret;		/* filename,v to return */
    int len;		/* filename string length */

    /* firewall */
    if (filename == NULL) {
	fatal("file_2_filev", "passed NULL ptr", 0);
	/*NOTREACHED*/
    }

    /*
     * allocate filename,v
     */
    len = strlen(filename);
    ret = malloc(len + sizeof(",v"));
    if (ret == NULL) {
	fatal("cannot allocate memory", "11", errno);
	/*NOTREACHED*/
    }
    snprintf(ret, len + sizeof(",v"), "%s,v", filename);

    /* return result */
    return ret;
}


/*
 * filev_2_file - convert file,v into a file
 *
 * given:
 *	filename,v	name of a file
 *
 * returns:
 *	filename as a malloced string
 *
 * NOTE: If filename does not end in ,v then this function will return
 *	 the arg, as a newly malloced string, unmodified.
 */
static char *
filev_2_file(char *filename)
{
    char *ret;		/* filename,v to return */
    int len;		/* filename string length */

    /* firewall */
    if (filename == NULL) {
	fatal("filev_2_file", "passed NULL ptr", 0);
	/*NOTREACHED*/
    }

    /*
     * allocate filename
     */
    ret = strdup(filename);
    if (ret == NULL) {
	fatal("cannot allocate memory", "12", errno);
	/*NOTREACHED*/
    }

    /*
     * trim off ,v if found on the end
     */
    if (strendstr(ret, &len, ",v", NULL) > 0) {
	ret[len-2] = '\0';
    }

    /* return result */
    return ret;
}


/*
 * pathname_2_rcs - convert a path/file into path/RCS/file,v RCS name
 */
static char *
pathname_2_rcs(char *pathname)
{
    char *base;		/* basename of path */
    char *basev;	/* basename of path with a trailing ,v */
    char *dir;		/* directory path of pathname */
    char *rcsdir;	/* directory path of pathname with a trailing /RCS */
    char *rcs;		/* malloced path/RCS/file,v to return */
    int dirlen;		/* length of dirname */
    int baselen;	/* length of basename */

    /* firewall */
    if (pathname == NULL) {
	fatal("pathname_2_rcs", "passed NULL ptr", 0);
	/*NOTREACHED*/
    } else if (pathname[0] == '\0') {
	/* empty string returns RCS by convention */
	rcs = strdup("RCS");
	if (rcs == NULL) {
	    fatal("cannot allocate memory", "13", errno);
	    /*NOTREACHED*/
	}
	return rcs;
    }

    /*
     * form directory with trailing /RCS
     */
    if (strchr(pathname, '/') == NULL) {
	/* no / in path, assume directory of just RCS */
	rcsdir = strdup("RCS");
	if (rcsdir == NULL) {
	    fatal("cannot allocate memory", "14", errno);
	    /*NOTREACHED*/
	}
    } else {
	/* add /RCS */
	dir = dir_name(pathname);
	rcsdir = dir_2_rcsdir(dir);
	free(dir);
    }
    dirlen = strlen(rcsdir);

    /*
     * form basename with ,v
     */
    /* get basename */
    base = base_name(pathname);
    basev = file_2_filev(base);
    baselen = strlen(basev);
    free(base);

    /*
     * combine dir/RCS and file,v
     */
    rcs = malloc(dirlen + 1 + baselen + 1);
    if (rcs == NULL) {
	fatal("cannot allocate memory", "15", errno);
	/*NOTREACHED*/
    }
    snprintf(rcs, dirlen + 1 + baselen + 1, "%s/%s",
	    strcmp(rcsdir, "/") == 0 ? "" : rcsdir, basev);
    free(rcsdir);
    free(basev);

    /* return result */
    return rcs;
}


/*
 * skipblanks - skip until end of whitespace or string
 *
 * given:
 *	cp	string pointer
 *
 * returns:
 *	after whitespace or at end if string
 */
static char *
skipblanks(char *cp)
{
    while (isspace(*cp))
	cp++;
    return cp;
}


/*
 * check_rcs_hdr - sanity check on RCS header parsing
 *
 * given:
 *	p	pointer into the RCS header, or NULL
 *	msg	warning message to print if a problem (1st arg to warn())
 *	fname	warning filename (2nd arg to warn())
 *	end_rcs_header	end of RCS header buffer
 *
 * returns:
 *	-1	pointer is not in buffer or beyond end of buffer
 *	0	all is OK
 */
static int
check_rcs_hdr(char *p, char *msg, char *fname, char *end_rcs_header)
{
    if (p == NULL || p > end_rcs_header) {
	warn(msg, fname, 0);
	return -1;
    }
    return 0;
}


/*
 * readrcs - read an RCS,v file and determine if it is locked
 *
 * given:
 *	f	name of RCS,v file
 *	statPtr	pointer to a struct stat
 *
 * returns:
 *	0 ==> not locked or unable to determine if is locked or not an RCS file
 *	1 ==> looks like an RCS,v file and is locked
 *
 * NOTE: This function was originally written by Kipp Hickman,
 *	 code cleanup and file closing by Landon Curt Noll.
 *
 * NOTE: This function may modify exitcode according to locks found and
 *	 access errors.
 */
static int
readrcs(char *f, struct stat *statPtr)
{
    char *end_rcs_header;
    char *fname;
    int fd = -1;
    char *owner_str, *revision_str;
    char *buf = (char *) (-1), *p, *q;
    char *lockp;

    /*
     * map the RCS file into memory
     */
    fname = f;
    dbg(5, "read RCS on: %s", f);
    if ((fd = open(fname, O_RDONLY)) < 0) {
	warn("cannot open RCS file", fname, errno);
	set_exitcode_mask(EXIT_MASK_ACCESS);
	return 0;
    }
    if (fstat(fd, statPtr) < 0) {
	warn("cannot stat RCS file", fname, errno);
	close(fd);
	set_exitcode_mask(EXIT_MASK_ACCESS);
	return 0;
    }
    if (statPtr->st_size == 0) {
	warn("zero length RCS file", fname, 0);
	close(fd);
	set_exitcode_mask(EXIT_MASK_ACCESS);
	return 0;
    }
    buf = (char *) mmap((void *) 0, statPtr->st_size, PROT_READ | PROT_WRITE,
		        MAP_PRIVATE, fd, 0);
    if (buf == (char *) (-1)) {
	warn("cannot mmap RCS file", fname, 0);
	close(fd);
	set_exitcode_mask(EXIT_MASK_ACCESS);
	return 0;
    }

    /*
     * RCS sanity check, must start a head string
     */
    if (strncmp(buf, "head", sizeof("head") - 1) != 0) {
	warn("malformed RCS file, missing head", fname, 0);
	close(fd);
	set_exitcode_mask(EXIT_MASK_ACCESS);
	return 0;
    }

    /*
     * RCS sanity check: must have a desc section
     *
     * The strstr calls presume that there is at least one NUL character
     * following all the valid data.  The mmap gives us this, except in
     * the case that the mapped file ends exactly on a page boundary.
     * In that case we overwrite the last character in the file with a NUL.
     *
     * This slight disregard for the file's data is actually user visible:
     * Truncate an RCS ,v file so that it ends with "\ndesc\n".  If the file
     * is any size other than a page multiple, this code will think the header
     * is fine, ignore the truncation, and find what it wants.  If the file
     * happens to end up an exact page multiple, this code will write a NUL
     * over the final "\n", then be unable to find end_rcs_header, and complain
     * that the file has "no desc line".  Who cares ...
     */
    if ((statPtr->st_size % getpagesize()) == 0)
	buf[statPtr->st_size - 1] = '\0';

    if ((end_rcs_header = strstr(buf, "\ndesc\n")) == NULL) {
	warn("malformed RCS file, no desc section", fname, 0);
	close(fd);
	return 0;
    }

    /*
     * RCS sanity check: inspect the head section is more detail
     */
    p = buf + sizeof("head") - 1;
    p = skipblanks(p);
    if (check_rcs_hdr(p, "malformed RCS file, invalid head line",
    		      fname, end_rcs_header)) {
	close(fd);
	set_exitcode_mask(EXIT_MASK_ACCESS);
	return 0;
    }
    q = strchr(p, ';');
    if (check_rcs_hdr(q, "malformed RCS file, invalid head format",
    		      fname, end_rcs_header)) {
	close(fd);
	set_exitcode_mask(EXIT_MASK_ACCESS);
	return 0;
    }

    /*
     * RCS sanity check: inspect the locks section
     */
    p = strstr(p, "\nlocks");
    if (check_rcs_hdr(p, "malformed RCS file, missing locks section",
    		      fname, end_rcs_header)) {
	close(fd);
	set_exitcode_mask(EXIT_MASK_ACCESS);
	return 0;
    }
    p += sizeof("\nlocks") - 1;
    p = skipblanks(p);
    if (check_rcs_hdr(p, "malformed RCS file, invalid locks section",
    		      fname, end_rcs_header)) {
	close(fd);
	set_exitcode_mask(EXIT_MASK_ACCESS);
	return 0;
    }
    q = strchr(p, ';');
    if (check_rcs_hdr(q, "malformed RCS file, invalid locks format",
    		      fname, end_rcs_header)) {
	close(fd);
	set_exitcode_mask(EXIT_MASK_ACCESS);
	return 0;
    }

    /*
     * grab a copy of the the locks section
     */
    lockp = strndup(p, q - p);
    if (buf != (char *) (-1)) {
	if (munmap(buf, statPtr->st_size) < 0) {
	    fatal("cannot munmap", fname, 0);
	    /*NOTREACHED*/
	}
    }

    /*
     * looks like an RCS,v file that is locked - gather lock info
     *
     * If there is no owner or RCS lock version, then the file is
     * not locked.
     */
    owner[0] = 0;
    revision[0] = 0;
    owner_str = strtok(lockp, ":");
    if (owner_str != NULL) {
	revision_str = strtok(NULL, ";\n\t");
    }
    if (owner_str == NULL || revision_str == NULL) {
	close(fd);
	/* not RCS locked */
	dbg(5, "unlocked RCS file: %s", f);
	return 0;
    }

    /*
     * save owner and revision in the global area
     */
    strncpy(owner, owner_str, sizeof(owner));
    strncpy(revision, revision_str, sizeof(revision));
    owner[sizeof(owner) - 1] = 0;
    revision[sizeof(revision) - 1] = 0;

    /*
     * report that the RCS,v file is locked
     */
    close(fd);
    set_exitcode_mask(EXIT_MASK_LOCK);
    dbg(5, "locked RCS file: %s", f);
    return 1;
}


/*
 * base_name - return a malloced basename of a string
 *
 * given:
 *	path	a string
 *
 * returns:
 *	malloced basename of path
 *
 * NOTE: The path argument is not modified by this call whereas
 *	 calling basename() directly does.
 *
 * NOTE: This function does not return on error and will not return NULL.
 */
static char *
base_name(char *path)
{
    char *path_dup;	/* copy of the path argument */
    char *base;		/* basename of path_dup */
    char *ret;		/* allocated basename to return */

    /*
     * firewall
     */
    if (path == NULL) {
    fatal("base_name", "passed NULL ptr", 0);
    /*NOTREACHED*/
}

/*
 * duplicate path so that basename() will not modify the path argument
 */
errno = 0;
path_dup = strdup(path);
if (path_dup == NULL) {
	fatal("cannot allocate memory", "16", errno);
	/*NOTREACHED*/
    }

    /*
     * compute basename
     */
    errno = 0;
    base = basename(path_dup);
    if (base == NULL) {
	fatal("cannot allocate memory", "17", errno);
	/*NOTREACHED*/
    }

    /*
     * duplicate basename to return
     */
    errno = 0;
    ret = strdup(base);
    if (ret == NULL) {
	fatal("cannot allocate memory", "18", errno);
	/*NOTREACHED*/
    }
    free(path_dup);
    return ret;
}


/*
 * dir_name - return a malloced dirname of a string
 *
 * given:
 *	path	a string
 *
 * returns:
 *	malloced dirname of path
 *
 * NOTE: The path argument is not modified by this call whereas
 *	 calling dirname() directly does.
 *
 * NOTE: This function does not return on error and will not return NULL.
 */
static char *
dir_name(char *path)
{
    char *path_dup;	/* copy of the path argument */
    char *dir;		/* dirname of path_dup */
    char *ret;		/* allocated dirname to return */

    /*
     * firewall
     */
    if (path == NULL) {
	fatal("dir_name", "passed NULL ptr", 0);
	/*NOTREACHED*/
    }

    /*
     * duplicate path so that dirname() will not modify the path argument
     */
    errno = 0;
    path_dup = strdup(path);
    if (path_dup == NULL) {
	fatal("cannot allocate memory", "19", errno);
	/*NOTREACHED*/
    }

    /*
     * compute dirname
     */
    errno = 0;
    dir = dirname(path_dup);
    if (dir == NULL) {
	fatal("cannot allocate memory", "20", errno);
	/*NOTREACHED*/
    }

    /*
     * duplicate dirname to return
     */
    errno = 0;
    ret = strdup(dir);
    if (ret == NULL) {
	fatal("cannot allocate memory", "21", errno);
	/*NOTREACHED*/
    }
    free(path_dup);
    return ret;
}


/*
 * ok_to_recurse - determine if a path is OK to recurse into
 */
static int
ok_to_recurse(char *path, char *name, struct stat *sbuf)
{
    struct stat lbuf;	/* stat of a symlink */
    struct avoid *p;	/* avoid table pointer */
    struct skip *s;	/* skip record */
    char fullpath[PATH_MAX+1];	/* full path we are recursing into */

    /* initialize the mount point avoid table */
    if (avoid_setup == 0) {
	avoid_init();
    }

    /* do not recuse into RCS, . or .. */
    if (strcmp(name, "RCS") == 0) {
	dbg(9, "will not recurse under RCS: %s", path);
	return 0;
    }
    if (strcmp(name, ".") == 0) {
	dbg(9, "will not recurse into .: %s", path);
	return 0;
    }
    if (strcmp(name, "..") == 0) {
	dbg(9, "will not recurse into ..: %s", path);
	return 0;
    }

    /* determine if -x allows us to recurse */
    if (xflag && (sbuf->st_dev != arg_dev)) {
	dbg(7, "will not recurse into another filesystem: %s", path);
	return 0;
    }

    /* do not follow symlinks to other directories */
    if (lstat(path, &lbuf) < 0) {
	dbg(7, "ignoring lstat vanished name: %s", path);
	return 0;
    }
    if (S_ISLNK(lbuf.st_mode)) {
	dbg(9, "will not recurse into directory symlink: %s", path);
	return 0;
    }

    /* scan the avoid table looking for filesystems to avoid */
    for (p = &avoid[0]; p->path != NULL; ++p) {

	/* ignore filesystems that do not exist */
	if (! p->exists) {
	    continue;
	}

	/* ignore if device is a a filesystem to avoid */
	if (sbuf->st_dev == p->device) {
	    dbg(7, "will not recurse under %s: %s", p->path, path);
	    return 0;
	}
    }

    /* do not recurse into paths blocked by -s /dir */
    if (skip != NULL && realpath(path, fullpath) != NULL) {
	for (s=skip; s != NULL; s = s->next) {

	    /* look for leading match */
	    if (strncmp(s->path, fullpath, s->len) == 0) {

		/* do not recurse on exact match or path component match */
		if (path[s->len] == '\0' || path[s->len] == '/') {
		    dbg(3, "will not recurse under %s", fullpath);
		    dbg(3, "\tdue to -s %s", s->path);
		    return 0;
		}
	    }
	}
    }

    /* OK to recuse */
    return 1;
}


/*
 * avoid_init - initialize the avoid array
 */
static void
avoid_init(void)
{
    struct avoid *p;	/* avoid table pointer */
    struct stat mbuf;	/* status of path */
    struct stat pbuf;	/* status of parent directory of path */
    char *parent;	/* parent directory of path */

    /*
     * process each table entry
     */
    for (p = &avoid[0]; p->path != NULL; ++p) {

	/* ignore if path does not exist */
	if (stat(p->path, &mbuf) < 0) {
	    /* path does not exist, leave entry as ignore */
	    dbg(9, "%s: does not exist, no need to avoid", p->path);
	    continue;
	}

	/* determine parent directory of path */
	parent = dir_name(p->path);
	if (stat(parent, &pbuf) < 0) {
	    /* parent does not exist, leave entry as ignore */
	    dbg(9, "%s: parent: %s does not exist, no need to avoid",
	    	p->path, parent);
	    free(parent);
	    parent = NULL;
	    continue;
	}

	/* ignore if patent and path are same device */
	if (pbuf.st_dev == mbuf.st_dev) {
	    /* parent same device as path, not a mount point */
	    dbg(9, "%s: parent: %s has same device number, no need to avoid",
	    	p->path, parent);
	    free(parent);
	    parent = NULL;
	    continue;
	}
	free(parent);
	parent = NULL;

	/* avoid path, it is a mount point */
	p->exists = 1;
	p->device = mbuf.st_dev;
	dbg(8, "%s: will avoid this mount point under -r", p->path);
    }

    /* avoid table is ready now */
    avoid_setup = 1;
    return;
}


/*
 * set_exitcode_mask - set a bit in the exit code
 */
static void
set_exitcode_mask(int mask)
{
    int oldexit;			/* exitcode before mask */

    /* save current exitcode */
    oldexit = exitcode;

    /* set mask bit */
    exitcode |= mask;

    /* report if changed */
    if (oldexit != exitcode) {
	dbg(1, "exit mask set 0x%02x, exitcode changed from %d to %d",
		mask, oldexit, exitcode);
    }
    return;
}


/*
 * dbg - debugging output if -v level is high enough
 */
static void
dbg(int level, char *fmt, ...)
{
    va_list ap;		/* argument pointer */

    /*
     * firewall
     */
    if (fmt == NULL) {
	fmt = "((NULL fmt given to dbg!!!))";
    }

    /*
     * do nothing if level is too low
     */
    if (level > vflag) {
	return;
    }

    /*
     * print debugging to stdout
     */
    va_start(ap, fmt);
    fprintf(stderr, "Debug[%d]: ", level);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return;
}


/*
 * warn - issue a warning and return
 *
 * given:
 *	warn1	1st warning string
 *	warn2	2nd warning string or NULL
 *	err	error number or 0 ==> not an error
 */
static void
warn(char *warn1, char *warn2, int err)
{
    /* firewall */
    if (warn1 == NULL) {
	warn1 = "<<NULL 1st arg passed to warn!!>>";
    }

    /* print without errno msg */
    if (err == 0) {
	if (warn2 == NULL) {
	    fprintf(stderr, "%s: %s\n", program, warn1);
	} else {
	    fprintf(stderr, "%s: %s: %s\n", program, warn1, warn2);
	}

    /* print with errno msg */
    } else {
	if (warn2 == NULL) {
	    fprintf(stderr, "%s: %s: %s\n", program, warn1, strerror(err));
	} else {
	    fprintf(stderr, "%s: %s: %s: %s\n",
	    	    program, warn1, warn2, strerror(err));
	}
    }
    return;
}


/*
 * fatal - issue a fatal error and exit
 *
 * given:
 *	msg1	1st error string
 *	msg2	2nd error string or NULL
 *	err	error number or 0 ==> not an error
 *
 * NOTE: This function changes exitcode to EXIT_FATAL for obvious reasons.
 */
static void
fatal(char *msg1, char *msg2, int err)
{
    /* firewall */
    if (msg1 == NULL) {
	msg1 = "<<NULL 1st arg passed to msg!!>>";
    }

    /* print without errno msg */
    if (err == 0) {
	if (msg2 == NULL) {
	    fprintf(stderr, "%s: Fatal error: %s\n", program, msg1);
	} else {
	    fprintf(stderr, "%s: Fatal error: %s: %s\n", program, msg1, msg2);
	}

    /* print with errno msg */
    } else {
	if (msg2 == NULL) {
	    fprintf(stderr, "%s: Fatal error: %s: %s\n",
	    	    program, msg1, strerror(err));
	} else {
	    fprintf(stderr, "%s: Fatal error: %s: %s: %s\n",
	    	    program, msg1, msg2, strerror(err));
	}
    }
    exitcode = EXIT_FATAL;
    dbg(3, "exitcode is now %d", exitcode);
    dbg(1, "exit(%d)", exitcode);
    exit(exitcode);
}
