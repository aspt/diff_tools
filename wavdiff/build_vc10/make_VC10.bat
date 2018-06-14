if exist "%VS140COMNTOOLS%\vsvars32.bat" call "%VS140COMNTOOLS%\vsvars32.bat"
devenv WavDiff.sln  /reBuild "Release|Win32" 
if ERRORLEVEL 1 goto :EOF


