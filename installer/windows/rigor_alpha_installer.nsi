; RIGOR Windows production installer (v0.2.1 display identity).
; Output: dist/alpha/windows/rigor-v0.2-windows.exe
; Build: bash scripts/alpha/build_alpha_windows_nsis.sh

!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!ifndef REPO_ROOT
  !define REPO_ROOT "."
!endif
!ifndef STAGING_DIR
  !define STAGING_DIR "${REPO_ROOT}/dist/alpha/windows/staging"
!endif
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "0.2.1"
!endif
!ifndef OUTFILE
  !define OUTFILE "${REPO_ROOT}/dist/alpha/windows/rigor-v0.2-windows.exe"
!endif

Name "Rigor Wallet"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\Rigor Wallet"
RequestExecutionLevel admin
; Non-solid lzma: /SOLID on Linux makensis has produced CRC/"NSIS Error" installers
; with large static Qt PE payloads (~50–80 MiB). Per-file lzma is more reliable cross-build.
SetCompressor lzma
Unicode true
CRCCheck on

!define REGKEY "SOFTWARE\Rigor Wallet"
!define COMPANY "Rigor Protocol"
!define DATADIR_HINT "$APPDATA\RIGOR"
BrandingText "RIGOR v${PRODUCT_VERSION}"

!define MUI_ICON "${REPO_ROOT}/core/share/icons/rigor/rigor.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${REPO_ROOT}/core/share/pixmaps/nsis-wizard.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${REPO_ROOT}/core/share/pixmaps/nsis-header.bmp"
!define MUI_FINISHPAGE_RUN "$INSTDIR\rigor-wallet.exe"
!define MUI_ABORTWARNING

VIProductVersion 0.2.1.0
VIAddVersionKey ProductName "Rigor Wallet"
VIAddVersionKey ProductVersion "${PRODUCT_VERSION}"
VIAddVersionKey FileVersion "${PRODUCT_VERSION}"
VIAddVersionKey CompanyName "${COMPANY}"
VIAddVersionKey FileDescription "Rigor Wallet v${PRODUCT_VERSION} Installer (unsigned)"
VIAddVersionKey LegalCopyright "Copyright (C) 2026 Rigor contributors"

!insertmacro MUI_PAGE_WELCOME
; Forward slashes: makensis on Linux/WSL cannot open Windows-style backslashes.
!insertmacro MUI_PAGE_LICENSE "${REPO_ROOT}/installer/windows/RIGOR_LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE English

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Rigor Wallet requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Section "Rigor Wallet (required)" SEC_MAIN
  SectionIn RO
  SetOutPath "$INSTDIR"
  SetOverwrite on

  ; Overlay installs into an old Alpha folder leave miner / README-ALPHA / boot stubs.
  ; If rigor-wallet.exe is still running, replacing it silently fails and About
  ; keeps showing the previous display stamp.
  ClearErrors
  IfFileExists "$INSTDIR\rigor-wallet.exe" 0 skip_wallet_lock_check
    Delete "$INSTDIR\rigor-wallet.exe"
    IfErrors 0 skip_wallet_lock_check
      MessageBox MB_OK|MB_ICONSTOP "Cannot replace rigor-wallet.exe. Quit Rigor Wallet completely (including the tray icon) and run setup again."
      Abort
  skip_wallet_lock_check:
  Delete "$INSTDIR\rigor-miner.exe"
  Delete "$INSTDIR\rigor-miner.cmd"
  Delete "$INSTDIR\minerd.exe"
  Delete "$INSTDIR\README-ALPHA.txt"
  Delete "$INSTDIR\rigor-wallet-boot.exe"
  Delete "$INSTDIR\rigor-qt.exe"
  Delete "$SMPROGRAMS\Rigor Wallet\Rigor Miner.lnk"
  Delete "$SMPROGRAMS\Rigor Alpha\Rigor Miner.lnk"
  Delete "$DESKTOP\Rigor Miner.lnk"

  ; Launcher at root. Real GUI is internal\rigor-qt.exe with -datadir=<install>\data.
  File "${STAGING_DIR}/rigor-wallet.exe"
  File "${STAGING_DIR}/rigord.exe"
  File "${STAGING_DIR}/rigor-cli.exe"
  File /oname=README.txt "${STAGING_DIR}/README.txt"
  File /oname=LICENSE.txt "${REPO_ROOT}/installer/windows/RIGOR_LICENSE.txt"

  SetOutPath "$INSTDIR\internal"
  File "${STAGING_DIR}/internal/rigor-qt.exe"

  ; Config template + datadir guidance
  SetOutPath "$INSTDIR\config"
  File "${STAGING_DIR}/rigor.conf.template"
  File "${STAGING_DIR}/DATADIR.txt"

  ; Qt runtime (windeployqt) when staged
