; The Windows installers, built with NSIS by "make installer":
;
;     makensis -NOCD -DVERSION=0.1.1 -DBITS=32 -DOUTFILE=build/amplitude-0.1.1-win32-setup.exe packaging/installer.nsi
;
; The script is compiled twice, with BITS set to 32 and to 64, giving one
; installer per build of the player. The 32-bit one installs on every
; Windows; the 64-bit one refuses to run on a 32-bit system. Paths below are
; relative to the project root (that is what -NOCD is for).
;
; "Unicode false" makes an ANSI installer, the only kind that starts on
; Windows 95/98/ME. The price is that an installation folder typed in by
; hand is limited to the characters of the system's code page.

Unicode false
SetCompressor /SOLID lzma
RequestExecutionLevel admin     ; writes to Program Files and the all-users Start menu

!include "MUI2.nsh"
!include "x64.nsh"

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Amplitude"

; --- File associations ---------------------------------------------------------
; All audio types share one file class, PROGID. An extension is tied to it
; in three ways, to cover every Windows:
;   - as the extension's default class, which is what decides the double-click
;     program up to Windows 7 (the class that was there is kept in a
;     "Amplitude.Backup" value and put back by the uninstaller);
;   - in the extension's OpenWithProgids list, which puts Amplitude in the
;     "Open with" menu;
;   - in Amplitude's "Capabilities", which lists it under Default Programs /
;     Default apps. From Windows 8 on only the user can change a default, and
;     that is where they do it.
; The extensions match is_audio_file() in src/main.c, plus .m3u playlists and
; minus .mp4, which is usually video.
!define PROGID "Amplitude.AudioFile"
!define CLASSES "Software\Classes"
!define CAPABILITIES "Software\Amplitude\Capabilities"

!macro EACH_EXTENSION ACTION
    !insertmacro ${ACTION} ".mp3"
    !insertmacro ${ACTION} ".flac"
    !insertmacro ${ACTION} ".wav"
    !insertmacro ${ACTION} ".ogg"
    !insertmacro ${ACTION} ".oga"
    !insertmacro ${ACTION} ".opus"
    !insertmacro ${ACTION} ".m4a"
    !insertmacro ${ACTION} ".m4b"
    !insertmacro ${ACTION} ".aac"
    !insertmacro ${ACTION} ".mod"
    !insertmacro ${ACTION} ".xm"
    !insertmacro ${ACTION} ".s3m"
    !insertmacro ${ACTION} ".it"
    !insertmacro ${ACTION} ".m3u"
!macroend

!macro ASSOCIATE EXT
    ReadRegStr $0 HKLM "${CLASSES}\${EXT}" ""
    ${If} $0 != "${PROGID}"
        ${If} $0 != ""
            WriteRegStr HKLM "${CLASSES}\${EXT}" "Amplitude.Backup" "$0"
        ${EndIf}
        WriteRegStr HKLM "${CLASSES}\${EXT}" "" "${PROGID}"
    ${EndIf}
    WriteRegStr HKLM "${CLASSES}\${EXT}\OpenWithProgids" "${PROGID}" ""
    WriteRegStr HKLM "${CAPABILITIES}\FileAssociations" "${EXT}" "${PROGID}"
!macroend

!macro UNASSOCIATE EXT
    ReadRegStr $0 HKLM "${CLASSES}\${EXT}" ""
    ${If} $0 == "${PROGID}"
        ReadRegStr $1 HKLM "${CLASSES}\${EXT}" "Amplitude.Backup"
        ${If} $1 != ""
            WriteRegStr HKLM "${CLASSES}\${EXT}" "" "$1"
        ${Else}
            DeleteRegValue HKLM "${CLASSES}\${EXT}" ""
        ${EndIf}
    ${EndIf}
    DeleteRegValue HKLM "${CLASSES}\${EXT}" "Amplitude.Backup"
    DeleteRegValue HKLM "${CLASSES}\${EXT}\OpenWithProgids" "${PROGID}"
!macroend

; File classes are looked up in the system's own registry view, not the
; 32-bit one the rest of the installer uses.
!macro NATIVE_REGISTRY
    ${If} ${RunningX64}
        SetRegView 64
    ${EndIf}
!macroend

; Tells Explorer that associations changed, so icons update at once.
!macro REFRESH_SHELL
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
!macroend

!if ${BITS} == 64
Name "Amplitude (64-bit)"
!else
Name "Amplitude"
!endif
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES32\Amplitude"
InstallDirRegKey HKLM "Software\Amplitude" "InstallDir"    ; an upgrade goes where the last version went
BrandingText "Amplitude ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Amplitude"
VIAddVersionKey "FileDescription" "Amplitude installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Srdjan Rudic, GPL-3.0-or-later"

