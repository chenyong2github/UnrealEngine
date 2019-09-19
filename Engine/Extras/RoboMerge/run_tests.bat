@ECHO OFF

CALL :CLEANUP

REM Create robomerge functional testing network
docker network create --driver bridge robomerge_functtest_network

REM Rebuild container
docker build -t p4docker -f Dockerfile.p4docker .
docker build -t robomerge -f Dockerfile .
docker build -t robomerge_functionaltests -f Dockerfile.functionaltests .

echo
echo ------------------
echo Running unit tests
echo ------------------

docker run -a stderr -h robomerge_unittests --name robomerge_unittests robomerge npm test

IF "%ERRORLEVEL%" == "0" ( 
    echo No errors encountered during unit tests
) ELSE (
    echo Errors encountered during unit tests, check docker logs.
    echo Logs from local Robomerge instance: 
    docker logs robomerge_unittests

    EXIT /B 0
)

REM Run functional tests
REM --------------------

echo
echo ----------------------
echo Running function tests
echo ----------------------

REM Run container
docker run -d -p 1666:1666 -h p4docker --name p4docker --network robomerge_functtest_network p4docker
timeout /t 5
docker run -d -e P4PORT=p4docker:1666 -h robomerge_functtest --name robomerge_functtest --network robomerge_functtest_network robomerge node dist/robo/robo.js -bs_root=//RoboMergeData/Main -noTLS -noIPC
docker run -h robomerge_functionaltests --name robomerge_functionaltests --network robomerge_functtest_network robomerge_functionaltests

IF "%ERRORLEVEL%" == "0" ( 
    echo No errors encountered during functional tests, cleanup up containers.
) ELSE (
    echo Errors encountered during functional tests, check docker logs.
    echo Logs from local Robomerge instance: 
    docker logs robomerge_functtest

    EXIT /B 0
)

:CLEANUP
echo Stopping and removing any old containers...
docker stop p4docker
docker rm p4docker

docker stop robomerge_functtest
docker rm robomerge_functtest

docker stop robomerge_functionaltests
docker rm robomerge_functionaltests

docker stop robomerge_unittests
docker rm robomerge_unittests

echo Removing any old networking...
docker network rm robomerge_functtest_network
EXIT /B 0