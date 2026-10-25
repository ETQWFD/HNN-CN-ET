; HNN CN ET Desktop Installer (NSIS 3.x, Unicode)
Unicode True

!include "MUI2.nsh"
!include "FileFunc.nsh"

; ---------- Metadata (ET) ----------
VIProductVersion "1.0.8.0"
VIAddVersionKey  "ProductName" "HNN CN ET"
VIAddVersionKey  "CompanyName" "ET"
VIAddVersionKey  "LegalCopyright" "Copyright (c) ET. All rights reserved."
VIAddVersionKey  "FileDescription" "HNN CN ET Desktop Installer"
VIAddVersionKey  "FileVersion" "1.0.8.0"
VIAddVersionKey  "ProductVersion" "1.0.8"

Name "HNN CN ET"
OutFile "/home/user/Doubao/chats/38447749949189634/work/desktop/HNN-CN-ET-Setup-1.0.8.exe"
InstallDir "$PROGRAMFILES64\HNN CN ET"
RequestExecutionLevel admin

; ---------- UI ----------
!define MUI_ABORTWARNING
!define MUI_ICON "resources\app.ico"
!define MUI_UNICON "resources\app.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\HNN_CN_ET_Desktop.exe"
!define MUI_FINISHPAGE_RUN_TEXT "启动 HNN CN ET"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; ---------- Install ----------
Section "Main" SEC_MAIN
  SetOutPath "$INSTDIR"
  ; v1.0.8 fix: NSIS Linux "File /r" recursion is broken -> pack every runtime file
  ; explicitly with absolute paths so the installer really contains the full runtime.
  !define PAYLOAD "/home/user/Doubao/chats/38447749949189634/work/dist/HNN-CN-ET"
  File "${PAYLOAD}/HNN_CN_ET_Desktop.exe"
  File "${PAYLOAD}/Qt5Core.dll"
  File "${PAYLOAD}/Qt5Gui.dll"
  File "${PAYLOAD}/Qt5Widgets.dll"
  File "${PAYLOAD}/Qt5Network.dll"
  File "${PAYLOAD}/Qt5Sql.dll"
  File "${PAYLOAD}/libgcc_s_seh-1.dll"
  File "${PAYLOAD}/libstdc++-6.dll"
  File "${PAYLOAD}/libwinpthread-1.dll"
  ; v1.0.8 hotfix: Qt platform plugins MUST live in their own subdirectories
  ; (platforms\ / imageformats\ / styles\). Without SetOutPath per folder NSIS
  ; flattens them into $INSTDIR and real Windows Qt cannot find the platform
  ; plugin -> "no Qt platform plugin could be initialized".
  SetOutPath "$INSTDIR\platforms"
  File "${PAYLOAD}/platforms/qwindows.dll"
  SetOutPath "$INSTDIR\imageformats"
  File "${PAYLOAD}/imageformats/qgif.dll"
  File "${PAYLOAD}/imageformats/qico.dll"
  File "${PAYLOAD}/imageformats/qjpeg.dll"
  SetOutPath "$INSTDIR\styles"
  File "${PAYLOAD}/styles/qwindowsvistastyle.dll"
  ; qt.conf pins the plugin search path to the app dir, extra insurance.
  SetOutPath "$INSTDIR"
  File "${PAYLOAD}/qt.conf"

  ; start menu + desktop shortcuts
  CreateDirectory "$SMPROGRAMS\HNN CN ET"
  CreateShortCut "$SMPROGRAMS\HNN CN ET\HNN CN ET.lnk" "$INSTDIR\HNN_CN_ET_Desktop.exe"
  CreateShortCut "$SMPROGRAMS\HNN CN ET\卸载 HNN CN ET.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$DESKTOP\HNN CN ET.lnk" "$INSTDIR\HNN_CN_ET_Desktop.exe"

  ; uninstaller registry
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "DisplayName" "HNN CN ET"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "DisplayVersion" "1.0.8"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "Publisher" "ET"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "DisplayIcon" "$INSTDIR\HNN_CN_ET_Desktop.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\HNN CN ET\HNN CN ET.lnk"
  Delete "$SMPROGRAMS\HNN CN ET\卸载 HNN CN ET.lnk"
  RMDir "$SMPROGRAMS\HNN CN ET"
  Delete "$DESKTOP\HNN CN ET.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET"
SectionEnd
