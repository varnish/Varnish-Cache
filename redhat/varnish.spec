%define XXXv_rc rc1
%define vd_rc %{?v_rc:-%{?v_rc}}
%define    _use_internal_dependency_generator 0
%define __find_provides %{_builddir}/varnish-%{version}%{?v_rc:-%{?v_rc}}/redhat/find-provides
Summary: High-performance HTTP accelerator
Name: varnish
Version: 4.0.1
#Release: 0.20140328%{?v_rc}%{?dist}
Release: 1%{?v_rc}%{?dist}
License: BSD
Group: System Environment/Daemons
URL: http://www.varnish-cache.org/
#Source0: http://repo.varnish-cache.org/source/%{name}-%{version}.tar.gz
Source0: %{name}-%{version}%{?vd_rc}.tar.gz
#Source0: %{name}-trunk.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: automake
BuildRequires: autoconf
BuildRequires: jemalloc-devel
BuildRequires: libedit-devel
BuildRequires: libtool
BuildRequires: ncurses-devel
BuildRequires: pcre-devel
BuildRequires: pkgconfig
BuildRequires: python-docutils >= 0.6
BuildRequires: python-sphinx
Requires: jemalloc
Requires: libedit
Requires: logrotate
Requires: ncurses
Requires: pcre
Requires: varnish-libs = %{version}-%{release}
Requires(pre): shadow-utils
Requires(post): /sbin/chkconfig, /usr/bin/uuidgen
Requires(preun): /sbin/chkconfig
Requires(preun): /sbin/service
%if %{undefined suse_version}
Requires(preun): initscripts
%endif
%if 0%{?fedora} >= 17 || 0%{?rhel} >= 7
Requires(post): systemd-units
Requires(post): systemd-sysv
Requires(preun): systemd-units
Requires(postun): systemd-units
BuildRequires: systemd-units
%endif

# Varnish actually needs gcc installed to work. It uses the C compiler 
# at runtime to compile the VCL configuration files. This is by design.
Requires: gcc

%description
This is Varnish Cache, a high-performance HTTP accelerator.
Documentation wiki and additional information about Varnish is
available on the following web site: http://www.varnish-cache.org/

%package libs
Summary: Libraries for %{name}
Group: System Environment/Libraries
BuildRequires: ncurses-devel
#Obsoletes: libvarnish1

%description libs
Libraries for %{name}.
Varnish Cache is a high-performance HTTP accelerator

%package libs-devel
Summary: Development files for %{name}-libs
Group: System Environment/Libraries
BuildRequires: ncurses-devel
Requires: varnish-libs = %{version}-%{release}

%description libs-devel
Development files for %{name}-libs
Varnish Cache is a high-performance HTTP accelerator

%package docs
Summary: Documentation files for %name
Group: System Environment/Libraries

%description docs
Documentation files for %name

#%package libs-static
#Summary: Files for static linking of %{name} library functions
#Group: System Environment/Libraries
#BuildRequires: ncurses-devel
#Requires: varnish-libs-devel = %{version}-%{release}
#
#%description libs-static
#Files for static linking of varnish library functions
#Varnish Cache is a high-performance HTTP accelerator

%prep
%setup -n varnish-%{version}%{?vd_rc}
#%setup -q -n varnish-trunk

%build
# No pkgconfig/libpcre.pc in rhel4
%if 0%{?rhel} == 4
	export PCRE_CFLAGS="`pcre-config --cflags`"
	export PCRE_LIBS="`pcre-config --libs`"
%endif

export CFLAGS="$CFLAGS -Wp,-D_FORTIFY_SOURCE=0"

# Remove "--disable static" if you want to build static libraries
# jemalloc is not compatible with Red Hat's ppc64 RHEL kernel :-(
%ifarch ppc64 ppc
	%configure --disable-static --localstatedir=/var/lib --without-jemalloc --without-rst2html
%else
	%configure --disable-static --localstatedir=/var/lib --without-rst2html
%endif

# We have to remove rpath - not allowed in Fedora
# (This problem only visible on 64 bit arches)
#sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g;
#	s|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g' libtool

make %{?_smp_mflags} V=1

%if 0%{?fedora}%{?rhel} != 0 && 0%{?rhel} <= 4 && 0%{?fedora} <= 8
	# Old style daemon function
	sed -i 's,--pidfile \$pidfile,,g;
		s,status -p \$pidfile,status,g;
		s,killproc -p \$pidfile,killproc,g' \
	redhat/varnish.initrc redhat/varnishlog.initrc redhat/varnishncsa.initrc
%endif

