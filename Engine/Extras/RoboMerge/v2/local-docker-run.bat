@ECHO OFF
set EPIC_BUILD_ROLE_NAME=robomerge-ts-service-testing

set DOCKER_REGISTRY_DOMAIN=hub.ol.epicgames.net
set DOCKER_REGISTRY_NAMESPACE=epicgames
set DOCKER_IMAGE_NAME=%DOCKER_REGISTRY_DOMAIN%/%DOCKER_REGISTRY_NAMESPACE%/%EPIC_BUILD_ROLE_NAME%
set DOCKER_VERSION=latest

REM Set P4PORT to IP of the Perforce server to bypass any DNS issues
set P4PORT=perforce:1666
set P4PASSWD=
set BOTS=test
set ROBO_EXTERNAL_URL=http://localhost:8080
set PORTS_ARGS=-p 8080:8080

@ECHO ON

docker pull --platform linux %DOCKER_IMAGE_NAME%:%DOCKER_VERSION%

docker stop %EPIC_BUILD_ROLE_NAME%>NUL
docker rm %EPIC_BUILD_ROLE_NAME%>NUL

@REM -v /home/admin/robo-vault:/vault:ro
@REM -v robosettings:/root/.robomerge \
docker run --platform linux -d --name %EPIC_BUILD_ROLE_NAME% ^
    -e "P4PASSWD=%P4PASSWD%" ^
    -e "P4PORT=%P4PORT%" ^
    -e "BOTNAME=%BOTS%" ^
    -e "ROBO_EXTERNAL_URL=%ROBO_EXTERNAL_URL%" ^
    -e "ROBO_DEV_MODE=true" ^
    -e "NODE_ENV=development" ^
    %PORTS_ARGS% ^
    %DOCKER_IMAGE_NAME%:%DOCKER_VERSION% ^
    node dist/robo/robo.js -noIPC -noTLS