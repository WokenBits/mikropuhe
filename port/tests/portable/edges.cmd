@echo off
rem The sound-file cases of tests/portable/edges.sh, in cmd. This file is in code page 850, as cmd reads it here.
cd /d %USERPROFILE%\mikropuhe-test
set P=%USERPROFILE%\mikropuhe-windows\puhu.exe
chcp
"%P%" --version
"%P%" -v sophia -o a.wav "Hei Michael, t„m„ on KITT"
echo to-file exit %errorlevel%
"%P%" -v sophia "Hei Michael, t„m„ on KITT" > b.wav
echo redirected exit %errorlevel%
echo Hei Michael, t„m„ on KITT| "%P%" -v sophia -o c.wav
echo piped-in exit %errorlevel%
"%P%" -o "„„kk”set.wav" "hyv„„ p„iv„„"
echo file-name exit %errorlevel%
"%P%" -d pcspeaker -o d.wav "hyv„„ p„iv„„"
"%P%" -d pcspeaker --machine xt --speaker none -o e.wav "hyv„„ p„iv„„"
for %%f in (a b c d e) do certutil -hashfile %%f.wav SHA256 | findstr /v ":"
certutil -hashfile "„„kk”set.wav" SHA256 | findstr /v ":"
