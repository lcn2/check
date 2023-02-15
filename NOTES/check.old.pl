#!/usr/bin/perl -wT
#
# check - check for locked RCS files
#
# @(#) $Revision: 2.1 $
# @(#) $Id: check.pl,v 2.1 2004/11/13 19:47:32 chongo Exp $
# @(#) $Source: /usr/local/src/cmd/check/NEW/RCS/check.pl,v $
#
# Copyright (c) 2004 by Landon Curt Noll.  All Rights Reserved.
#
# Permission to use, copy, modify, and distribute this software and
# its documentation for any purpose and without fee is hereby granted,
# provided that the above copyright, this permission notice and text
# this comment, and the disclaimer below appear in all of the following:
#
#       supporting documentation
#       source copies
#       source works derived from this source
#       binaries derived from this source or from derived source
#
# LANDON CURT NOLL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
# INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO
# EVENT SHALL LANDON CURT NOLL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
# CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
# USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
# OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.
#
# chongo (Landon Curt Noll, http://www.isthe.com/chongo/index.html) /\oo/\
#
# Share and enjoy! :-)

# requirements
#
use strict;
use vars qw($opt_v $opt_l $opt_d $opt_t $opt_u $opt_a $opt_r $opt_q $opt_h);
use Getopt::Long;
use File::Basename;
use File::stat;

# version - RCS style *and* usable by MakeMaker
#
my $VERSION = substr q$Revision: 2.1 $, 10;
$VERSION =~ s/\s+$//;

# my vars
#
my $search_limit = 8*1024;	# search limit in chars for a ";" after locks

# usage and help
#
my $usage = "$0 [-d [-t]] [-l [-a]] [-u] [-r]\n" .
	"\t[-q] [-v lvl] [--help] [path ...]";
my $help = qq{$usage

	-d      print RCS file modification date
	-t	print modification date as seconds since the epoch

	-l      print the 1st RCS lock information found
	-a	report all locks, not just the first lock of a file
	-u	also print unlocked RCS files

	-r	recursive directory scan

	-q	no error messages, convert exit 2 to 0, exit 3 to 1
	-v lvl	verbose / debug level
	--help	print this help message

	exit 0 ==> no lock(s) found
	exit 1 ==> some lock(s) found
	exit 2 ==> permission or RCS error(s), no lock(s) found
	exit 3 ==> permission or RCS error(s), some lock(s) found
	exit 4 ==> fatal error};
my %optctl = (
    "d" => \$opt_d,
    "t" => \$opt_t,
    "l" => \$opt_l,
    "u" => \$opt_u,
    "a" => \$opt_a,
    "r" => \$opt_r,
    "q" => \$opt_q,
    "v=i" => \$opt_v,
    "help" => \$opt_h,
);

# exit codes
#
# 0 ==> no locks, all is OK
# 1 ==> some locks, all is OK
# 2 ==> no locks, access permission problems or RCS format problems
# 3 ==> some locks, access permission problems or RCS format problems
# 4 ==> fatal error
#
# NOTE: Format errors are reserved for mal-formed ,v files that we can
#	read.  Access errors are reserved for not being able to read/access
#	files and directories.  Encountering a ,v inode that is not a
#	regular file or encountering an RCS inode that is not a directory
#	will not cause exitcode to change.
#
my $EXIT_MASK_LOCK =   0x1;	# set bit if lock found
my $EXIT_MASK_ACCESS = 0x2;	# set bit if access errors
my $EXIT_FATAL =       0x4;	# fatal error encountered
my $exitcode = 0;		# how we will/should exit


# function prototypes
#
sub process_arg($);
sub scan_rcsfile($$);
sub rcs_ioerr($$$$);
sub rcs_problem($);
sub read_rcs($);
sub scan_rcsdir($$);
sub rcs_2_pathname($);
sub dir_2_rcsdir($);
sub file_2_filev($);
sub filev_2_file($);
sub pathname_2_rcs($);
sub error($$);
sub debug($$);


