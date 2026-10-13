Name:           page-trees
Version:        %{version}
Release:        1%{?dist}
Summary:        The page trees, a screensaver from "Anathem"
License:        MIT
Source0:        page-trees-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
The page trees from Neal Stephenson's novel Anathem: a grove inside the concent
through one year. The leaves bud, broaden and turn, are picked by hand in
autumn and strung on lines to dry, then pressed under stones for a hundred
years and cut square in a frame to make pages.

It begins at the season it really is.

On Wayland desktops it starts by itself after five minutes idle.
page-trees-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/page-trees

%files
%license LICENSE
%doc README.md
%{_bindir}/page-trees
%{_bindir}/page-trees-screensaver
%{_datadir}/applications/page-trees.desktop
%dir %{_datadir}/screensaver-service
%dir %{_datadir}/screensaver-service/savers
%{_datadir}/screensaver-service/savers/page-trees.desktop
%{_datadir}/icons/hicolor/256x256/apps/page-trees.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/page-trees-screensaver.desktop
