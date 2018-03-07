@echo off

cl /Zi editor.cpp /link /out:editor.exe kernel32.lib user32.lib dwrite.lib d2d1.lib gdi32.lib

@echo on