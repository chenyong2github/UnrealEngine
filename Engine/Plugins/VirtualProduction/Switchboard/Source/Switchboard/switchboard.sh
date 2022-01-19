#!/bin/sh

_switchboardDir=$(dirname "$0")
_engineDir=$(cd "$_switchboardDir/../../../../.."; pwd)
_enginePythonDir="$_engineDir/Binaries/ThirdParty/Python3/Linux"
_venvDir="$_engineDir/Extras/ThirdPartyNotUE/SwitchboardThirdParty/Python"

if [ ! -x "$_venvDir/bin/python3" ]; then
    "$_enginePythonDir/bin/python3" "$_switchboardDir/sb_setup.py" install --venv-dir="$_venvDir"
    _installResult=$?
    if [ $_installResult -ne 0 ]; then
        echo "Installation failed with non-zero exit code!"
        exit $_installResult
    fi
fi

PYTHONPATH="$_switchboardDir:$PYTHONPATH" "$_venvDir/bin/python3" -m switchboard
