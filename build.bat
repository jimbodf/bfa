@echo off

cl /O2 .\cli.c /I.\external /I%VULKAN_SDK%\Include .\external\freetype.lib %VULKAN_SDK%\Lib\vulkan-1.lib /Fe:.\bfa.exe
cl /O2 .\example.c /I.\external /I%VULKAN_SDK%\Include .\external\freetype.lib %VULKAN_SDK%\Lib\vulkan-1.lib /Fe:.\bfa_glyph.exe

@echo on

