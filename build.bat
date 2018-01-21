@echo off

cl /Zi toy.cpp /link /out:kal.exe kernel32.lib user32.lib dwrite.lib d2d1.lib gdi32.lib

@echo on