@echo off

@rem Enable vendor-specific graphics APIs if the container is running with GPU acceleration
powershell -ExecutionPolicy Bypass -File "%~dp0.\enable-graphics-apis.ps1"

@rem Run the entrypoint command specified via our command-line parameters
%*
