# check

Check on checked out RCS files.

* check - check checked out RCS files

* rcheck - recursively run check


# To install

```sh
make clobber all
sudo make install clobber
```


# To use


```
/usr/local/bin/check [-a] [-A] [-c] [-d] [-e] [-l] [-m] [-p] [-r] [-R]
		[-s /dir]... [-t] [-x] [-h] [-v level] [-V] [path ...]

/usr/local/bin/rcheck [-a] [-A] [-c] [-d] [-e] [-l] [-m] [-p] [-r] [-R]
		 [-s /dir]... [-t] [-x] [-h] [-v level] [-V] [path ...]

	-a		-c -d -m -p (with rcheck: -r)
	-A		-c -d -e -m -p -R (with rcheck: -r)
	-c		print 1-word comment before each filename (def: don't)
	-d		note when file and RCS differ (def: don't)
	-e		report files that are under RCS control (def: don't)
	-h		print help and exit 0 (def: don't)
	-l		print RCS lock information (def: don't)
	-m		report missing files under RCS control (def: don't)
	-p		print absolute paths (def: don't unless using rcheck)
	-q		do not report locked filenames (def: do)
	-r		recursive search (def: don't unless using rcheck)
	-R		find *.rpm{orig,init,save,new} files (def: don't)
	-s /dir		skip paths starting with /dir, sets -p (def: don't)
	-t		    print RCS modification timestamp (def: don't)
	-x		    do not cross filesystems when -r (def: do)
	-v level	debugging level (def: 0)
	-V		    print version string and exit

exit 0 ==> all OK
exit bit 0 ==> locked file (1, 3, 5, 7, 9, 11, 13, 15,
                            17, 19, 21, 23, 25, 27, 29, 31)
exit bit 1 ==> -m & file not checked out (2-3, 6-7, 10-11, 14-15,
                                          18-19, 22-23, 26-27, 30-31)
exit bit 2 ==> -d and file different from RCS (8-15, 24-31)
exit bit 3 ==> -R and *.rpm{orig,init,save,new} found (16-31)
exit 32 ==> fatal error
```


# Reporting Security Issues

To report a security issue, please visit "[Reporting Security Issues](https://github.com/lcn2/check/security/policy)".
