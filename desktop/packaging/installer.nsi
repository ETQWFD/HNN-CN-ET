; HNN CN ET Desktop Installer (NSIS 3.x, Unicode)
Unicode True

!include "MUI2.nsh"
!include "FileFunc.nsh"

; ---------- Metadata (ET) ----------
VIProductVersion "1.0.0.0"
VIAddVersionKey  "ProductName" "HNN CN ET"
VIAddVersionKey  "CompanyName" "ET"
VIAddVersionKey  "LegalCopyright" "Copyright (c) ET. All rights reserved."
VIAddVersionKey  "FileDescription" "HNN CN ET Desktop Installer"
VIAddVersionKey  "FileVersion" "1.0.0.0"
VIAddVersionKey  "ProductVersion" "1.0.0"

Name "HNN CN ET"
OutFile "..\..\HNN-CN-ET-Setup-1.0.0.exe"
InstallDir "$PROGRAMFILES64\HNN CN ET"
RequestExecutionLevel admin

; ---------- UI ----------
!define MUI_ABORTWARNING
!define MUI_ICON "resources\app.ico"
!define MUI_UNICON "resources\app.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\HNN-CN-ET.exe"
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
  File /r "..\..\dist\HNN-CN-ET\*.*"

  ; start menu + desktop shortcuts
  CreateDirectory "$SMPROGRAMS\HNN CN ET"
  CreateShortCut "$SMPROGRAMS\HNN CN ET\HNN CN ET.lnk" "$INSTDIR\HNN-CN-ET.exe"
  CreateShortCut "$SMPROGRAMS\HNN CN ET\卸载 HNN CN ET.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$DESKTOP\HNN CN ET.lnk" "$INSTDIR\HNN-CN-ET.exe"

  ; uninstaller registry
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "DisplayName" "HNN CN ET"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "DisplayVersion" "1.0.0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "Publisher" "ET"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HNNCNET" "DisplayIcon" "$INSTDIR\HNN-CN-ET.exe"
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
