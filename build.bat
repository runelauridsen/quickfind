@echo off
if not exist build mkdir build
pushd build
cl ..\main.c /nologo /Fequickfind.dll /DNDEBUG /O2 /DQUICKFIND_BUILD_CLIENT /LD
cl ..\main.c /nologo /Fequickfind.exe /DNDEBUG /O2 /DQUICKFIND_BUILD_SERVER
popd