# setup
#
MAIN: {
    my $arg;	# command line argument to process

    # setup
    #
    select(STDOUT);
    $| = 1;

    # set the defaults
    #
    $opt_d = 0;
    $opt_t = 0;
    $opt_l = 0;
    $opt_u = 0;
    $opt_r = 0;
    $opt_q = 0;
    $opt_v = 0;
    $opt_h = 0;

    # parse args
    #
    if (!GetOptions(%optctl)) {
	$exitcode = $EXIT_FATAL;
	debug(1, "exit($exitcode)");
	error($exitcode, "invalid command line\nusage: $help");
	exit($exitcode);	# never reached
    }
    if ($opt_h) {
	$exitcode = 0;
	debug(1, "exit(0)");
	print STDERR "usage: $help\n";
	exit($exitcode);
    }

    # single arg processing
    #
    if (! defined $ARGV[0]) {
	debug(1, "no arg, process .");
	process_arg(".");
	debug(1, "exit($exitcode)");
	exit($exitcode);
    }

    # process each arg
    #
    foreach $arg (@ARGV) {

	# canonicalize - no //'s, no extra trailing /'s
	#
	$arg =~ s|/{2,}|/|g;
	$arg =~ s|(.)/$|$1|;

	# process the argument
	#
	process_arg($arg);
    }

    # All done!!! -- Jessica Noll, Age 2
    #
    if ($opt_q && ($exitcode & $EXIT_MASK_ACCESS) != 0) {
	debug(1, "turning off $EXIT_MASK_ACCESS bit due to -q");
	$exitcode &= ~$EXIT_MASK_ACCESS;
    }
    debug(1, "exit($exitcode)");
    exit($exitcode);
}


