const config = require('./config');
const WebSocket = require('ws');
const mediasoup = require('mediasoup_prebuilt');
const mediasoupSdp = require('mediasoup-sdp-bridge');

var connected = true;

let signalServer = null;
let router;
let endpointRtpCapabilities;
let producers;
let consumers = new Map();

function connect(server) {
  console.log("Connecting to Signalling Server at %s", server);
  signalServer = new WebSocket(server);
  signalServer.addEventListener("open", _ => { console.log(`Connected`); });
  signalServer.addEventListener("close", result => { console.log(`Closed: ${result.code} ${result.reason}`); });
  signalServer.addEventListener("error", result => { console.log(`Error: ${result.message}`); });
  signalServer.addEventListener("message", result => onMessage(JSON.parse(result.data)));
}

async function onMessage(msg) {
	//console.log(`Got MSG: ${JSON.stringify(msg)}`);
  if (msg.type == 'offer') {
    console.log("Got offer from producer");
    // offer msg from producer
    if (producers != null) {
      signalServer.close(1013 /* Try again later */, 'Producer is already connected');
      return;
    }
    const transport = await createWebRtcTransport("Streamer");
    const sdpEndpoint = await mediasoupSdp.createSdpEndpoint( transport, router.rtpCapabilities );
    producers = await sdpEndpoint.processOffer(msg.sdp);
    const sdpAnswer = sdpEndpoint.createAnswer();
    const answer = { type:"answer", sdp:sdpAnswer };
    signalServer.send(JSON.stringify(answer));
    console.log("Sending answer to producer.");
  }
  else if (msg.type == 'answer') {
    console.log("Got answer from player %s", msg.playerId);
    let consumer = consumers.get(msg.playerId);
    if (!consumer)
      console.error(`Unable to find player ${msg.playerId}`);
    else
      await consumer.endpoint.processAnswer(msg.sdp);
  }
  else if (msg.type == 'playerConnected') {
    console.log("Player %s joined", msg.playerId);
    const transport = await createWebRtcTransport("Player", msg.playerId);
    const endpoint = await mediasoupSdp.createSdpEndpoint( transport, router.rtpCapabilities );
    const newPlayer = { id: msg.playerId, transport: transport, endpoint: endpoint };
    consumers.set(msg.playerId, newPlayer);

    try {
      for (const producer of producers) {
        const consumer = await transport.consume({ producerId: producer.id, rtpCapabilities: router.rtpCapabilities });
        consumer.observer.on("layerschange", function() { console.log("layer changed!", consumer.currentLayers); });
        endpoint.addConsumer(consumer);
      }
    } catch(err) {
      console.error("transport.consume() failed:", err);
    }

    const sdpOffer = endpoint.createOffer();
    const offer = { type: "offer", sdp: sdpOffer, playerId: msg.playerId };
    signalServer.send(JSON.stringify(offer));
    console.log("Sending offer to player %s", msg.playerId);
  }
  else if (msg.type == 'playerDisconnected') {
    console.log("Player %s left", msg.playerId);
    consumers.delete(msg.playerId);
  }
  else if (msg.type == 'streamerDisconnected') {
    console.log("Stream ended");
    consumers = new Map();
    producers = null;
  }
  // todo a new message type for force layer switch (for debugging)
  // see: https://mediasoup.org/documentation/v3/mediasoup/api/#consumer-setPreferredLayers
  // preferredLayers for debugging to select a particular simulcast layer, looks like { spatialLayer: 2, temporalLayer: 0 }
}

async function startMediasoup() {
  let worker = await mediasoup.createWorker({
    logLevel: config.mediasoup.worker.logLevel,
    logTags: config.mediasoup.worker.logTags,
    rtcMinPort: config.mediasoup.worker.rtcMinPort,
    rtcMaxPort: config.mediasoup.worker.rtcMaxPort,
  });

  worker.on('died', () => {
    console.error('mediasoup worker died (this should never happen)');
    process.exit(1);
  });

  const mediaCodecs = config.mediasoup.router.mediaCodecs;
  const router = await worker.createRouter({ mediaCodecs });

  return router;
}

async function createWebRtcTransport(name, playerId) {
  const {
    listenIps,
    initialAvailableOutgoingBitrate
  } = config.mediasoup.webRtcTransport;

  const transport = await router.createWebRtcTransport({
    listenIps: listenIps,
    enableUdp: true,
    enableTcp: true,
    preferUdp: true,
    initialAvailableOutgoingBitrate: initialAvailableOutgoingBitrate,
    appData: { name: name, id: playerId }
  });

  transport.on("icestatechange", (iceState) => { console.log("%s %s ICE state changed to %s", name, playerId, iceState); });
  transport.on("iceselectedtuplechange", (iceTuple) => { console.log("%s %s ICE selected tuple %s", name, playerId, JSON.stringify(iceTuple)); });

  return transport;
}

async function main() {
  console.log('Starting Mediasoup...');
  console.log("Config = ");
  console.log(config);
  router = await startMediasoup();

  // this might need work. returns hard coded caps
  endpointRtpCapabilities = mediasoupSdp.generateRtpCapabilities0();

  connect(config.signallingURL);
}

main();
