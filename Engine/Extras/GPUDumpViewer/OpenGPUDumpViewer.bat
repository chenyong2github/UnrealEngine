set CWD=%cd%

REM --allow-file-access-from-files allow to load a file from a file:// webpage required for GPUDumpViewer.html to work.
REM --user-data-dir is required to force chrome to open a new instance so that --allow-file-access-from-files is honored.
"C:\Program Files\Google\Chrome\Application\chrome.exe" file://%CWD%/GPUDumpViewer.html --allow-file-access-from-files --new-window --incognito --user-data-dir=%CWD%/.tmp_chrome_data/
