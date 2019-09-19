set +x
export EPIC_BUILD_ROLE_NAME=robomerge-ts-service-testing

export DOCKER_REGISTRY_DOMAIN=hub.ol.epicgames.net
export DOCKER_REGISTRY_NAMESPACE=epicgames
export DOCKER_IMAGE_NAME=$DOCKER_REGISTRY_DOMAIN/$DOCKER_REGISTRY_NAMESPACE/$EPIC_BUILD_ROLE_NAME
export DOCKER_VERSION=latest

# Set P4PORT to IP of the Perforce server to bypass any DNS issues
export P4PORT=perforce:1666
export P4PASSWD=
export BOTS=test
export ROBO_EXTERNAL_URL=http://localhost:8080

set -x

docker pull $DOCKER_IMAGE_NAME:$DOCKER_VERSION

docker stop $EPIC_BUILD_ROLE_NAME > /dev/null
docker rm $EPIC_BUILD_ROLE_NAME  > /dev/null

docker run -d --name $EPIC_BUILD_ROLE_NAME \
    -e "P4PASSWD=$P4PASSWD" \
    -e "P4PORT=$P4PORT" \
    -e "BOTNAME=$BOTS" \
    -e "ROBO_EXTERNAL_URL=$ROBO_EXTERNAL_URL" \
    -e "ROBO_DEV_MODE=true" \
    -e "NODE_ENV=development" \
    -p 8080:8080 \
    -p 1666:1666 \
    $DOCKER_IMAGE_NAME:$DOCKER_VERSION \
    node dist/robo/watchdog.js 