# process_arg - determine if an file is locked or a directory as locked files
#
# given:
#	$arg	a file or directory
#
# NOTE: This function will NOT directly alter exitcode, even if it encounters
#	a ,v file that is not a regular file nor will if if finds an
#	RCS inode that is not a directory.  Format errors are reserved
#	for mal-formed ,v files that we can read.  Access errors are
#	reserved for not being able to read/access files and directories.
#
sub process_arg($)
{
    my ($arg) = @_;	# get args
    my $modarg;		# modified copy of arg
    my $tmp;

    # firewall: assume arg is . if undefined or empty
    #
    if (! defined $arg || $arg eq "") {
	debug(1, "arg was undefined or empty");
	$arg = ".";
    }
    debug(1, "processing arg: $arg");

    # case: arg ends in ,v
    #
    # We must deal with the ending in ,v case first before considering
    # normal cases of directories and files because of the way RCS
    # tools process such ,v ending arguments.
    #
    # NOTE: I didn't make the rules below, the RCS tools did!
    #
    if ($arg =~ m/,v$/) {

	# case: arg ends in ,v and has NO / in the path
	#
	# The RCS tools, when given just "foo,v" will first look for
	# a RCS/foo,v file (even when ./foo,v exists).  If RCS/foo,v
	# does not exist, then the RCS tool will look for the foo,v file.
	#
	# If we are processing "foo,v" and RCS/foo,v exists but is
	# not a file, then the RCS tool report an error.  We consider
	# foo to not be locked when RCS/foo,v exists and is not a file.
	#
	# If we are processing "foo,v" and RCS/foo,v does NOT exist,
	# then "./foo,v is examined for locking.
	#
	debug(3, "*,v arg processing");
	if ($arg !~ m|/|) {

	    # case: arg ends in ,v and has NO / and RCS as a ,v file
	    #
	    # NOTE: We have to check the RCS sub-directory even when
	    #       the file, arg, exists.
	    #
	    $tmp = filev_2_file($arg);
	    $modarg = pathname_2_rcs($tmp);
	    if (-f $modarg) {

		# found RCS/arg,v file, scan it
		debug(5, "RCS *,v file found: $modarg");
		scan_rcsfile($modarg, $tmp);

	    # case: arg ends in ,v and has NO / and RCS has a non-file ,v
	    #
	    # NOTE: The RCS tools will ignore arg (an existing ,v file)
	    #	    when they discover that the RCS ,v exists but is
	    #       not a file.
	    #
	    } elsif (-e $modarg) {

	    	# found RCS/arg,v but it is not a regular file, so no lock
		debug(5, "RCS *,v non-file found: $modarg");

	    # case: arg ends in ,v and has NO /, RCS has no ,v, arg is a file
	    #
	    # NOTE: When given just "foo,v" and no RCS/foo,v exists,
	    #       the RCS tools will look at "./foo,v".
	    #
	    } elsif (-f $arg) {

	    	# scan the ,v file directly
		scan_rcsfile($arg, $tmp);

	    # case: arg ends in ,v and has NO /, no RCS ,v and arg not a file
	    #
	    } elsif (-e $arg) {

		# no ,v not a file, no lock
		debug(5, "*,v non-file found: $arg");

	    # case: arg ends in ,v and has NO /, no RCS ,v and no arg file
	    #
	    } else {

		# no ,v file, no lock
		debug(5, "no RCS *,v and no . *,v found for: $arg");
	    }

	# case: arg exists, ends in ,v and has a / in it, and is NOT a file
	#
	# If an RCS tool is given "stuff/foo,v" where the arg has a /
	# in it, then only "stuff/foo,v" is examined.  Unlike the case
	# where the arg as no /, RCS does not examine "stuff/RCS/foo,v".
	#
	# When the ,v arg is not a regular file, the RCS tool returns a
	# error.  We consider these files to not be locked.
	#
	} elsif (-e $arg && ! -f $arg) {

	    # ,v is not an regular file, so it is not locked
	    debug(5, "RCS/*,v non-file found: $arg");

	# case: arg is a ,v file, ends in ,v and has a / in it, and is a file
	#
	# Unlike the case of just "foo,v", the RCS tools only look at ./foo,v
	# when given "./foo,v" for example.
	#
	} elsif (-f $arg) {

	    # scan the ,v file directly
	    debug(5, "./*,v file found: $arg");
	    scan_rcsfile($arg, undef);

	# case: arg ends in ,v but does not exist
	#
	} else {

	    # case: arg does not exist but the RCS subdir has the ,v
	    #
	    $tmp = filev_2_file($arg);
	    $modarg = pathname_2_rcs($tmp);
	    if (-f $modarg) {

		# found arg,v file, scan it
		debug(5, "RCS/*,v file found: $modarg");
		scan_rcsfile($modarg, $tmp);

	    # case: arg does not exist, and no ,v file in RCS subdir
	    #
	    } else {

		# no ,v file, no lock
		debug(5, "no RCS/*,v and no ./*,v found");
	    }
	}

    # case: arg does not end in ,v and is a directory (and exists)
    #
    } elsif (-d $arg) {

	# case: arg is a directory and ends in RCS (or is just RCS)
	#
	debug(3, "directory arg processing");
	if ($arg =~ m|/RCS$| || $arg eq "RCS" || $arg eq "RCS/") {

	    # arg is a RCS directory: only look for for ,v files in it
	    debug(5, "arg is an RCS directory: $arg");
	    scan_rcsdir($arg, undef);

	# case: arg is a directory and the last path component is not RCS
	#
	} else {

	    # case: arg is a directory and an RCS sub-directory exists
	    #
	    $modarg = dir_2_rcsdir($arg);
	    if (-d $modarg) {

		# First scan the RCS sub-directory for ,v files,
		# then scan the arg directory for ,v files that are
		# not also under the RCS sub-directory.
		#
		# NOTE: If ./foo,v and ./RCS/foo,v both exist, then
		#	the RCS tools only process ./RCS/foo,v.  Even
		#	when ./RCS/foo,v is a regular file (e.g., a
		#	directory), the RCS tools ignore the ./foo.v file.
		#
		# The first call to scan_rcsdir() will scan the RCS
		# sub-directory for ,v files.  The second call to
		# scan_rcsdir() will ,v files not previously found
		# in the RCS sub-directory.  If either (or both)
		# contain locks, ret will be TRUE.
		#
		debug(5, "RCS directory found: $modarg");
		scan_rcsdir($modarg, undef);
		debug(5, "arg directory found: $arg");
		scan_rcsdir($arg, $modarg);

	    # case: arg is a directory without an RCS sub-directory
	    #
	    } else {

		# just scan the arg directory for ,v files
		debug(5, "only arg directory found: $arg");
		scan_rcsdir($arg, undef);
	    }
	}

    # case: arg does not end in ,v and is not a directory
    #
    } else {

	# case: arg does not end in ,v and RCS sub-dir has a ,v file
	#
	$modarg = pathname_2_rcs($arg);
	if (-f $modarg) {

	    # found RCS ,v file, scan it
	    debug(5, "found RCS/*,v file: $modarg for arg: $arg");
	    scan_rcsfile($modarg, $arg);

	# case: arg does not end in ,v and the RCS ,v exists but is not a file
	#
	} elsif (-e $modarg) {

	    # ,v is not an regular file, so it is not locked
	    debug(5, "found RCS/*,v non-file: $modarg for arg: $arg");

	# case: arg does not end in ,v and no RCS ,v
	#
	} else {

	    # case: arg does not end in ,v and no RCS ,v and arg,v is a file
	    #
	    $modarg = file_2_filev($arg);
	    if (-f $modarg) {

		# found arg,v file, scan it
		debug(5, "found ./*,v file: $modarg for arg: $arg");
		scan_rcsfile($modarg, $arg);

	    # case: arg does not end in ,v and no RCS ,v and arg,v not a file
	    # case: arg does not end in ,v and no RCS ,v and arg,v not exist
	    #
	    } else {

		# no ,v file (regular or missing), so no lock
		debug(5, "no *,v files found");
	    }
	}
    }
    return;
}


