@echo off

set PWD=%cd%

pushd ..\build

rem C4201: nonstandard extension used: nameless struct/union
rem C4204: nonstandard extension used: non-constant aggregate initializer
rem C4710: function not inlined
rem C4711: function selected for automatic inline expansion

del code_*.pdb >nul 2>nul
echo compiling > build.lock
cl /FC /GS- /kernel /LD /O2 /Oi /std:c++17 /utf-8 /wd4201 /wd4204 /wd4710 /wd4711 /Wall /WX /Z7 /nologo %PWD%\code.cpp /link /DEBUG /DLL /NOENTRY /NODEFAULTLIB /OPT:ICF /OPT:REF /PDB:code_%RANDOM%.pdb /SUBSYSTEM:WINDOWS
del build.lock

cl /FC /GS- /kernel /O2 /Oi /std:c++17 /utf-8 /wd4201 /wd4204 /wd4710 /wd4711 /Wall /WX /Z7 /nologo %PWD%\main.cpp /link /DEBUG /ENTRY:WinEntry /NODEFAULTLIB /OPT:ICF /OPT:REF /SUBSYSTEM:WINDOWS kernel32.lib user32.lib gdi32.lib opengl32.lib

popd
