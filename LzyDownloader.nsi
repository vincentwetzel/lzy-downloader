!include "MUI2.nsh"
!include "LogicLib.nsh"

!ifndef APP_VERSION
  !error "APP_VERSION must be supplied to makensis (for example /DAPP_VERSION=x.y.z)"
!endif

!ifndef RELEASE_BUILD_DIR
  !define RELEASE_BUILD_DIR "build-release\Release"
!endif

; Set this only for a release after the Chrome Web Store extension ID is final.
; An empty value leaves host registration disabled rather than using a wildcard.
!ifndef BROWSER_EXTENSION_ID
  !define BROWSER_EXTENSION_ID ""
!endif

;--------------------------------
; General Configuration
Name "LzyDownloader"
OutFile "LzyDownloader-Setup-${APP_VERSION}.exe"
InstallDir "$PROGRAMFILES64\LzyDownloader"
InstallDirRegKey HKLM "Software\LzyDownloader" "InstallLocation"
RequestExecutionLevel admin

;--------------------------------
; Interface Settings
!define MUI_ABORTWARNING

; Uncomment these if you have an icon file available in your source tree
; !define MUI_ICON "src\resources\icon.ico"
; !define MUI_UNICON "src\resources\icon.ico"

;--------------------------------
; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\LzyDownloader.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch LzyDownloader"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages
!insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Installer Section
Section "Install"
    ; Ensure the app is closed before installing over it
    ExecWait 'taskkill /F /IM LzyDownloader.exe /T'

    ; Remove old installation files to prevent DLL conflicts from prior versions
    RMDir /r "$INSTDIR"

    SetOutPath "$INSTDIR"

    ; Copy all files from the CMake release output directory
    ; This includes LzyDownloader.exe, Qt DLLs, plugins, and extractor JSONs
    File /r "${RELEASE_BUILD_DIR}\*.*"

    !if "${BROWSER_EXTENSION_ID}" != ""
        ; Register the exact production extension origin for the current user.
        ; The host manifest is generated here so its path matches this install.
        FileOpen $0 "$INSTDIR\com.lzydownloader.browser.json" w
        FileWrite $0 '{$\r$\n'
        FileWrite $0 '  "name": "com.lzydownloader.browser",$\r$\n'
        FileWrite $0 '  "description": "LzyDownloader local browser companion",$\r$\n'
        FileWrite $0 '  "path": "$INSTDIR\\LzyDownloaderBrowserHost.exe",$\r$\n'
        FileWrite $0 '  "type": "stdio",$\r$\n'
        FileWrite $0 '  "allowed_origins": ["chrome-extension://${BROWSER_EXTENSION_ID}/"]$\r$\n'
        FileWrite $0 '}$\r$\n'
        FileClose $0
        WriteRegStr HKCU "Software\Google\Chrome\NativeMessagingHosts\com.lzydownloader.browser" "" "$INSTDIR\com.lzydownloader.browser.json"
        WriteRegStr HKCU "Software\Chromium\NativeMessagingHosts\com.lzydownloader.browser" "" "$INSTDIR\com.lzydownloader.browser.json"
    !endif

    ; Create Start Menu shortcut
    CreateDirectory "$SMPROGRAMS\LzyDownloader"
    CreateShortcut "$SMPROGRAMS\LzyDownloader\LzyDownloader.lnk" "$INSTDIR\LzyDownloader.exe"
    
    ; Create Desktop shortcut
    CreateShortcut "$DESKTOP\LzyDownloader.lnk" "$INSTDIR\LzyDownloader.exe"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Write registry keys for Windows "Add/Remove Programs"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\LzyDownloader" "DisplayName" "LzyDownloader"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\LzyDownloader" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\LzyDownloader" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\LzyDownloader" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\LzyDownloader" "DisplayIcon" "$INSTDIR\LzyDownloader.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\LzyDownloader" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\LzyDownloader" "Publisher" "Vincent Wetzel"

    ; The application updater uses /S, which suppresses the finish page. Relaunch
    ; the freshly installed application explicitly for that silent update path.
    IfSilent 0 +2
    Exec '"$INSTDIR\LzyDownloader.exe"'
SectionEnd

;--------------------------------
; Uninstaller Section
Section "Uninstall"
    ; Ensure the app is closed before uninstalling
    ExecWait 'taskkill /F /IM LzyDownloader.exe /T'

    ; Remove installation directory and files
    RMDir /r "$INSTDIR"

    !if "${BROWSER_EXTENSION_ID}" != ""
        DeleteRegKey HKCU "Software\Google\Chrome\NativeMessagingHosts\com.lzydownloader.browser"
        DeleteRegKey HKCU "Software\Chromium\NativeMessagingHosts\com.lzydownloader.browser"
    !endif

    ; Remove shortcuts
    Delete "$DESKTOP\LzyDownloader.lnk"
    RMDir /r "$SMPROGRAMS\LzyDownloader"

    ; Remove registry keys
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\LzyDownloader"
    
    ; Note: %LOCALAPPDATA%\LzyDownloader is intentionally left intact 
    ; to preserve the user's settings.ini, download_archive.db, and log files.
SectionEnd
