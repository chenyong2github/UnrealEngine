set CurrentFolder=%~dp0
echo %CurrentFolder%

REM delete OutputEnvVars from the old location
del "%CurrentFolder%OutputEnvVars.txt"

set OutputEnvVarsFolder=%~dp0..\
del "%OutputEnvVarsFolder%OutputEnvVars.txt"



REM set android sdk environment variables
echo MLSDK=%CurrentFolder%>>"%OutputEnvVarsFolder%OutputEnvVars.txt"
