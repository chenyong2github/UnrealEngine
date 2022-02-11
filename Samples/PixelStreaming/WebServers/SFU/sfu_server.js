const config = require('./config');
const WebSocket = require('ws');
const mediasoup = require('mediasoup_prebuilt');
const mediasoupSdp = require('mediasoup-sdp-bridge');

let signalServer = null;
let mediasoupRouter;
let streamer = null;
let peers = new Map();

function connectSignalling(server) {
  console.log("Connecting to Signalling Server at %s", server);
  signalServer = new WebSocket(server);
  signalServer.addEventListener("open", _ => { console.log(`Connected to signalling server`); });
  signalServer.addEventListener("close", result => { console.log(`Disconnected from signalling server: ${result.code} ${result.reason}`); });
  signalServer.addEventListener("error", result => { console.log(`Error: ${result.message}`); });
  signalServer.addEventListener("message", result => onSignallingMessage(result.data));
}

async function onStreamerOffer(sdp) {
  console.log("Got offer from streamer");

  if (streamer != null) {
    signalServer.close(1013 /* Try again later */, 'Producer is already connected');
    return;
  }

  const transport = await createWebRtcTransport("Streamer");
  const sdpEndpoint = mediasoupSdp.createSdpEndpoint(transport, mediasoupRouter.rtpCapabilities);
  const producers = await sdpEndpoint.processOffer(sdp);
  const sdpAnswer = sdpEndpoint.createAnswer();
  const answer = { type: "answer", sdp: sdpAnswer };

  console.log("Sending answer to streamer.");
  signalServer.send(JSON.stringify(answer));
  streamer = { transport: transport, producers: producers, nextDataStreamId: 0 };
}

function getNextStreamerDataProducerId() {
  if (!streamer.transport.sctpParameters || typeof streamer.transport.sctpParameters.MIS !== 'number') {
      throw new TypeError('missing streamer.transport.sctpParameters.MIS');
  }
  const numStreams = streamer.transport.sctpParameters.MIS;
  if (!streamer.dataStreamIds)
      streamer.dataStreamIds = Buffer.alloc(numStreams, 0);
  let sctpStreamId;
  for (let idx = 0; idx < streamer.dataStreamIds.length; ++idx) {
      sctpStreamId = (streamer.nextDataStreamId + idx) % streamer.dataStreamIds.length;
      if (!streamer.dataStreamIds[sctpStreamId]) {
          streamer.nextDataStreamId = sctpStreamId + 1;
          return sctpStreamId;
      }
  }
  console.log("no available data streams on streamer");
  return -1;
}

function onStreamerDisconnected() {
  console.log("Streamer disconnected");
  disconnectAllPeers();

  if (streamer != null) {
    for (const mediaProducer of streamer.producers) {
      mediaProducer.close();
    }
    streamer.transport.close();
    streamer = null;
  }
}

async function onPeerConnected(peerId) {
  console.log("Player %s joined", peerId);

  if (streamer == null) {
    console.log("No streamer connected, ignoring player.");
    return;
  }

  const transport = await createWebRtcTransport("Peer " + peerId);
  const sdpEndpoint = mediasoupSdp.createSdpEndpoint( transport, mediasoupRouter.rtpCapabilities );
  sdpEndpoint.addConsumeData(); // adds the sctp 'application' section to the offer

  // media consumers
  let consumers = [];
  try {
    for (const mediaProducer of streamer.producers) {
      const consumer = await transport.consume({ producerId: mediaProducer.id, rtpCapabilities: mediasoupRouter.rtpCapabilities });
      consumer.observer.on("layerschange", function() { console.log("layer changed!", consumer.currentLayers); });
      sdpEndpoint.addConsumer(consumer);
      consumers.push(consumer);
    }
  } catch(err) {
    console.error("transport.consume() failed:", err);
    return;
  }

  const offerSignal = {
    type: "offer",
    playerId: peerId,
    sdp: sdpEndpoint.createOffer(),
    sfu: true // indicate we're offering from sfu
  };

  // send offer to peer
  signalServer.send(JSON.stringify(offerSignal));

  const newPeer = {
    id: peerId,
    transport: transport,
    sdpEndpoint: sdpEndpoint,
    consumers: consumers
  };

  // add the new peer
  peers.set(peerId, newPeer);
}

