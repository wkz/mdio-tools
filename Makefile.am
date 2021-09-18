SUBDIRS         = man src include
doc_DATA        = README.md ChangeLog.md COPYING
EXTRA_DIST      = $(doc_DATA) kernel
DISTCLEANFILES  = *~ *.d
ACLOCAL_AMFLAGS = -I m4

TAG = $(top_srcdir)/.git/refs/tags/$(PACKAGE_VERSION)

$(TAG):
	@printf "\e[1m\e[41mPlease create the \"$(PACKAGE_VERSION)\" tag first\e[0m\n"
	@exit 1

release: $(TAG) distcheck
	@git status
	@for file in $(DIST_ARCHIVES); do		\
		md5sum $$file    > ../$$file.md5;	\
		sha1sum $$file   > ../$$file.sha1;	\
		sha256sum $$file > ../$$file.sha256;	\
	done
	@mv $(DIST_ARCHIVES) ../
	@echo
	@echo "Resulting release files:"
	@echo "================================================================="
	@for file in $(DIST_ARCHIVES); do					\
		printf "$$file    \tDistribution tarball\n";			\
		printf "$$file.md5\t";    cat ../$$file.md5    | cut -f1 -d' ';	\
		printf "$$file.sha1\t";   cat ../$$file.sha1   | cut -f1 -d' ';	\
		printf "$$file.sha256\t"; cat ../$$file.sha256 | cut -f1 -d' ';	\
	done
