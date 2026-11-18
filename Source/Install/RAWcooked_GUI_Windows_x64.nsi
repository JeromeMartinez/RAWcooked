#NSIS: encoding=UTF-8
; Installer of the user interface, same form as the one of MediaInfo
; makensis Source\Install\RAWcooked_GUI_Windows_x64.nsi, after the package is ready in Project\Qt\x64
; (user interface, Qt libraries and plugins from windeployqt, runtime of MSVC, FFmpeg in ffmpeg\bin)

; Request application privileges for Windows Vista
RequestExecutionLevel admin

; Enable high-DPI support
ManifestDPIAware true

; Some defines
!define PRODUCT_NAME "RAWcooked"
!define PRODUCT_PUBLISHER "MediaArea.net"
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "25.12"
!endif
!define PRODUCT_VERSION4 "${PRODUCT_VERSION}.0.0"
!define PRODUCT_WEB_SITE "https://MediaArea.net/RAWcooked"
!define COMPANY_REGISTRY "Software\MediaArea"
!define PRODUCT_REGISTRY "Software\MediaArea\RAWcooked"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\RAWcooked-GUI.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"
!ifndef PACKAGE_DIR
  !define PACKAGE_DIR "..\..\Project\Qt\x64"
!endif

; Compression
SetCompressor /FINAL /SOLID lzma

; x64 stuff
!include "x64.nsh"

; Logic stuff
!include LogicLib.nsh

; File size
!include FileFunc.nsh
!include WinVer.nsh

; Modern UI
!include "MUI2.nsh"
!define MUI_ABORTWARNING
!define MUI_ICON "..\..\Source\Resource\Image\Icon.ico"
!define MUI_UNICON "..\..\Source\Resource\Image\Icon.ico"

; Language Selection Dialog Settings
!define MUI_LANGDLL_REGISTRY_ROOT "${PRODUCT_UNINST_ROOT_KEY}"
!define MUI_LANGDLL_REGISTRY_KEY "${PRODUCT_UNINST_KEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "NSIS:Language"

; Function to launch RAWcooked with same integrity level as Windows Explorer
Function LaunchRAWcookedAsCurrentUser
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\RAWcooked-GUI.exe"'
FunctionEnd

; Installer pages
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION "LaunchRAWcookedAsCurrentUser"
!insertmacro MUI_PAGE_FINISH
; Uninstaller pages
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; Language files
!insertmacro MUI_LANGUAGE "Arabic"
!insertmacro MUI_LANGUAGE "Albanian"
!insertmacro MUI_LANGUAGE "Belarusian"
!insertmacro MUI_LANGUAGE "Catalan"
!insertmacro MUI_LANGUAGE "Croatian"
!insertmacro MUI_LANGUAGE "Czech"
!insertmacro MUI_LANGUAGE "Danish"
!insertmacro MUI_LANGUAGE "Dutch"
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Farsi"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Greek"
!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "Hungarian"
!insertmacro MUI_LANGUAGE "Indonesian"
!insertmacro MUI_LANGUAGE "Italian"
!insertmacro MUI_LANGUAGE "Japanese"
!insertmacro MUI_LANGUAGE "Lithuanian"
!insertmacro MUI_LANGUAGE "Polish"
!insertmacro MUI_LANGUAGE "Portuguese"
!insertmacro MUI_LANGUAGE "PortugueseBR"
!insertmacro MUI_LANGUAGE "Romanian"
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "Swedish"
!insertmacro MUI_LANGUAGE "Thai"
!insertmacro MUI_LANGUAGE "TradChinese"
!insertmacro MUI_LANGUAGE "Turkish"
!insertmacro MUI_LANGUAGE "Ukrainian"
!insertmacro MUI_RESERVEFILE_LANGDLL

; Info
VIProductVersion "${PRODUCT_VERSION4}"
VIAddVersionKey /LANG=0 "CompanyName"      "${PRODUCT_PUBLISHER}"
VIAddVersionKey /LANG=0 "ProductName"      "${PRODUCT_NAME}"
VIAddVersionKey /LANG=0 "ProductVersion"   "${PRODUCT_VERSION4}"
VIAddVersionKey /LANG=0 "FileDescription"  "Lossless video compression of audio-visual RAW data"
VIAddVersionKey /LANG=0 "FileVersion"      "${PRODUCT_VERSION4}"
VIAddVersionKey /LANG=0 "LegalCopyright"   "${PRODUCT_PUBLISHER}"
VIAddVersionKey /LANG=0 "OriginalFilename" "${PRODUCT_NAME}_GUI_${PRODUCT_VERSION}_Windows_x64.exe"
BrandingText " "