rm -rf doc/sphinx/build/html/_sources
mv doc/sphinx/build/html doc
rm -rf doc/sphinx/build

%check
# rhel5 on ppc64 is just too strange
%ifarch ppc64
	%if 0%{?rhel} > 4
		cp bin/varnishd/.libs/varnishd bin/varnishd/lt-varnishd
	%endif
%endif

# The redhat ppc builders seem to have some ulimit problems?
# These tests work on a rhel4 ppc/ppc64 instance outside the builders
%ifarch ppc64 ppc
	%if 0%{?rhel} == 4
		rm bin/varnishtest/tests/c00031.vtc
		rm bin/varnishtest/tests/r00387.vtc
	%endif
%endif

make check %{?_smp_mflags} LD_LIBRARY_PATH="../../lib/libvarnish/.libs:../../lib/libvarnishcompat/.libs:../../lib/libvarnishapi/.libs:../../lib/libvcc/.libs:../../lib/libvgz/.libs" VERBOSE=1

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
mkdir -p %{buildroot}%{_sysconfdir}/ld.so.conf.d/
install -D -m 0644 etc/example.vcl %{buildroot}%{_sysconfdir}/varnish/default.vcl
install -D -m 0644 redhat/varnish.logrotate %{buildroot}%{_sysconfdir}/logrotate.d/varnish

# systemd support
%if 0%{?fedora} >= 17 || 0%{?rhel} >= 7
mkdir -p %{buildroot}%{_unitdir}
install -D -m 0644 redhat/varnish.service %{buildroot}%{_unitdir}/varnish.service
install -D -m 0644 redhat/varnish.params %{buildroot}%{_sysconfdir}/varnish/varnish.params
install -D -m 0644 redhat/varnishncsa.service %{buildroot}%{_unitdir}/varnishncsa.service
install -D -m 0644 redhat/varnishlog.service %{buildroot}%{_unitdir}/varnishlog.service
sed -i 's,sysconfig/varnish,varnish/varnish.params,' redhat/varnish_reload_vcl
# default is standard sysvinit
%else
install -D -m 0644 redhat/varnish.sysconfig %{buildroot}%{_sysconfdir}/sysconfig/varnish
install -D -m 0755 redhat/varnish.initrc %{buildroot}%{_initrddir}/varnish
install -D -m 0755 redhat/varnishlog.initrc %{buildroot}%{_initrddir}/varnishlog
install -D -m 0755 redhat/varnishncsa.initrc %{buildroot}%{_initrddir}/varnishncsa
%endif
install -D -m 0755 redhat/varnish_reload_vcl %{buildroot}%{_sbindir}/varnish_reload_vcl

