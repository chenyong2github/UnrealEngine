ARG UNREAL_ENGINE_RELEASE
FROM ghcr.io/epicgames/unreal-engine:dev-${UNREAL_ENGINE_RELEASE} as source
FROM node:10

# Copy the signalling server source code from the Unreal Engine development image
COPY --from=source /home/ue4/UnrealEngine/Samples/PixelStreaming/WebServers/SignallingWebServer /opt/SignallingWebServer

# Install the dependencies for the signalling server
WORKDIR /opt/SignallingWebServer
RUN npm install .

# Expose TCP port 80 for player WebSocket connections and web server HTTP access
EXPOSE 80

# Expose TCP port 8888 for streamer WebSocket connections
EXPOSE 8888

# Set the signalling server as the container's entrypoint
ENTRYPOINT ["/usr/local/bin/node", "/opt/SignallingWebServer/cirrus.js"]