async function setupPeerDataChannels(peerId) {
  const peer = peers.get(peerId);
  if (peer) {
    const streamerDataProducerId = getNextStreamerDataProducerId();
    if (streamerDataProducerId != -1) {
      // streamer data producer
      peer.streamerDataProducer = await streamer.transport.produceData({label: 'datachannel', sctpStreamParameters: {streamId: streamerDataProducerId, ordered: true}});

      // peer data consumer
      peer.peerDataConsumer = await peer.transport.consumeData({dataProducerId: peer.streamerDataProducer.id});

      // peer data producer
      peer.peerDataProducer = await peer.transport.produceData({label: 'datachannel', sctpStreamParameters: {streamId: 1, ordered: true}});

      // streamer data consumer
      peer.streamerDataConsumer = await streamer.transport.consumeData({dataProducerId: peer.peerDataProducer.id});

      const peerSignal = {
        type: 'peerDataChannels',
        playerId: peerId,
        sendStreamId: 1,
        recvStreamId: peer.peerDataConsumer.sctpStreamParameters.streamId
      };

      const streamerSignal = {
        type: "streamerDataChannels",
        playerId: peerId,
        sendStreamId: streamerDataProducerId,
        recvStreamId: peer.streamerDataConsumer.sctpStreamParameters.streamId
      };

      // peer data connection
      signalServer.send(JSON.stringify(peerSignal));

      // streamers data connection
      signalServer.send(JSON.stringify(streamerSignal));
    }
  }
}

function onPeerAnswer(peerId, sdp) {
  console.log("Got answer from player %s", peerId);

  const consumer = peers.get(peerId);
  if (!consumer)
    console.error(`Unable to find player ${peerId}`);
  else
    consumer.sdpEndpoint.processAnswer(sdp);
}

function onPeerDisconnected(peerId) {
  console.log("Player %s disconnected", peerId);
  const peer = peers.get(peerId);
  if (peer != null) {
    for (consumer of peer.consumers) {
      consumer.close();
    }
    if (peer.peerDataConsumer) {
      peer.peerDataConsumer.close();
      peer.peerDataProducer.close();
      peer.streamerDataConsumer.close();
      peer.streamerDataProducer.close();
    }
    peer.transport.close();
  }
  peers.delete(peerId);
}

function disconnectAllPeers() {
  console.log("Disconnected all players");
  for (const [peerId, peer] of peers) {
    onPeerDisconnected(peerId);
  }
}

async function onSignallingMessage(message) {
	//console.log(`Got MSG: ${message}`);
  const msg = JSON.parse(message);

  if (msg.type == 'offer') {
    onStreamerOffer(msg.sdp);
  }
  else if (msg.type == 'answer') {
    onPeerAnswer(msg.playerId, msg.sdp);
  }
  else if (msg.type == 'playerConnected') {
    onPeerConnected(msg.playerId);
  }
  else if (msg.type == 'playerDisconnected') {
    onPeerDisconnected(msg.playerId);
  }
  else if (msg.type == 'streamerDisconnected') {
    onStreamerDisconnected();
  }
  else if (msg.type == 'dataChannelRequest') {
    setupPeerDataChannels(msg.playerId);
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
  const mediasoupRouter = await worker.createRouter({ mediaCodecs });

  return mediasoupRouter;
}

async function createWebRtcTransport(identifier) {
  const {
    listenIps,
    initialAvailableOutgoingBitrate
  } = config.mediasoup.webRtcTransport;

  const transport = await mediasoupRouter.createWebRtcTransport({
    listenIps: listenIps,
    enableUdp: true,
    enableTcp: false,
    preferUdp: true,
    enableSctp: true, // datachannels
    initialAvailableOutgoingBitrate: initialAvailableOutgoingBitrate
  });

  transport.on("icestatechange", (iceState) => { console.log("%s ICE state changed to %s", identifier, iceState); });
  transport.on("iceselectedtuplechange", (iceTuple) => { console.log("%s ICE selected tuple %s", identifier, JSON.stringify(iceTuple)); });
  transport.on("sctpstatechange", (sctpState) => { console.log("%s SCTP state changed to %s", identifier, sctpState); });

  return transport;
}

async function main() {
  console.log('Starting Mediasoup...');
  console.log("Config = ");
  console.log(config);

  mediasoupRouter = await startMediasoup();

  connectSignalling(config.signallingURL);
}

main();
