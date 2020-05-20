Name:           x11spice
Version:        1.2
Release:        1%{?dist}
Summary:        Utility to share an x11 desktop via Spice
Group:          Applications/System
License:        GPLv3+
URL:            http://spice-space.org/
Source0:        http://people.freedesktop.org/~jwhite/%{name}/%{name}-%{version}.tar.gz
BuildRequires:  glib2-devel gtk2-devel libX11-devel spice-server-devel spice-protocol pixman-devel
BuildRequires:  libxcb-devel >= 1.11
BuildRequires:  xorg-x11-server-devel >= 1.17
BuildRequires:  xcb-util-devel

%global moduledir %(pkg-config xorg-server --variable=moduledir )
%global driverdir %{moduledir}/drivers

%description
Utility to share x11 desktops via Spice.


%prep
%setup -q -n %{name}-%{version}


%build
%undefine _hardened_build
%configure --enable-dummy
make %{?_smp_mflags}

%install
%make_install
rm -f %{buildroot}%{driverdir}/spicedummy_drv.la

%files
%doc COPYING ChangeLog README
%{_bindir}/x11spice
%{_bindir}/spicedummy.sh
%{_bindir}/x11spice_connected_gnome
%{_bindir}/x11spice_disconnected_gnome
%{_sysconfdir}/xdg/x11spice/x11spice.conf
%{_sysconfdir}/X11/spicedummy.conf
%{_datadir}/applications/x11spice.desktop
%{_datadir}/icons/hicolor/scalable/apps/x11spice.svg
%{_mandir}/man1/%{name}*.1*
%{driverdir}/spicedummy_drv.so


%changelog
* Wed May 20 2020 Jeremy White <jwhite@codeweavers.com> 1.2.0-1
- Include the new spice-video-dummy driver, providing graphics capability
- Refine behavior to enable use with compositing window managers like mutter

* Wed Nov 02 2016 Jeremy White <jwhite@codeweavers.com> 1.1.0-1
- Fix issues uncovered by Coverity
- Invert the logic of view only; make it the default
- Add optional audit calls
- Add callback capabilities
- Provide a connect / disconnect callback facility

* Fri Sep 02 2016 Jeremy White <jwhite@codeweavers.com> 1.0.0-1
- Initial package
