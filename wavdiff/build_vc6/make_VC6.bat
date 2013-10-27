if exist "C:\Program Files\Microsoft Visual Studio\VC98\Bin\VCVARS32.BAT"  call "C:\Program Files\Microsoft Visual Studio\VC98\Bin\VCVARS32.BAT"  
MSDev.com WavDiff.dsw  /MAKE "WavDiff - Win32 Release" 
if ERRORLEVEL 1 goto :EOF

REM call "C:\Program Files\Microsoft Visual Studio\VC98\Bin\VCVARS32.BAT" 
REM start MSDev.exe WavDiff.dsw /MAKE ALL
