@echo off
rem Monster Reise App - Daten aus eigenen Spieldateien erzeugen.
rem Spieldateien (entschluesselte CIA/.3ds) oder Ordner auf diese Datei ziehen.
where node >nul 2>nul || (echo Node.js fehlt: https://nodejs.org/ & pause & exit /b 1)
node "%~dp0daten.mjs" %* --ziel "%~dp0SD"
pause