!define MUI_ICON "packaging\amplitude.ico"
!define MUI_UNICON "packaging\amplitude.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC

!define MUI_WELCOMEPAGE_TEXT "This will install Amplitude ${VERSION}, a lightweight audio player, on your computer.$\r$\n$\r$\nIf Amplitude is running, close it before you continue."
!insertmacro MUI_PAGE_WELCOME
!define MUI_LICENSEPAGE_TEXT_BOTTOM "Amplitude is free software. You do not have to accept this licence to use it; it only sets the terms for copying and changing it."
!define MUI_LICENSEPAGE_BUTTON "&Next >"
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
    ; The registry is always used through the 32-bit view, so the installer
    ; finds its own entries again on every system. Only the default folder
    ; differs: the 64-bit player belongs in the 64-bit Program Files. (If
    ; the other installer was used before, this one replaces that copy.)
!if ${BITS} == 64
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "This is the 64-bit version of Amplitude, and this Windows is 32-bit.$\r$\n$\r$\nPlease use the 32-bit installer instead."
        Abort
    ${EndIf}
    ${If} $INSTDIR == "$PROGRAMFILES32\Amplitude"
        StrCpy $INSTDIR "$PROGRAMFILES64\Amplitude"
    ${EndIf}
!endif
FunctionEnd

Section "Amplitude" SecPlayer
    SectionIn RO
    SetOutPath "$INSTDIR"
    File "build\win${BITS}\amplitude.exe"
    File /oname=LICENSE.txt "LICENSE"
    WriteUninstaller "$INSTDIR\uninstall.exe"

    WriteRegStr HKLM "Software\Amplitude" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "Amplitude"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "Srdjan Rudic"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "https://amplitude.cr.rs"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\amplitude.exe"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" 1400      ; KB
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Start menu shortcut" SecStartMenu
    SetShellVarContext all
    CreateShortCut "$SMPROGRAMS\Amplitude.lnk" "$INSTDIR\amplitude.exe"
SectionEnd

Section /o "Desktop shortcut" SecDesktop
    SetShellVarContext all
    CreateShortCut "$DESKTOP\Amplitude.lnk" "$INSTDIR\amplitude.exe"
SectionEnd

Section "Open audio files with Amplitude" SecAssociations
    !insertmacro NATIVE_REGISTRY
    WriteRegStr HKLM "${CLASSES}\${PROGID}" "" "Amplitude audio file"
    WriteRegStr HKLM "${CLASSES}\${PROGID}\DefaultIcon" "" "$INSTDIR\amplitude.exe,0"
    WriteRegStr HKLM "${CLASSES}\${PROGID}\shell" "" "open"
    WriteRegStr HKLM "${CLASSES}\${PROGID}\shell\open" "" "&Play in Amplitude"
    WriteRegStr HKLM "${CLASSES}\${PROGID}\shell\open\command" "" '"$INSTDIR\amplitude.exe" "%1"'
    WriteRegStr HKLM "${CLASSES}\${PROGID}\shell\enqueue" "" "&Enqueue in Amplitude"
    WriteRegStr HKLM "${CLASSES}\${PROGID}\shell\enqueue\command" "" '"$INSTDIR\amplitude.exe" --enqueue "%1"'

    WriteRegStr HKLM "${CAPABILITIES}" "ApplicationName" "Amplitude"
    WriteRegStr HKLM "${CAPABILITIES}" "ApplicationDescription" "A lightweight audio player."
    WriteRegStr HKLM "Software\RegisteredApplications" "Amplitude" "${CAPABILITIES}"
    !insertmacro EACH_EXTENSION ASSOCIATE
    SetRegView 32
    !insertmacro REFRESH_SHELL
SectionEnd

; Settings and the saved playlist (in Application Data) are left alone, so
; an upgrade or reinstall keeps them.
Section "Uninstall"
    SetShellVarContext all
    Delete "$SMPROGRAMS\Amplitude.lnk"
    Delete "$DESKTOP\Amplitude.lnk"
    Delete "$INSTDIR\amplitude.exe"
    Delete "$INSTDIR\LICENSE.txt"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
    DeleteRegKey HKLM "${UNINSTALL_KEY}"
    DeleteRegKey HKLM "Software\Amplitude"

    ; Harmless if the associations were never installed.
    !insertmacro NATIVE_REGISTRY
    !insertmacro EACH_EXTENSION UNASSOCIATE
    DeleteRegKey HKLM "${CLASSES}\${PROGID}"
    DeleteRegValue HKLM "Software\RegisteredApplications" "Amplitude"
    DeleteRegKey HKLM "Software\Amplitude"
    SetRegView 32
    !insertmacro REFRESH_SHELL
SectionEnd