!ifdef INCLUDE_QT_RUNTIME
  SetOutPath "$INSTDIR"
  File /r "${STAGING_DIR}/qt-runtime/"
!endif

  ; Portable data directory next to the program (Litecoin-style). Not %APPDATA%.
  CreateDirectory "$INSTDIR\data"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Working directory must be $INSTDIR (not config\) or shortcuts start in the wrong folder.
  SetOutPath "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\Rigor Wallet"
  CreateShortcut "$SMPROGRAMS\Rigor Wallet\Rigor Wallet.lnk" "$INSTDIR\rigor-wallet.exe"
  CreateShortcut "$SMPROGRAMS\Rigor Wallet\Rigor CLI.lnk" "$INSTDIR\rigor-cli.exe"
  CreateShortcut "$SMPROGRAMS\Rigor Wallet\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  ; Remove retired Alpha start-menu folder if a previous build left it behind.
  RMDir /r "$SMPROGRAMS\Rigor Alpha"

  WriteRegStr HKLM "${REGKEY}" "Path" "$INSTDIR"
  WriteRegStr HKLM "${REGKEY}" "Version" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${REGKEY}" "DataDir" "$INSTDIR\data"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RigorWallet" "DisplayName" "Rigor Wallet"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RigorWallet" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RigorWallet" "DisplayIcon" "$INSTDIR\rigor-wallet.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RigorWallet" "Publisher" "${COMPANY}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RigorWallet" "DisplayVersion" "${PRODUCT_VERSION}"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RigorAlpha"
  DeleteRegKey HKLM "SOFTWARE\Rigor Alpha"
SectionEnd

Section "Desktop shortcut" SEC_DESKTOP
  SetOutPath "$INSTDIR"
  CreateShortcut "$DESKTOP\Rigor Wallet.lnk" "$INSTDIR\rigor-wallet.exe"
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\rigor-wallet.exe"
  Delete "$INSTDIR\rigor-qt.exe"
  RMDir /r "$INSTDIR\internal"
  Delete "$INSTDIR\rigord.exe"
  Delete "$INSTDIR\rigor-miner.exe"
  Delete "$INSTDIR\rigor-cli.exe"
  Delete "$INSTDIR\start-wallet.cmd"
  Delete "$INSTDIR\rigor-wallet-boot.exe"
  Delete "$SMPROGRAMS\Rigor Wallet\Rigor Miner.lnk"
  Delete "$DESKTOP\Rigor Miner.lnk"
  Delete "$INSTDIR\rigor-miner.cmd"
  Delete "$INSTDIR\rigor-cli.cmd"
  Delete "$INSTDIR\load-rpc-auth.cmd"
  Delete "$INSTDIR\start-node.cmd"
  Delete "$INSTDIR\start-peer2.cmd"
  Delete "$INSTDIR\fix-miningresfeeaddr.cmd"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\README-ALPHA.txt"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\COPYING"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR\config"
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\qt-runtime"
  Delete "$INSTDIR\*.dll"
  RMDir /r "$SMPROGRAMS\Rigor Wallet"
  RMDir /r "$SMPROGRAMS\Rigor Alpha"
  RMDir /r "$SMPROGRAMS\RIGOR Alpha"
  Delete "$DESKTOP\Rigor Wallet.lnk"
  Delete "$DESKTOP\RIGOR Wallet.lnk"
  DeleteRegKey HKLM "${REGKEY}"
  DeleteRegKey HKLM "SOFTWARE\Rigor Alpha"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RigorWallet"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RigorAlpha"
  RMDir "$INSTDIR"
  ; Datadir under %APPDATA%\RIGOR is retained (user funds). Remove manually if desired.
SectionEnd

LangString DESC_SEC_MAIN ${LANG_ENGLISH} "Rigor Wallet, node, and CLI. Mining software is not included."
LangString DESC_SEC_DESKTOP ${LANG_ENGLISH} "Optional desktop shortcut for Rigor Wallet."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_MAIN} $(DESC_SEC_MAIN)
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP} $(DESC_SEC_DESKTOP)
!insertmacro MUI_FUNCTION_DESCRIPTION_END
