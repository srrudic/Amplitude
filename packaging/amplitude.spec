# The .rpm package, built by "make rpm":
#
#     rpmbuild -bb --define "version 0.2.6" --define "srcroot <project>" \
#              --define "_topdir <project>/build/rpm" packaging/amplitude.spec
#
# Like the .deb, it packs the player that "make" has already built rather
# than compiling again, so there is no %prep or %build step. The files and
# their places are the same as in the .deb.

Name:           amplitude
Version:        %{version}
Release:        1
Summary:        Lightweight audio player
License:        GPL-3.0-or-later
URL:            https://amplitude.cr.rs
Packager:       %{?packager_name}%{!?packager_name:Amplitude}
Recommends:     (zenity or kdialog)

# The binary is already stripped; nothing to extract or post-process.
%global debug_package %{nil}
%global __os_install_post %{nil}
%global _build_id_links none

%description
Amplitude is a lightweight audio player for your own files, with a tiny
executable and memory footprint. It plays MP3, FLAC, WAV, Ogg Vorbis,
Opus, AAC/M4A and tracker modules (MOD, XM, S3M, IT), and loads
classic .wsz skins.

%install
install -Dm755 %{srcroot}/build/linux/amplitude %{buildroot}%{_bindir}/amplitude
install -Dm644 %{srcroot}/packaging/amplitude.desktop %{buildroot}%{_datadir}/applications/amplitude.desktop
install -Dm644 %{srcroot}/packaging/amplitude-enqueue.desktop %{buildroot}%{_datadir}/kio/servicemenus/amplitude-enqueue.desktop
install -Dm644 %{srcroot}/packaging/amplitude-audiocd.desktop %{buildroot}%{_datadir}/solid/actions/amplitude-audiocd.desktop
install -Dm644 %{srcroot}/assets/amplitude.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/amplitude.svg
for size in 16 24 32 48 64 128 256; do
    install -Dm644 %{srcroot}/assets/icons/amplitude-$size.png \
        %{buildroot}%{_datadir}/icons/hicolor/${size}x${size}/apps/amplitude.png
done
install -Dm644 %{srcroot}/build/rpm/amplitude.metainfo.xml %{buildroot}%{_datadir}/metainfo/amplitude.metainfo.xml
install -Dm644 %{srcroot}/LICENSE %{buildroot}%{_datadir}/licenses/amplitude/LICENSE
install -Dm644 %{srcroot}/packaging/copyright %{buildroot}%{_datadir}/doc/amplitude/copyright

%files
%{_bindir}/amplitude
%{_datadir}/applications/amplitude.desktop
%{_datadir}/kio/servicemenus/amplitude-enqueue.desktop
%{_datadir}/solid/actions/amplitude-audiocd.desktop
%{_datadir}/icons/hicolor/*/apps/amplitude.*
%{_datadir}/metainfo/amplitude.metainfo.xml
%license %{_datadir}/licenses/amplitude/LICENSE
%doc %{_datadir}/doc/amplitude/copyright
