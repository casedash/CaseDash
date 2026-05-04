@echo off
setlocal
for %%I in ("%~dp0.") do set "REPO_ROOT=%%~fI"

set "CASEDASH_LINK_MAPS=ON"
del /q "%REPO_ROOT%\build\CaseDash.map" "%REPO_ROOT%\build\CaseDashBenchmarks.map" >nul 2>nul
del /q "%REPO_ROOT%\build\CaseDash.exe" "%REPO_ROOT%\build\CaseDashBenchmarks.exe" >nul 2>nul
call "%REPO_ROOT%\build.cmd" %*
if errorlevel 1 exit /b %errorlevel%

python "%REPO_ROOT%\tools\analyze_link_map.py" "%REPO_ROOT%\build\CaseDash.map" --top 25 > "%REPO_ROOT%\build\CaseDash.map.summary.txt"
if errorlevel 1 exit /b %errorlevel%

python "%REPO_ROOT%\tools\analyze_link_map.py" "%REPO_ROOT%\build\CaseDashBenchmarks.map" --top 15 > "%REPO_ROOT%\build\CaseDashBenchmarks.map.summary.txt"
if errorlevel 1 exit /b %errorlevel%

echo Map summaries written:
echo   build\CaseDash.map.summary.txt
echo   build\CaseDashBenchmarks.map.summary.txt
exit /b %errorlevel%