# scan_rcsfile - determine if a file is locked
#
# given:
#	$filename	determine if file is locked
#	$arg		path/foo arg to print if locked
#
# NOTE: If arg is undef, this function will attempt to form the name to
#	print from the filename.
#
# NOTE: This function will set the EXIT_MASK_LOCK bits of exitcode
#	if it finds a lock RCS file.
#
sub scan_rcsfile($$)
{
    my ($filename, $arg) = @_;	# get args
    my $result;		# 0 ==> unlocked, 1 ==> locked, -1 ==> error
    my $owner;		# lock owners if $filename locked and -d
    my $revision;	# revisions locked if $filename locked and -d
    my $mod_time;	# modification time of RCS file
    my $locks_printed;	# number of locks printed for this file
    my $i;

    # firewall
    #
    if (! defined $filename) {
	$exitcode = $EXIT_FATAL;
	debug(1, "exit($exitcode)");
	error($exitcode, "scan_rcsfile filename is undefined");
	exit($exitcode);	# never reached
    }

    # see if the RCS file is locked
    #
    debug(2, "scanning file: $filename");
    if (defined $arg) {
	debug(4, "2nd arg: $arg");
    } else {
	debug(4, "no 2nd arg");
    }
    ($result, $owner, $revision, $mod_time) = read_rcs($filename);

    # compute non-RCS name if needed
    #
    if (! defined $arg) {
	$arg = rcs_2_pathname($filename);
    }

    # print results if locked
    #
    if ($result > 0) {

	# firewall - check ownership and revision info if -d
	#
	# If we don't need the owner/rev info, then we won't
	# complain if it is mal-formed.
	#
	if ($opt_l &&
	    (! defined $owner || ! defined $revision ||
	     $#{$owner} != $#{$revision} || $#{$owner} < 0)) {
	    $exitcode = $EXIT_FATAL;
	    debug(1, "exit($exitcode)");
	    error($exitcode, "bogus owner and/or revision arrays");
	    exit($exitcode);	# never reached
	}

	# firewall - check mod date
	#
	# If we don't need the mod_time, then we won't
	# complain if it is mal-formed.
	#
	if ($opt_d && ! defined $mod_time) {
	    $exitcode = $EXIT_FATAL;
	    debug(1, "exit($exitcode)");
	    error($exitcode, "bogus mod time");
	    exit($exitcode);	# never reached
	}

	# debugging and exit code management
	#
	if (($exitcode & $EXIT_MASK_LOCK) == 0) {
	    debug(3, "#0: adding code $EXIT_MASK_LOCK" .
	    	     " to exitcode: $exitcode");
	}
	$exitcode |= $EXIT_MASK_LOCK;

	# if -a, we must print a line for every lock
	#
	$locks_printed = ($opt_a ? $#{$owner}+1 : 1);
	for ($i=0; $i < $locks_printed; ++$i) {
	    print "$arg";
	    print "\t@{$owner}[$i]\t@{$revision}[$i]" if $opt_l;
	    print "\t$mod_time" if $opt_d;
	    print "\n";
	}

    # print if unlocked and -u
    #
    } elsif ($result == 0) {
	print "$arg";
	print "\t$mod_time" if ($opt_d && defined $mod_time);
	print "\n";

    # I/O or RCS syntax error
    #
    } else {
    	debug(7, "I/O or RCS syntax error for: $arg");
    }
    return;
}


# rcs_ioerr - report an RCS IO or read error
#
# given:
#	*HANDLE		the handle on which the read failed
#	$filename	name of file that we were reading
#	$errmsg		error message on I/O error while reading
#	$eofmsg		error message if we hit EOF while reading,
#			    undef ==> do not check for EOF, assume errmsg
#
# returns:
#	(-1, undef, undef, undef)
#
# NOTE: The return is a suitable return from read_rcs($) indicating that
#	the file is not locked and no owner/revision/mod_date is found.
#
# NOTE: A side effect of this call is that EXIT_MASK_ACCESS will
#	be set in the exitcode.
#
sub rcs_ioerr($$$$)
{
    my ($fh, $filename, $errmsg, $eofmsg) = @_;		# get args

    # firewall
    #
    if (! defined $fh) {
	fatal(1, "rcs_ioerr: undefined file handle");
    }
    if (! defined $filename) {
	fatal(1, "rcs_ioerr: undefined filename");
    }
    if (! defined $errmsg) {
	fatal(1, "rcs_ioerr: undefined read error message");
    }

    # EOF processing
    #
    if (defined $eofmsg && eof $fh) {
	if ($opt_q) {
	    debug(1, "$eofmsg: $filename");
	} else {
	    print STDERR "$eofmsg: $filename\n";
	}
    } else {
	if ($opt_q) {
	    debug(1, "$errmsg: $filename: $!");
	} else {
	    print STDERR "$errmsg: $filename: $!\n";
	}
    }

    # set EXIT_MASK_ACCESS in exit code
    #
    if (($exitcode & $EXIT_MASK_ACCESS) == 0) {
	debug(3, "#1: adding code $EXIT_MASK_ACCESS" .
		 " to exitcode: $exitcode");
    }
    $exitcode |= $EXIT_MASK_ACCESS;

    # return no lock - suitable for a read_rcs($) return
    #
    return (-1, undef, undef, undef);
}


# rcs_problem - report an non-I/O error with an RCS file
#
# given:
#	$errmsg		the error message to report
#
# returns:
#	(-1, undef, undef, undef)
#
# NOTE: The return is a suitable return from read_rcs($) indicating that
#	the file is not locked and no owner/revision/mod_date is found.
#
# NOTE: A side effect of this call is that EXIT_MASK_ACCESS will
#	be set in the exitcode.
#
sub rcs_problem($)
{
    my ($errmsg) = @_;		# get args

    # firewall
    #
    if (! defined $errmsg) {
	fatal(1, "rcs_ioerr: undefined read error message");
    }

    # report the problem
    #
    if ($opt_q) {
	debug(1, "$errmsg");
    } else {
	print STDERR "$errmsg\n";
    }

    # set EXIT_MASK_ACCESS in exit code
    #
    if (($exitcode & $EXIT_MASK_ACCESS) == 0) {
	debug(3, "#2: adding code $EXIT_MASK_ACCESS" .
		 " to exitcode: $exitcode");
    }
    $exitcode |= $EXIT_MASK_ACCESS;

    # return no lock - suitable for a read_rcs($) return
    #
    return (-1, undef, undef, undef);
}


# read_rcs - read an RCS file and determine lock information if locked
#
# given:
#	$filename	a RCS file to read for lock info
#
# returns:
#	($ret, $owner, $revision, $mod_time)
#
#	$ret == 0 ==> not locked, else file is locked
#	if $ret != 0, $owner is who locked the file
#	if $ret != 0, $revision is the 1st version that was locked
#	if defined, $mod_time is the modification time of RCS file
#	$ret == -1 ==> I/O or RCS file format error
#
sub read_rcs($)
{
     my ($filename) = @_;	# RCS file to read for locks
     my $line;			# a line from the RCS file
     my $locks;			# locks directive
     my @own;			# list of owners found
     my @rev;			# list of lock revisions found
     my $mod_time;		# mod file of the RCS file if locked

    # firewall - empty of missing path is treated as an unlocked RCS file
    #
    if (! defined $filename || $filename eq "") {
	# a "nothing" can never be locked
	#
    	return (0, undef, undef, undef);
    }

    # open the file
    #
    if (! open FILE, "<", $filename) {
	return rcs_problem("cannot read RCS file: $filename: $!");
    }

    # get the modification time of the RCS file
    #
    $mod_time = stat($filename)->mtime;
    if (! defined $mod_time) {
	close FILE;
	return rcs_problem("cannot stat RCS file: $filename: $!");
    }

    # convert mod_time, if we have one, to string unless -t
    #
    if (defined $mod_time && !$opt_t) {
	$mod_time = scalar localtime($mod_time);
    }

    # must start with a head line
    #
    $line = <FILE>;
    if (! defined $line) {
	close FILE;
        return rcs_ioerr(*FILE, $filename,
			 "RCS 1st read error", "empty RCS file");
    }
    if ($line !~ /^head\s+[0-9][0-9.]+;$/) {
	close FILE;
	return rcs_problem("not an RCS file or bad RCS header: $filename");
    }

    # quick exit
    #
    # If we have -u (printing locked and unlocked files) and we do not
    # want lock users (no -l), then quicly return as if the file
    # was not locked.
    #
    if ($opt_u && !$opt_l && !$opt_d) {
	debug(5, "skip lock check on RCS file: $filename");
	close FILE;
	return (0, undef, undef, $mod_time);
    }

    # read until we see the locks line
    #
    do {
	# read another line
	#
	$line = <FILE>;
	if (! defined $line) {
	    return rcs_ioerr(*FILE, $filename,
	    		     "RCS read error",
			     "premature EOF on RCS file");
	}

	# we should never see a desc line before the locks line
	#
	if ($line =~ /^desc\s*$/) {
	    close FILE;
	    return rcs_problem("missing RCS locks section: $filename");
	}

    } while ($line !~ /^locks/);

    # We have the start of the locks directive, collect lines until
    # we see a ";" (semi-colon).
    #
    # Lock directive are of the form:
    #
    #	locks (username : rev)* ; (strict ;)*
    #
    # There is at ";" after all of the username:rev pairs.  If the file
    # if not locked (and thus there are zero username:rev pairs), then
    # the ";" follows the locks directive.
    #
    # We impose a limit of $search_limit on the amount of data we are
    # willing to read before we give up looking for a ";".  This limit
    # is not part of the RCS syntax.  We impose this limit in case we
    # find an insane file that looks like an RCS file.
    #
    $locks = $line;
    while (length($locks) < $search_limit && $locks !~ /;/) {
	# read another line
	#
	$line = <FILE>;
	if (! defined $line) {
	    close FILE;
	    return rcs_ioerr(*FILE, $filename,
	    		     "RCS read error after locks directive",
			     "premature EOF after locks directive on RCS file");
	}
	# turn the trailing newline into a space
	chomp $line;
	$locks = $locks . " " . $line;
    }
    if ($locks !~ /;/) {
	close FILE;
	return rcs_problem("failed to find ; after locks directive");
    }

    # parse the locks directive
    #
    if ($locks =~ /^locks\s*;/) {
	# file is not locked
	debug(5, "$filename is not locked");
	close FILE;
	return (0, undef, undef, $mod_time);
    }

    # remove leading and trailing text from the lock string
    #
    $locks =~ s/^locks\s*//;
    $locks =~ s/\s*;.*$//;
    while ($locks =~ s/\s*(\w+)\s*:\s*([\d.]+)\s*(.*)$/$3/) {
	push @own, $1;
	push @rev, $2;
    }

    # return lock info
    #
    close FILE;
    return (1, \@own, \@rev, $mod_time);
}


# scan_rcsdir - determine if a directory is locked
#
# given:
#	$dir1	directory to scan for ,v files
#	$dir2	if defined, ignore a ,v file in $dir1 if the dir2/,v file exists
#
# NOTE: Will alter $exitcode if it encounters directories it cannot scan.
#
sub scan_rcsdir($$)
{
    my ($dir1, $dir2) = @_;	# get args
    my @v;			# list of ,v files found
    my @r;			# list of subdirectories to recurse of -r
    my $arg;			# directory or file from list to process
    my $modarg;			# modified copy of arg
    my $tmp;			# temp filename of $arg without the ,v

    # firewall - empty or missing path is treated as .
    #
    if (! defined $dir1 || $dir1 eq "") {
	$dir1 = ".";
    }
    debug(7, "starting RCS scan of $dir1");

    # firewall - $dir1 is not a directory - do nothing
    #
    if (! -d $dir1) {
	debug(7, "early RCS scan stop, not a directory: $dir1");
	if (($exitcode & $EXIT_MASK_ACCESS) == 0) {
	    debug(3, "#3: adding code $EXIT_MASK_ACCESS" .
	    	     " to exitcode: $exitcode");
	}
	$exitcode |= $EXIT_MASK_ACCESS;
	return;
    }

    # read the $dir1 directory for appropriate ,v files
    #
    # if $dir2 is a directory (and is defined),
    #	ignore $dir1 ,v filenames that are also in $dir2
    #
    if (!opendir DIR, $dir1) {
	debug(7, "unable to scan directory: $dir1");
	if (($exitcode & $EXIT_MASK_ACCESS) == 0) {
	    debug(3, "#4: adding code $EXIT_MASK_ACCESS" .
	    	     " to exitcode: $exitcode");
	}
	$exitcode |= $EXIT_MASK_ACCESS;
	return;
    }
    if (defined $dir2 && -d $dir2) {
        debug(9, "scanning *,v files in $dir1 except if $dir2/*,v");
	@v = sort grep {/,v$/ && -f "$dir1/$_" && ! -e "$dir2/$_"} readdir(DIR);
    } else {
        debug(9, "scanning *,v files in $dir1");
	@v = sort grep {/,v$/ && -f "$dir1/$_"} readdir(DIR);
    }
    closedir(DIR);

    # scan each ,v file found
    #
    if (scalar @v > 0) {
	debug(7, "under $dir1, will scan: " . join(' ', @v));
	foreach $arg (@v) {
	    debug(5, "*,v file found under $dir1: $arg");
	    $tmp = rcs_2_pathname("$dir1/$arg");
	    scan_rcsfile("$dir1/$arg", $tmp);
	}
    } else {
	debug(7, "under $dir1, nothing to scan");
    }

    # if -r, recurse on appropriate sub-directories,
    # unless we are inside an RCS directory
    #
    if ($opt_r && $dir1 !~ m|/RCS$| && $dir1 ne "RCS") {

	# determine the appropriate sub-directories to recursively scan
	#
	if (!opendir DIR, $dir1) {
	    debug(7, "unable to rescan directory: $dir1");
	    if (($exitcode & $EXIT_MASK_ACCESS) == 0) {
		debug(3, "#5: adding code $EXIT_MASK_ACCESS" .
			 " to exitcode: $exitcode");
	    }
	    $exitcode |= $EXIT_MASK_ACCESS;
	    return;
	}
	@r = grep {$_ ne "." && $_ ne ".." &&
		   -d "$dir1/$_" && ! -l "$dir1/$_"} readdir(DIR);
	closedir(DIR);

	# recursively scan each sub-directory found
	#
	if (scalar @r > 0) {
	    debug(7, "under $dir1, will recurse: " . join(' ', @r));
	    foreach $arg (@r) {
		debug(7, "subdir found under $dir1: $arg");
		if (defined $dir2 && -d $dir2) {
		    $modarg = dir_2_rcsdir("$dir2/$arg");
		    scan_rcsdir("$dir1/$arg", $modarg);
		} else {
		    scan_rcsdir("$dir1/$arg", undef);
		}
	    }
	} else {
	    debug(7, "under $dir1, nothing to recurse");
	}
    }
}


# rcs_2_pathname - convert path/RCS/filename,v name into path/filename
#
# given:
#	$rcsname	RCS,v name
#
# returns:
#	pathname without the /RCS or ,v
#
sub rcs_2_pathname($)
{
    my ($rcsname) = @_;		# get args
    my $dir;			# directory component
    my $file;			# filename component
    my $suffix;			# trailing ,v suffix
    my $pre_rcsdir;		# directory before the RCS path component
    my $rcs;			# RCS path component
    my $ret;			# path/filename to return

    # firewall - empty or missing path is treated as . so return it
    #
    if (! defined $rcsname || $rcsname eq "") {
	return ".";
    }

    # convert multiple //'s into a single /
    #
    $rcsname =~ s|/{2,}|/|g;

    # parse the RCS pathname
    #
    ($file, $dir, $suffix) = fileparse($rcsname, qr{,v});
    $dir =~ s|/+$||;
    $dir = "." if $dir eq "";

    # strip off a trailing RCS from the directory
    #
    ($rcs, $pre_rcsdir, ) = fileparse($dir, );
    $pre_rcsdir =~ s|/+$||;
    $pre_rcsdir = "." if $pre_rcsdir eq "";

    # form path/filename with RCS stripped
    #
    # Paranoia: The fileparse($$) function should leave $dir or $pre_rcsdir
    #	        with a trailing /.  But in case it does not, we stick in
    #	        our own /.  Any extra /'s will be removed below.
    #
    if ($rcs eq "RCS") {
	$ret = "$pre_rcsdir/$file";
    } else {
	$ret = "$dir/$file";
    }

    # convert multiple //'s into a single /
    #
    $ret =~ s|/{2,}|/|g;

    # remove any leading ./
    #
    $ret =~ s|^\./||;
    $ret = "." if $ret eq "";

    # return path/filename
    #
    return $ret;
}


# dir_2_rcsdir - convert a dirpath into a dirpath/RCS
#
# given:
#	$dirname	path to a directory
#
# returns:
#	dirname/RCS
#
sub dir_2_rcsdir($)
{
    my ($dirname) = @_;		# get args

    # firewall - empty or missing path is treated as . so return ./RCS
    #
    if (! defined $dirname || $dirname eq "") {
    	return "./RCS";
    }

    # append /RCS
    #
    $dirname .= "/RCS";

    # convert multiple //'s into a single /
    #
    $dirname =~ s|/{2,}|/|g;

    # remove any leading ./
    #
    $dirname =~ s|^\./||;
    $dirname = "." if $dirname eq "";

    # return dirpath/RCS
    #
    return $dirname;
}


# file_2_filev - convert file into a file,v
#
# given:
#	$file	name of a file
#
# returns:
#	file,v
#
sub file_2_filev($)
{
    my ($file) = @_;		# get args

    # firewall - empty or missing path is treated as . so return ./,v
    #
    if (! defined $file || $file eq "") {
	return "./,v";
    }

    # add on ,v
    #
    $file .= ",v";

    # convert multiple //'s into a single /
    #
    $file =~ s|/{2,}|/|g;

    # remove any leading ./
    #
    $file =~ s|^\./||;
    $file = "." if $file eq "";

    # return file,v
    #
    return $file;
}


# filev_2_file - convert file,v into a file
#
# given:
#    $file	name of a file,v
#
# returns:
#    $file without a trailing ,v
#
sub filev_2_file($)
{
    my ($file) = @_;		# get args

    # firewall - empty or missing path is treated as . so return .
    #
    if (! defined $file || $file eq "") {
	return ".";
    }

    # convert multiple //'s into a single /
    #
    $file =~ s|/{2,}|/|g;

    # strip off ,v
    #
    $file =~ s/,v$//;

    # remove any leading ./
    #
    $file =~ s|^\./||;
    $file = "." if $file eq "";

    # return file without ,v
    #
    return $file;
}


# pathname_2_rcs - convert a path/file into path/RCS/file,v RCS name
#
# given:
#	$pathname	path/file
#
# returns:
#	path/RCS/file,v RCS name
#
sub pathname_2_rcs($)
{
    my ($pathname) = @_;	# get args
    my $dir;			# directory component
    my $file;			# filename component
    my $ret;			# path/RCS/file,v to return

    # firewall - empty or missing path is treated as . so return ./RCS/,v
    #
    if (! defined $pathname || $pathname eq "") {
	return "./RCS/,v";
    }

    # convert multiple //'s into a single /
    #
    $pathname =~ s|/{2,}|/|g;

    # split pathname into directory and file
    #
    ($file, $dir, ) = fileparse($pathname, );
    $dir =~ s|/+$||;

    # form the path/RCS/file,v RCS name
    #
    # Paranoia: The fileparse($$) function should leave $dir or $pre_rcsdir
    #	        with a trailing /.  But in case it does not, we stick in
    #	        our own /.  Any extra /'s will be removed below.
    #
    $ret = "$dir/RCS/$file,v";

    # convert multiple //'s into a single /
    #
    $ret =~ s|/{2,}|/|g;

    # remove any leading ./
    #
    $ret =~ s|^\./||;
    $ret = "." if $ret eq "";

    # return path/RCS/file,v RCS name
    #
    return $ret;
}


# error - report an error and exit
#
# given:
#       $exitval	exit code value
#       $msg		the message to print
#
sub error($$)
{
    my ($exitval, $msg) = @_;    # get args

    # parse args
    #
    if (!defined $exitval) {
	$exitval = 254;
    }
    if (!defined $msg) {
	$msg = "<<< no message supplied >>>";
    }
    if ($exitval =~ /\D/) {
	$msg .= "<<< non-numeric exit code: $exitval >>>";
	$exitval = 253;
    }

    # issue the error message
    #
    print STDERR "$0: $msg\n";

    # issue an error message
    #
    exit($exitval);
}


# debug - print a debug message is debug level is high enough
#
# given:
#       $min_lvl	minimum debug level required to print
#       $msg		message to print
#
sub debug($$)
{
    my ($min_lvl, $msg) = @_;    # get args

    # firewall
    #
    if (!defined $min_lvl) {
    	error(97, "debug called without a minimum debug level");
    }
    if ($min_lvl =~ /\D/) {
    	error(98, "debug called with non-numeric debug level: $min_lvl");
    }
    if ($opt_v < $min_lvl) {
	return;
    }
    if (!defined $msg) {
    	error(99, "debug called without a message");
    }

    # issue the debug message
    #
    print STDERR "DEBUG[$min_lvl]: $msg\n";
}