echo %{_libdir}/varnish > %{buildroot}%{_sysconfdir}/ld.so.conf.d/varnish-%{_arch}.conf

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_sbindir}/*
%{_bindir}/*
%{_var}/lib/varnish
%{_var}/log/varnish
%{_mandir}/man1/*.1*
%{_mandir}/man3/*.3*
%{_mandir}/man7/*.7*
%doc LICENSE README redhat/README.redhat ChangeLog
%{_docdir}/varnish/
%dir %{_sysconfdir}/varnish/
%config(noreplace) %{_sysconfdir}/varnish/default.vcl
%config(noreplace) %{_sysconfdir}/logrotate.d/varnish

# systemd from fedora 17 and rhel 7
%if 0%{?fedora} >= 17 || 0%{?rhel} >= 7
%{_unitdir}/varnish.service
%{_unitdir}/varnishncsa.service
%{_unitdir}/varnishlog.service
%config(noreplace)%{_sysconfdir}/varnish/varnish.params

# default is standard sysvinit
%else
%config(noreplace) %{_sysconfdir}/sysconfig/varnish
%{_initrddir}/varnish
%{_initrddir}/varnishlog
%{_initrddir}/varnishncsa
%endif

%files libs
%defattr(-,root,root,-)
%{_libdir}/*.so.*
%{_libdir}/varnish
%doc LICENSE
%config %{_sysconfdir}/ld.so.conf.d/varnish-%{_arch}.conf

%files libs-devel
%defattr(-,root,root,-)
%{_libdir}/lib*.so
%dir %{_includedir}/varnish
%{_includedir}/varnish/*
%{_libdir}/pkgconfig/varnishapi.pc
/usr/share/varnish
/usr/share/aclocal

%doc LICENSE

%files docs
%defattr(-,root,root,-)
%doc LICENSE
%doc doc/sphinx
%doc doc/html
%doc doc/changes*.html

#%files libs-static
#%{_libdir}/libvarnish.a
#%{_libdir}/libvarnishapi.a
#%{_libdir}/libvarnishcompat.a
#%{_libdir}/libvcc.a
#%doc LICENSE

%pre
getent group varnish >/dev/null || groupadd -r varnish
getent passwd varnish >/dev/null || \
	useradd -r -g varnish -d /var/lib/varnish -s /sbin/nologin \
		-c "Varnish Cache" varnish
exit 0

%post
%if 0%{?fedora} >= 17 || 0%{?rhel} >= 7
/bin/systemctl daemon-reload >/dev/null 2>&1 || :
%else
/sbin/chkconfig --add varnish
/sbin/chkconfig --add varnishlog
/sbin/chkconfig --add varnishncsa 
%endif
test -f /etc/varnish/secret || (uuidgen > /etc/varnish/secret && chmod 0600 /etc/varnish/secret)

%triggerun -- varnish < 3.0.2-1
# Save the current service runlevel info
# User must manually run systemd-sysv-convert --apply varnish 
# to migrate them to systemd targets
%{_bindir}/systemd-sysv-convert --save varnish >/dev/null 2>&1 ||:

# If the package is allowed to autostart:
#/bin/systemctl --no-reload enable varnish.service >/dev/null 2>&1 ||:

# Run these because the SysV package being removed won't do them
/sbin/chkconfig --del varnish >/dev/null 2>&1 || :
#/bin/systemctl try-restart varnish.service >/dev/null 2>&1 || :

%preun
if [ $1 -lt 1 ]; then
  # Package removal, not upgrade
  %if 0%{?fedora} >= 17 || 0%{?rhel} >= 7
  /bin/systemctl --no-reload disable varnish.service > /dev/null 2>&1 || :
  /bin/systemctl stop varnish.service > /dev/null 2>&1 || :
  %else
  /sbin/service varnish stop > /dev/null 2>&1
  /sbin/service varnishlog stop > /dev/null 2>&1
  /sbin/service varnishncsa stop > /dev/null 2>%1
  /sbin/chkconfig --del varnish
  /sbin/chkconfig --del varnishlog
  /sbin/chkconfig --del varnishncsa 
  %endif
fi

%post libs -p /sbin/ldconfig

%postun libs -p /sbin/ldconfig

%changelog
* Mon Mar 12 2012 Ingvar Hagelund <ingvar@redpill-linpro.com> - 3.0.2-2
- Added PrivateTmp=true to varnishd unit file, closing #782539
- Fixed comment typos in varnish unit file

* Tue Mar 06 2012 Ingvar Hagelund <ingvar@redpill-linpro.com> - 3.0.2-1
- New upstream version 3.0.2
- Removed INSTALL as requested by rpmlint
- Added a ld.so.conf.d fragment file listing libdir/varnish 
- Removed redundant doc/html/_sources
- systemd support from fedora 17
- Stopped using macros for make and install, according to 
  Fedora's packaging guidelines
- Changes merged from upstream:
  - Added suse_version macro
  - Added comments on building from a git checkout
  - mkpasswd -> uuidgen for fewer dependencies
  - Fixed missing quotes around cflags for pcre
  - Removed unnecessary 32/64 bit parallell build hack as this is fixed upstream
  - Fixed typo in configure call, disable -> without
  - Added lib/libvgz/.libs to LD_LIBRARY_PATH in make check
  - Added section 3 manpages
  - Configure with --without-rst2man --without-rst2html
  - changelog entries
- Removed unnecessary patch for system jemalloc, upstream now supports this

* Fri Feb 10 2012 Petr Pisar <ppisar@redhat.com> - 2.1.5-4
- Rebuild against PCRE 8.30

* Sat Jan 14 2012 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 2.1.5-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_17_Mass_Rebuild

* Mon Feb 07 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 2.1.5-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Tue Feb 01 2011 Ingvar Hagelund <ingvar@redpill-linpro.com> - 2.1.5-1
- New upstream release
- New download location
- Moved varnish_reload_vcl to sbin
- Removed patches included upstream
- Use jemalloc as system installed library

* Mon Nov 15 2010 Ingvar Hagelund <ingvar@redpill-linpro.com> - 3.0.0-0.svn20101115r5543
- Merged some changes from fedora
- Upped general version to 3.0 prerelease in trunk

* Thu Nov 04 2010 Ingvar Hagelund <ingvar@redpill-linpro.com> - 2.1.4-4
- Added a patch fixing a missing echo in the init script that
  masked failure output from the script
- Added a patch from upstream, fixing a problem with Content-Length
  headers (upstream r5461, upstream bug #801)
- Added a patch from upstream, adding empty Default-Start and Default-Stop
  to initscripts for better lsb compliance
- Added varnish_reload_vcl from trunk
- Synced descriptions from release spec

* Thu Oct 28 2010 Ingvar Hagelund <ingvar@redpill-linpro.com> - 2.1.4-3
- Fixed missing manpages because of no rst2man in rhel4 and 5

* Mon Oct 25 2010 Ingvar Hagelund <ingvar@redpill-linpro.com> - 2.1.4-2
- Removed RHEL6/ppc64 specific patch that has been included upstream

* Mon Oct 25 2010 Ingvar Hagelund <ingvar@redpill-linpro.com> - 2.1.4-1
- New upstream release
- New URL for source tarball and main website
- Prebuilt html docs now included, use that instead of running sphinx
- Putting sphinx generated doc in a separate subpackage
- Replaced specific include files with a wildcard glob
- Needs python-sphinx and deps to build sphinx documentation

* Tue Aug 24 2010 Ingvar Hagelund <ingvar@redpill-linpro.com> - 2.1.3-2
- Added a RHEL6/ppc64 specific patch that changes the hard coded
  stack size in tests/c00031.vtc

* Thu Jul 29 2010 Ingvar Hagelund <ingvar@redpill-linpro.com> - 2.1.4-0.svn20100824r5117
- Replaced specific include files with a wildcard glob
- Needs python-sphinx and deps to build sphinx documentation
- Builds html and latex documentation. Put that in a subpackage varnish-docs

* Thu Jul 29 2010 Ingvar Hagelund <ingvar@redpill-linpro.com> - 2.1.3-1
- New upstream release
- Add a patch for jemalloc on s390 that lacks upstream

* Wed May 05 2010 Ingvar Hagelund <ingvar@redpill-linpro.com> - 2.1.2-1
- New upstream release
- Remove patches merged upstream

* Tue Apr 27 2010 Ingvar Hagelund <ingvar@linpro.no> - 2.1.1-1
- New upstream release
- Added a fix for missing pkgconfig/libpcre.pc on rhel4
- Added a patch from trunk making the rpm buildable on lowspec
  build hosts (like Red Hat's ppc build farm nodes)
- Removed patches that are merged upstream

* Wed Apr 14 2010 Ingvar Hagelund <ingvar@linpro.no> - 2.1.0-2
- Added a patch from svn that fixes changes-2.0.6-2.1.0.xml

* Tue Apr 06 2010 Ingvar Hagelund <ingvar@linpro.no> - 2.1.0-1
- New upstream release; note: Configuration changes, see the README
- Removed unneeded patches 
- CVE-2009-2936: Added a patch from Debian that adds the -S option 
  to the varnisdh(1) manpage and to the sysconfig defaults, thus
  password-protecting the admin interface port (#579536,#579533)
- Generates that password in the post script, requires mkpasswd
- Added a patch from Robert Scheck for explicit linking to libm
- Requires pcre

* Wed Dec 23 2009 Ingvar Hagelund <ingvar@linpro.no> - 2.0.6-2
- Added a test that enables jemalloc on ppc if the kernel is
  not a rhel5 kernel (as on redhat builders)
- Removed tests c00031.vtc and r00387on rhel4/ppc as they fail
  on the Red Hat ppc builders (but works on my rhel4 ppc instance)
- Added a patch that fixes broken changes-2.0.6.html in doc

* Mon Dec 14 2009 Ingvar Hagelund <ingvar@linpro.no> - 2.0.6-1
- New upstream release
- Removed patches for libjemalloc, as they are added upstream

* Mon Nov 09 2009 Ingvar Hagelund <ingvar@linpro.no> - 2.0.5-1
- New upstream release

* Thu Aug 13 2009 Ingvar Hagelund <ingvar@linpro.no> - 2.0.4-4
- Added a sparc specific patch to libjemalloc.

* Sun Jul 26 2009 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 2.0.4-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_12_Mass_Rebuild

* Thu Jun 04 2009 Ingvar Hagelund <ingvar@linpro.no> - 2.0.4-2
- Added a s390 specific patch to libjemalloc.

* Fri Mar 27 2009 Ingvar Hagelund <ingvar@linpro.no> - 2.0.4-1
  New upstream release 2.0.4 

* Wed Feb 25 2009 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 2.0.3-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_11_Mass_Rebuild

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
- Added varnisncsa init script (Colin Hill)
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

* Fri Sep 07 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.1.1-3
- Added a patch, changeset 1913 from svn trunk. This makes varnish
  more stable under specific loads. 

* Thu Sep 06 2007 Ingvar Hagelund <ingvar@linpro.no> - 1.1.1-2
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