; Modern UI end

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "..\..\Release\${PRODUCT_NAME}_GUI_${PRODUCT_VERSION}_Windows_x64.exe"
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" ""
ShowInstDetails nevershow
ShowUnInstDetails nevershow

Function .onInit
  ; Only a 64-bit x86 build (FFmpeg libraries are not loaded by 32-bit and ARM builds), ARM64 Windows runs it through emulation since Windows 11
  ${IfNot} ${IsNativeAMD64}
    ${IfNot} ${IsNativeARM64}
    ${OrIfNot} ${AtLeastWin11}
      MessageBox MB_OK|MB_ICONSTOP "${PRODUCT_NAME} needs a 64-bit Windows."
      Abort
    ${EndIf}
  ${EndIf}
  SetRegView 64
  !insertmacro MUI_LANGDLL_DISPLAY

  ; Increment install count
  ReadRegDWORD $0 HKCU "${PRODUCT_REGISTRY}" "InstallCount"
  IntOp $0 $0 + 1
  WriteRegDWORD HKCU "${PRODUCT_REGISTRY}" "InstallCount" $0
FunctionEnd

Section "SectionPrincipale" SEC01
  SetOverwrite on
  SetOutPath "$SMPROGRAMS"
  CreateShortCut "$SMPROGRAMS\RAWcooked.lnk" "$INSTDIR\RAWcooked-GUI.exe" "" "" "" "" "" "Encode and decode audio-visual RAW data with Matroska, FFV1 and FLAC"
  SetOutPath "$INSTDIR"

  ; Architecture neutral files
  File "..\..\License.html"

  ; The package: user interface, Qt, runtime of MSVC and FFmpeg
  ; Files of a previous version which are not in this one are removed first (e.g. Qt plugins)
  Call RemovePackage
  File /r "${PACKAGE_DIR}\*.*"

  # Create files
  WriteIniStr "$INSTDIR\${PRODUCT_NAME}.url" "InternetShortcut" "URL" "${PRODUCT_WEB_SITE}"
SectionEnd

Section -Post
  WriteUninstaller "$INSTDIR\uninst.exe"
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\RAWcooked-GUI.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName"          "$(^Name)"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher"            "${PRODUCT_PUBLISHER}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString"      "$\"$INSTDIR\uninst.exe$\""
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "QuietUninstallString" "$\"$INSTDIR\uninst.exe$\" /S"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayIcon"          "$INSTDIR\RAWcooked-GUI.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion"       "${PRODUCT_VERSION}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout"         "${PRODUCT_WEB_SITE}"

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0 ; Convert the decimal KB value in $0 to DWORD, put it right back into $0
  WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "EstimatedSize" "$0" ; Create/Write the reg key with the dword value
SectionEnd

; Files of the package (the list depends on the versions of Qt and FFmpeg): executables and libraries in the directory, directories of the plugins and of FFmpeg
; Other files of the directory (e.g. files of the user if the directory was not a new one) are kept
!macro RemovePackage_Macro un
Function ${un}RemovePackage
  Delete "$INSTDIR\RAWcooked-GUI.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\vc_redist.x64.exe"
  RMDir /r "$INSTDIR\ffmpeg"
  RMDir /r "$INSTDIR\generic"
  RMDir /r "$INSTDIR\iconengines"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\networkinformation"
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\tls"
  RMDir /r "$INSTDIR\translations"
FunctionEnd
!macroend
!insertmacro RemovePackage_Macro ""
!insertmacro RemovePackage_Macro "un."

Section Uninstall
  SetRegView 64
  Call un.RemovePackage
  Delete "$INSTDIR\${PRODUCT_NAME}.url"
  Delete "$INSTDIR\uninst.exe"
  Delete "$INSTDIR\License.html"
  Delete "$SMPROGRAMS\RAWcooked.lnk"
  RMDir "$INSTDIR"

  ; The settings of the user interface are in the profile of the user and are kept
  DeleteRegKey HKLM "${PRODUCT_REGISTRY}"
  DeleteRegKey /ifempty HKLM "${COMPANY_REGISTRY}"
  DeleteRegKey HKCU "${PRODUCT_REGISTRY}"
  DeleteRegKey /ifempty HKCU "${COMPANY_REGISTRY}"
  DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
  SetAutoClose true
SectionEnd
