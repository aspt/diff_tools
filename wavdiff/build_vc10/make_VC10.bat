if exist "C:\Program Files\Microsoft Visual Studio 10.0\VC\vcvarsall.bat"   call "C:\Program Files\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" 
devenv WavDiff.sln  /reBuild "Release|Win32" 
if ERRORLEVEL 1 goto :EOF


