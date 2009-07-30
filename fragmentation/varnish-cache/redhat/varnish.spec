Summary: High-performance HTTP accelerator
Name: varnish
Version: 2.0.2
Release: 1%{?dist}
License: BSD
Group: System Environment/Daemons
URL: http://www.varnish-cache.org/
Source0: http://downloads.sourceforge.net/varnish/varnish-%{version}.tar.gz
#Patch0: varnish.varnishtest_debugflag.patch
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
# The svn sources needs autoconf, automake and libtool to generate a suitable
# configure script. Release tarballs would not need this
#BuildRequires: automake autoconf libtool
BuildRequires: ncurses-devel libxslt groff
Requires: varnish-libs = %{version}-%{release}
Requires: logrotate
Requires: ncurses
Requires(pre): shadow-utils
Requires(post): /sbin/chkconfig
Requires(preun): /sbin/chkconfig
Requires(preun): /sbin/service
Requires(preun): initscripts

# Varnish actually needs gcc installed to work. It uses the C compiler 
# at runtime to compile the VCL configuration files. This is by design.
Requires: gcc

%description
This is the Varnish high-performance HTTP accelerator. Documentation
wiki and additional information about Varnish is available on the following
web site: http://www.varnish-cache.org/

%package libs
Summary: Libraries for %{name}
Group: System Environment/Libraries
BuildRequires: ncurses-devel
#Obsoletes: libvarnish1

%description libs
Libraries for %{name}.
Varnish is a high-performance HTTP accelerator.

%package libs-devel
Summary: Development files for %{name}-libs
Group: System Environment/Libraries
BuildRequires: ncurses-devel
Requires: varnish-libs = %{version}-%{release}

%description libs-devel
Development files for %{name}-libs
Varnish is a high-performance HTTP accelerator

#%package libs-static
#Summary: Files for static linking of %{name} library functions
#Group: System Environment/Libraries
#BuildRequires: ncurses-devel
#Requires: varnish-libs-devel = %{version}-%{release}
#
#%description libs-static
#Files for static linking of varnish library functions
#Varnish is a high-performance HTTP accelerator

%prep
%setup -q
#%setup -q -n varnish-cache

#%patch0 -p0

# The svn sources needs to generate a suitable configure script
# Release tarballs would not need this
#./autogen.sh

# Hack to get 32- and 64-bits tests run concurrently on the same build machine
case `uname -m` in
	ppc64 | s390x | x86_64 | sparc64 )
		sed -i ' 
			s,9001,9011,g;
			s,9080,9090,g; 
			s,9081,9091,g; 
			s,9082,9092,g; 
			s,9180,9190,g;
		' bin/varnishtest/*.c bin/varnishtest/tests/*vtc
		;;
	*)
		;;
esac

mkdir examples
cp bin/varnishd/default.vcl etc/zope-plone.vcl examples

%build

# Remove "--disable static" if you want to build static libraries 
# jemalloc is not compatible with Red Hat's ppc64 RHEL5 kernel koji server :-(
%ifarch ppc64 ppc
%configure --disable-static --localstatedir=/var/lib --disable-jemalloc
%else
%configure --disable-static --localstatedir=/var/lib
%endif

# We have to remove rpath - not allowed in Fedora
# (This problem only visible on 64 bit arches)
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g;
	s|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool

%{__make} %{?_smp_mflags}

head -6 etc/default.vcl > redhat/default.vcl

cat << EOF >> redhat/default.vcl
backend default {
  .host = "127.0.0.1";
  .port = "80";
}
EOF

tail -n +11 etc/default.vcl >> redhat/default.vcl

%if 0%{?fedora}%{?rhel} == 0 || 0%{?rhel} <= 4 && 0%{?fedora} <= 8
	# Old style daemon function
	sed -i 's,--pidfile \$pidfile,,g;
		s,status -p \$pidfile,status,g;
		s,killproc -p \$pidfile,killproc,g' \
	redhat/varnish.initrc redhat/varnishlog.initrc redhat/varnishncsa.initrc
%endif

%check
# rhel5 on ppc64 is just too strange
%ifarch ppc64
	%if 0%{?rhel} > 4
		cp bin/varnishd/.libs/varnishd bin/varnishd/lt-varnishd
	%endif
%endif

LD_LIBRARY_PATH="lib/libvarnish/.libs:lib/libvarnishcompat/.libs:lib/libvarnishapi/.libs:lib/libvcl/.libs" bin/varnishd/varnishd -b 127.0.0.1:80 -C -n /tmp/foo
%{__make} check LD_LIBRARY_PATH="../../lib/libvarnish/.libs:../../lib/libvarnishcompat/.libs:../../lib/libvarnishapi/.libs:../../lib/libvcl/.libs"

# Remove uneccessary doc src files
mkdir doc.src
mv doc/*.xml doc/*.xsl doc/Makefile* doc.src

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot} INSTALL="install -p"

# None of these for fedora
find %{buildroot}/%{_libdir}/ -name '*.la' -exec rm -f {} ';'

# Remove this line to build a devel package with symlinks
#find %{buildroot}/%{_libdir}/ -name '*.so' -type l -exec rm -f {} ';'

mkdir -p %{buildroot}/var/lib/varnish
mkdir -p %{buildroot}/var/log/varnish
mkdir -p %{buildroot}/var/run/varnish
%{__install} -D -m 0644 redhat/default.vcl %{buildroot}%{_sysconfdir}/varnish/default.vcl
%{__install} -D -m 0644 redhat/varnish.sysconfig %{buildroot}%{_sysconfdir}/sysconfig/varnish
%{__install} -D -m 0644 redhat/varnish.logrotate %{buildroot}%{_sysconfdir}/logrotate.d/varnish
%{__install} -D -m 0755 redhat/varnish.initrc %{buildroot}%{_initrddir}/varnish
%{__install} -D -m 0755 redhat/varnishlog.initrc %{buildroot}%{_initrddir}/varnishlog
%{__install} -D -m 0755 redhat/varnishncsa.initrc %{buildroot}%{_initrddir}/varnishncsa

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_sbindir}/*
%{_bindir}/*
%{_var}/lib/varnish
%{_var}/log/varnish
%{_mandir}/man1/*.1*
%{_mandir}/man7/*.7*
%doc INSTALL LICENSE README redhat/README.redhat ChangeLog 
%doc examples
%doc doc
%dir %{_sysconfdir}/varnish/
%config(noreplace) %{_sysconfdir}/varnish/default.vcl
%config(noreplace) %{_sysconfdir}/sysconfig/varnish
%config(noreplace) %{_sysconfdir}/logrotate.d/varnish
%{_initrddir}/varnish
%{_initrddir}/varnishlog
%{_initrddir}/varnishncsa

%files libs
%defattr(-,root,root,-)
%{_libdir}/*.so.*
%doc LICENSE

%files libs-devel
%defattr(-,root,root,-)
%{_libdir}/libvarnish.so
%{_libdir}/libvarnishapi.so
%{_libdir}/libvarnishcompat.so
%{_libdir}/libvcl.so
%dir %{_includedir}/varnish
%{_includedir}/varnish/shmlog.h
%{_includedir}/varnish/shmlog_tags.h
%{_includedir}/varnish/stat_field.h
%{_includedir}/varnish/stats.h
%{_includedir}/varnish/varnishapi.h
%{_libdir}/pkgconfig/varnishapi.pc
%doc LICENSE

#%files libs-static
#%{_libdir}/libvarnish.a
#%{_libdir}/libvarnishapi.a
#%{_libdir}/libvarnishcompat.a
#%{_libdir}/libvcl.a
#%doc LICENSE

%pre
getent group varnish >/dev/null || groupadd -r varnish
getent passwd varnish >/dev/null || \
	useradd -r -g varnish -d /var/lib/varnish -s /sbin/nologin \
		-c "Varnish http accelerator user" varnish
exit 0

%post
/sbin/chkconfig --add varnish
/sbin/chkconfig --add varnishlog
/sbin/chkconfig --add varnishncsa 

%preun
if [ $1 -lt 1 ]; then
  /sbin/service varnish stop > /dev/null 2>&1
  /sbin/service varnishlog stop > /dev/null 2>&1
  /sbin/service varnishncsa stop > /dev/null 2>%1
  /sbin/chkconfig --del varnish
  /sbin/chkconfig --del varnishlog
  /sbin/chkconfig --del varnishncsa 
fi

%post libs -p /sbin/ldconfig

%postun libs -p /sbin/ldconfig

%changelog
* Wed Feb 11 2009 Ingvar Hagelund <ingvar@linpro.no> - 2.0.3-1
  New upstream release 2.0.3. A bugfix and feature enhancement release

* Fri Dec 12 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0.2-2
  Added a fix for a timeout bug, backported from trunk

* Mon Nov 10 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0.2-1
  New upstream release 2.0.2. A bugfix release

* Sun Nov 02 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0.1-2
- Removed the requirement for kernel => 2.6.0. All supported
  platforms meets this, and it generates strange errors in EPEL

* Fri Oct 17 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0.1-1
- 2.0.1 released, a bugfix release. New upstream sources
- Package now also available in EPEL

* Thu Oct 16 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-2
- Readded the debugflag patch. It's so practical
- Added a strange workaround for make check on ppc64

* Wed Oct 15 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-1
- 2.0 released. New upstream sources
- Disabled jemalloc on ppc and ppc64. Added a note in README.redhat
- Synced to upstream again. No more patches needed

* Wed Oct 08 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-0.11.rc1
- 2.0-rc1 released. New upstream sources
- Added a patch for pagesize to match redhat's rhel5 ppc64 koji build boxes
- Added a patch for test a00008, from r3269
- Removed condrestart in postscript at upgrade. We don't want that

* Fri Sep 26 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-0.10.beta2
- 2.0-beta2 released. New upstream sources
- Whitespace changes to make rpmlint more happy

* Fri Sep 12 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-0.9.20080912svn3184
- Added varnisnsca init script (Colin Hill)
- Corrected varnishlog init script (Colin Hill)

* Tue Sep 09 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-0.8.beta1
- Added a patch from r3171 that fixes an endian bug on ppc and ppc64
- Added a hack that changes the varnishtest ports for 64bits builds,
  so they can run in parallell with 32bits build on same build host

* Tue Sep 02 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-0.7.beta1
- Added a patch from r3156 and r3157, hiding a legit errno in make check

* Tue Sep 02 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-0.6.beta1
- Added a commented option for max coresize in the sysconfig script
- Added a comment in README.redhat about upgrading from 1.x to 2.0

* Fri Aug 29 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-0.5.beta1
- Bumped version numbers and source url for first beta release \o/
- Added a missing directory to the libs-devel package (Michael Schwendt)
- Added the LICENSE file to the libs-devel package
- Moved make check to its proper place
- Removed superfluous definition of lockfile in initscripts

* Wed Aug 27 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-0.4.20080827svn3136
- Fixed up init script for varnishlog too

* Mon Aug 25 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-0.3.20080825svn3125
- Fixing up init script according to newer Fedora standards
- The build now runs the test suite after compiling
- Requires initscripts
- Change default.vcl from nothing but comments to point to localhost:80,

* Mon Aug 18 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-0.2.tp2
- Changed source, version and release to match 2.0-tp2

* Thu Aug 14 2008 Ingvar Hagelund <ingvar@linpro.no> - 2.0-0.1.20080814svn
- default.vcl has moved
- Added groff to build requirements

* Tue Feb 19 2008 Fedora Release Engineering <rel-eng@fedoraproject.org> - 1.1.2-6
- Autorebuild for GCC 4.3

* Sat Dec 29 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.1.2-5
- Added missing configuration examples
- Corrected the license to "BSD"

* Fri Dec 28 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.1.2-4
- Build for fedora update

* Fri Dec 28 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.1.2-2
- Added missing changelog items

* Thu Dec 20 2007 Stig Sandbeck Mathisen <ssm@linpro.no> - 1.1.2-1
- Bumped the version number to 1.1.2.
- Addeed build dependency on libxslt

* Wed Sep 08 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.1.1-3
- Added a patch, changeset 1913 from svn trunk. This makes varnish
  more stable under specific loads. 

* Tue Sep 06 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.1.1-2
- Removed autogen call (only diff from relase tarball)

* Mon Aug 20 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.1.1-1
- Bumped the version number to 1.1.1.

* Tue Aug 14 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.1.svn
- Update for 1.1 branch
- Added the devel package for the header files and static library files
- Added a varnish user, and fixed the init script accordingly

* Thu Jul 05 2007 Dag-Erling Smørgrav <des@des.no> - 1.1-1
- Bump Version and Release for 1.1

* Mon May 28 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.0.4-3
- Fixed initrc-script bug only visible on el4 (fixes #107)

* Sun May 20 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.0.4-2
- Repack from unchanged 1.0.4 tarball
- Final review request and CVS request for Fedora Extras
- Repack with extra obsoletes for upgrading from older sf.net package

* Fri May 18 2007 Dag-Erling Smørgrav <des@des.no> - 1.0.4-1
- Bump Version and Release for 1.0.4

* Wed May 16 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.0.svn-20070517
- Wrapping up for 1.0.4
- Changes in sysconfig and init scripts. Syncing with files in
  trunk/debian

* Fri May 11 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.0.svn-20070511
- Threw latest changes into svn trunk
- Removed the conversion of manpages into utf8. They are all utf8 in trunk

* Wed May 09 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.0.3-7
- Simplified the references to the subpackage names
- Added init and logrotate scripts for varnishlog

* Mon Apr 23 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.0.3-6
- Removed unnecessary macro lib_name
- Fixed inconsistently use of brackets in macros
- Added a condrestart to the initscript
- All manfiles included, not just the compressed ones
- Removed explicit requirement for ncurses. rpmbuild figures out the 
  correct deps by itself.
- Added ulimit value to initskript and sysconfig file
- Many thanks to Matthias Saou for valuable input

* Mon Apr 16 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.0.3-5
- Added the dist tag
- Exchanged  RPM_BUILD_ROOT variable for buildroot macro
- Removed stripping of binaries to create a meaningful debug package
- Removed BuildRoot and URL from subpackages, they are picked from the
  main package
- Removed duplication of documentation files in the subpackages
- 'chkconfig --list' removed from post script
- Package now includes _sysconfdir/varnish/
- Trimmed package information
- Removed static libs and .so-symlinks. They can be added to a -devel package
  later if anybody misses them

* Wed Feb 28 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.0.3-4
- More small specfile fixes for Fedora Extras Package
  Review Request, see bugzilla ticket 230275
- Removed rpath (only visible on x86_64 and probably ppc64)

* Tue Feb 27 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.0.3-3
- Made post-1.0.3 changes into a patch to the upstream tarball
- First Fedora Extras Package Review Request

* Fri Feb 23 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.0.3-2
- A few other small changes to make rpmlint happy

* Thu Feb 22 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.0.3-1
- New release 1.0.3. See the general ChangeLog
- Splitted the package into varnish, libvarnish1 and
  libvarnish1-devel

* Thu Oct 19 2006 Ingvar Hagelund <ingvar@linpro.no> - 1.0.2-7
- Added a Vendor tag

* Thu Oct 19 2006 Ingvar Hagelund <ingvar@linpro.no> - 1.0.2-6
- Added redhat subdir to svn
- Removed default vcl config file. Used the new upstream variant instead.
- Based build on svn. Running autogen.sh as start of build. Also added
  libtool, autoconf and automake to BuildRequires.
- Removed rule to move varnishd to sbin. This is now fixed in upstream
- Changed the sysconfig script to include a lot more nice features.
  Most of these were ripped from the Debian package. Updated initscript
  to reflect this.

* Tue Oct 10 2006 Ingvar Hagelund <ingvar@linpro.no> - 1.0.1-3
- Moved Red Hat specific files to its own subdirectory

* Tue Sep 26 2006 Ingvar Hagelund <ingvar@linpro.no> - 1.0.1-2
- Added gcc requirement.
- Changed to an even simpler example vcl in to /etc/varnish (thanks, perbu)
- Added a sysconfig entry

* Fri Sep 22 2006 Ingvar Hagelund <ingvar@linpro.no> - 1.0.1-1
- Initial build.
