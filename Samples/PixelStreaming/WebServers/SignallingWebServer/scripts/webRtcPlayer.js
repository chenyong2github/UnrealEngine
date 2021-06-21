// Copyright Epic Games, Inc. All Rights Reserved.
// universal module definition - read https://www.davidbcalhoun.com/2014/what-is-amd-commonjs-and-umd/

(function (root, factory) {
    if (typeof define === 'function' && define.amd) {
        // AMD. Register as an anonymous module.
        define(["./adapter"], factory);
    } else if (typeof exports === 'object') {
        // Node. Does not work with strict CommonJS, but
        // only CommonJS-like environments that support module.exports,
        // like Node.
        module.exports = factory(require("./adapter"));
    } else {
        // Browser globals (root is window)
        root.webRtcPlayer = factory(root.adapter);
    }
}(this, function (adapter) {

    function webRtcPlayer(parOptions) {
    	parOptions = parOptions || {};
    	
        var self = this;

        //**********************
        //Config setup
        //**********************
		this.cfg = parOptions.peerConnectionOptions || {};
		this.cfg.sdpSemantics = 'unified-plan';
        // this.cfg.rtcAudioJitterBufferMaxPackets = 10;
        // this.cfg.rtcAudioJitterBufferFastAccelerate = true;
        // this.cfg.rtcAudioJitterBufferMinDelayMs = 0;


		//If this is true in Chrome 89+ SDP is sent that is incompatible with UE WebRTC and breaks.
        //this.cfg.offerExtmapAllowMixed = false;
        this.pcClient = null;
        this.dcClient = null;
        this.tnClient = null;

        this.sdpConstraints = {
          offerToReceiveAudio: 1, //Note: if you don't need audio you can get improved latency by turning this off.
          offerToReceiveVideo: 1
        };

        // See https://www.w3.org/TR/webrtc/#dom-rtcdatachannelinit for values
        this.dataChannelOptions = {ordered: true};

        // ToDo: get this useMic from url string like ?useMic or similar
        //this.useMic = false;

        //**********************
        //Variables
        //**********************
        this.latencyTestTimings = 
        {
            TestStartTimeMs: null,
            ReceiptTimeMs: null,
            PreCaptureTimeMs: null,
            PostCaptureTimeMs: null,
            PreEncodeTimeMs: null,
            PostEncodeTimeMs: null,
            FrameDisplayTimeMs: null,
            Reset: function()
            {
                this.TestStartTimeMs = null;
                this.ReceiptTimeMs = null;
                this.PreCaptureTimeMs = null;
                this.PostCaptureTimeMs = null;
                this.PreEncodeTimeMs = null;
                this.PostEncodeTimeMs = null;
                this.FrameDisplayTimeMs = null;
            },
            HasAllTimings: function()
            {
                return this.TestStartTimeMs && 
                this.ReceiptTimeMs && 
                this.PreCaptureTimeMs && 
                this.PostCaptureTimeMs && 
                this.PreEncodeTimeMs && 
                this.PostEncodeTimeMs && 
                this.FrameDisplayTimeMs;
            },
            SetUETimings: function(UETimings)
            {
                this.ReceiptTimeMs = UETimings.ReceiptTimeMs;
                this.PreCaptureTimeMs = UETimings.PreCaptureTimeMs;
                this.PostCaptureTimeMs = UETimings.PostCaptureTimeMs;
                this.PreEncodeTimeMs = UETimings.PreEncodeTimeMs;
                this.PostEncodeTimeMs = UETimings.PostEncodeTimeMs;
                if(this.HasAllTimings())
                {
                    this.OnAllLatencyTimingsReady(this);
                }
            },
            SetFrameDisplayTime: function(TimeMs)
            {
                this.FrameDisplayTimeMs = TimeMs;
                if(this.HasAllTimings())
                {
                    this.OnAllLatencyTimingsReady(this);
                }
            },
            OnAllLatencyTimingsReady: function(Timings){}
        }

        //**********************
        //Functions
        //**********************

        //Create Video element and expose that as a parameter
        createWebRtcVideo = function() {
            var video = document.createElement('video');

            video.id = "streamingVideo";
            video.playsInline = true;
			
            video.addEventListener('loadedmetadata', function(e){
                if(self.onVideoInitialised){
                    self.onVideoInitialised();
                }
            }, true);
			
			// Check if request video frame callback is supported
			if ('requestVideoFrameCallback' in HTMLVideoElement.prototype) {
				// The API is supported! 
				
				const onVideoFrameReady = (now, metadata) => {
					
					if(metadata.receiveTime && metadata.expectedDisplayTime)
					{
						const receiveToCompositeMs = metadata.presentationTime - metadata.receiveTime;
						self.aggregatedStats.receiveToCompositeMs = receiveToCompositeMs;
					}
					
				  
					// Re-register the callback to be notified about the next frame.
					video.requestVideoFrameCallback(onVideoFrameReady);
				};
				
				// Initially register the callback to be notified about the first frame.
				video.requestVideoFrameCallback(onVideoFrameReady);
			}
			
            return video;
        }

        this.video = createWebRtcVideo();

        onsignalingstatechange = function(state) {
            console.info('signaling state change:', state)
        };

        oniceconnectionstatechange = function(state) {
            console.info('ice connection state change:', state)
        };

        onicegatheringstatechange = function(state) {
            console.info('ice gathering state change:', state)
        };

        handleOnTrack = function(e) {
            console.log('handleOnTrack', e.streams);
			
			if (e.track)
			{
				console.log('Got track - ' + e.track.kind + ' id=' + e.track.id + ' readyState=' + e.track.readyState); 
			}
			
			if(e.track.kind == "audio")
			{
				return;
			}
			
            if (self.video.srcObject !== e.streams[0]) {
                self.video.srcObject = e.streams[0];
				console.log('Set video stream from ontrack');
				
            }
			
        };

        setupDataChannel = function(pc, label, options) {
            try {
                let datachannel = pc.createDataChannel(label, options);
                console.log(`Created datachannel (${label})`)

                // Inform browser we would like binary data as an ArrayBuffer (FF chooses Blob by default!)
                datachannel.binaryType = "arraybuffer";
                
                datachannel.onopen = function (e) {
                  console.log(`data channel (${label}) connect`)
                  if(self.onDataChannelConnected){
                    self.onDataChannelConnected();
                  }
                }

                datachannel.onclose = function (e) {
                  console.log(`data channel (${label}) closed`)
                }

                datachannel.onmessage = function (e) {
                  console.log(`Got message (${label})`, e.data)
                  if (self.onDataChannelMessage)
                    self.onDataChannelMessage(e.data);
                }

                return datachannel;
            } catch (e) { 
                console.warn('No data channel', e);
                return null;
            }
        }

        onicecandidate = function (e) {
			console.log('ICE candidate', e)
			if (e.candidate && e.candidate.candidate) {
                self.onWebRtcCandidate(e.candidate);
            }
        };

        handleCreateOffer = function (pc) {
            pc.createOffer(self.sdpConstraints).then(function (offer) {
            	pc.setLocalDescription(offer);
            	if (self.onWebRtcOffer) {
            		// (andriy): increase start bitrate from 300 kbps to 20 mbps and max bitrate from 2.5 mbps to 100 mbps
                    // (100 mbps means we don't restrict encoder at all)
                    // after we `setLocalDescription` because other browsers are not c happy to see google-specific config
            		offer.sdp = offer.sdp.replace(/(a=fmtp:\d+ .*level-asymmetry-allowed=.*)\r\n/gm, "$1;x-google-start-bitrate=10000;x-google-max-bitrate=100000\r\n");
                    //offer.sdp = offer.sdp.replace("http://www.webrtc.org/experiments/rtp-hdrext/playout-delay", "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay 0 0\r\na=name:playout-delay");
            		self.onWebRtcOffer(offer);
                }
            },
            function () { console.warn("Couldn't create offer") });
        }
        
        setupPeerConnection = function (pc) {
        	if (pc.SetBitrate)
        		console.log("Hurray! there's RTCPeerConnection.SetBitrate function");

            //Setup peerConnection events
            pc.onsignalingstatechange = onsignalingstatechange;
            pc.oniceconnectionstatechange = oniceconnectionstatechange;
            pc.onicegatheringstatechange = onicegatheringstatechange;

            pc.ontrack = handleOnTrack;
            pc.onicecandidate = onicecandidate;
        };

        generateAggregatedStatsFunction = function(){
            if(!self.aggregatedStats)
                self.aggregatedStats = {};

            return function(stats){
                //console.log('Printing Stats');

                let newStat = {};
                console.log('----------------------------- Stats start -----------------------------');
                stats.forEach(stat => {
//                    console.log(JSON.stringify(stat, undefined, 4));
                    if (stat.type == 'inbound-rtp' 
                        && !stat.isRemote 
                        && (stat.mediaType == 'video' || stat.id.toLowerCase().includes('video'))) {

                        newStat.timestamp = stat.timestamp;
                        newStat.bytesReceived = stat.bytesReceived;
                        newStat.framesDecoded = stat.framesDecoded;
                        newStat.packetsLost = stat.packetsLost;
                        newStat.bytesReceivedStart = self.aggregatedStats && self.aggregatedStats.bytesReceivedStart ? self.aggregatedStats.bytesReceivedStart : stat.bytesReceived;
                        newStat.framesDecodedStart = self.aggregatedStats && self.aggregatedStats.framesDecodedStart ? self.aggregatedStats.framesDecodedStart : stat.framesDecoded;
                        newStat.timestampStart = self.aggregatedStats && self.aggregatedStats.timestampStart ? self.aggregatedStats.timestampStart : stat.timestamp;

                        if(self.aggregatedStats && self.aggregatedStats.timestamp){
                            if(self.aggregatedStats.bytesReceived){
                                // bitrate = bits received since last time / number of ms since last time
                                //This is automatically in kbits (where k=1000) since time is in ms and stat we want is in seconds (so a '* 1000' then a '/ 1000' would negate each other)
                                newStat.bitrate = 8 * (newStat.bytesReceived - self.aggregatedStats.bytesReceived) / (newStat.timestamp - self.aggregatedStats.timestamp);
                                newStat.bitrate = Math.floor(newStat.bitrate);
                                newStat.lowBitrate = self.aggregatedStats.lowBitrate && self.aggregatedStats.lowBitrate < newStat.bitrate ? self.aggregatedStats.lowBitrate : newStat.bitrate
                                newStat.highBitrate = self.aggregatedStats.highBitrate && self.aggregatedStats.highBitrate > newStat.bitrate ? self.aggregatedStats.highBitrate : newStat.bitrate
                            }

                            if(self.aggregatedStats.bytesReceivedStart){
                                newStat.avgBitrate = 8 * (newStat.bytesReceived - self.aggregatedStats.bytesReceivedStart) / (newStat.timestamp - self.aggregatedStats.timestampStart);
                                newStat.avgBitrate = Math.floor(newStat.avgBitrate);
                            }

                            if(self.aggregatedStats.framesDecoded){
                                // framerate = frames decoded since last time / number of seconds since last time
                                newStat.framerate = (newStat.framesDecoded - self.aggregatedStats.framesDecoded) / ((newStat.timestamp - self.aggregatedStats.timestamp) / 1000);
                                newStat.framerate = Math.floor(newStat.framerate);
                                newStat.lowFramerate = self.aggregatedStats.lowFramerate && self.aggregatedStats.lowFramerate < newStat.framerate ? self.aggregatedStats.lowFramerate : newStat.framerate
                                newStat.highFramerate = self.aggregatedStats.highFramerate && self.aggregatedStats.highFramerate > newStat.framerate ? self.aggregatedStats.highFramerate : newStat.framerate
                            }

                            if(self.aggregatedStats.framesDecodedStart){
                                newStat.avgframerate = (newStat.framesDecoded - self.aggregatedStats.framesDecodedStart) / ((newStat.timestamp - self.aggregatedStats.timestampStart) / 1000);
                                newStat.avgframerate = Math.floor(newStat.avgframerate);
                            }
                        }
                    }

                    //Read video track stats
                    if(stat.type == 'track' && (stat.trackIdentifier == 'video_label' || stat.kind == 'video')) {
                        newStat.framesDropped = stat.framesDropped;
                        newStat.framesReceived = stat.framesReceived;
                        newStat.framesDroppedPercentage = stat.framesDropped / stat.framesReceived * 100;
                        newStat.frameHeight = stat.frameHeight;
                        newStat.frameWidth = stat.frameWidth;
                        newStat.frameHeightStart = self.aggregatedStats && self.aggregatedStats.frameHeightStart ? self.aggregatedStats.frameHeightStart : stat.frameHeight;
                        newStat.frameWidthStart = self.aggregatedStats && self.aggregatedStats.frameWidthStart ? self.aggregatedStats.frameWidthStart : stat.frameWidth;
                    }

                    if(stat.type =='candidate-pair' && stat.hasOwnProperty('currentRoundTripTime') && stat.currentRoundTripTime != 0){
                        newStat.currentRoundTripTime = stat.currentRoundTripTime;
                    }
                });

				
				if(self.aggregatedStats.receiveToCompositeMs)
				{
					newStat.receiveToCompositeMs = self.aggregatedStats.receiveToCompositeMs;
				}
				
                self.aggregatedStats = newStat;

                if(self.onAggregatedStats)
                    self.onAggregatedStats(newStat)
            }
        };

        setupTracksToSendAsync = async function(pc){
            const stream = await navigator.mediaDevices.getUserMedia({video: false, audio: false});
            if(stream)
            {
                for (const track of stream.getTracks()) {
                    if(track.kind && track.kind == "audio")
                    {
                        pc.addTrack(track);
                    }
                }
            }
        };


        //**********************
        //Public functions
        //**********************

        this.startLatencyTest = function(onTestStarted) {
            // Can't start latency test without a video element
            if(!self.video)
            {
                return;
            }
            let videoCanvas = document.createElement("canvas");
            videoCanvas.style.display = 'none';
            var ctx = videoCanvas.getContext('2d');
            videoCanvas.width = self.video.videoWidth;
            videoCanvas.height = self.video.videoHeight;

            self.latencyTestTimings.Reset();
            self.video.focus();

            let checkCanvasSpecialLatencyPixels = function()
            {
                // Once we have our canvas pixel checker running we consider the test started properly
                if(self.latencyTestTimings.TestStartTimeMs == null)
                {
                    self.latencyTestTimings.TestStartTimeMs = Date.now();
                    onTestStarted(self.latencyTestTimings.TestStartTimeMs);
                }

                // If we have not found the special latency frame in 1 seconds we abort the test as this test is expensive.
                let testDeltaMs = Date.now() - self.latencyTestTimings.TestStartTimeMs;
                if(testDeltaMs > 1000)
                {
                    return;
                }

                // draw the video to the canvas so we can analyse pixels in the canvas
                ctx.drawImage(self.video, 0,0);
                let middlePixelW = self.video.videoWidth * 0.5
                let middlePixelH = self.video.videoHeight * 0.5
                let data = ctx.getImageData(middlePixelW, middlePixelH, 1, 1).data;
                let rgb = [ data[0], data[1], data[2] ];
                let redValue = rgb[0];
                if(redValue == 255)
                {
                    delete videoCanvas;
                    console.log("Got special latency frame!");
                    self.latencyTestTimings.SetFrameDisplayTime(Date.now());
                }
                else
                {
                    window.requestAnimationFrame(checkCanvasSpecialLatencyPixels);
                }
            }

            // start checking for special latency pixels
            window.requestAnimationFrame(checkCanvasSpecialLatencyPixels);
            
        }

        //This is called when revceiving new ice candidates individually instead of part of the offer
        //This is currently not used but would be called externally from this class
        this.handleCandidateFromServer = function(iceCandidate) {
            console.log("ICE candidate: ", iceCandidate);
            let candidate = new RTCIceCandidate(iceCandidate);
            self.pcClient.addIceCandidate(candidate).then(_=>{
                console.log('ICE candidate successfully added');
            });
        };

        //Called externaly to create an offer for the server
        this.createOffer = function() {
            if(self.pcClient){
                console.log("Closing existing PeerConnection")
                self.pcClient.close();
                self.pcClient = null;
            }
            self.pcClient = new RTCPeerConnection(self.cfg);

            setupPeerConnection(self.pcClient);
            
            // At this stage Pixel Streaming does not support transmission of any audio/video from browser, so indicate recvonly
            self.videoTransceiver = self.pcClient.addTransceiver("audio", { direction: "recvonly" });
            self.audioTransceiver = self.pcClient.addTransceiver("video", { direction: "recvonly" });

            
            self.dcClient = setupDataChannel(self.pcClient, 'cirrus', self.dataChannelOptions);
            handleCreateOffer(self.pcClient);

            //if we were going to support browser sending mic/video into UE
            // setupTracksToSendAsync(self.pcClient).finally(function()
            // {
            //     setupPeerConnection(self.pcClient);
            //     self.dcClient = setupDataChannel(self.pcClient, 'cirrus', self.dataChannelOptions);
            //     handleCreateOffer(self.pcClient);
            // });
            
        };

        //Called externaly when an answer is received from the server
        this.receiveAnswer = function(answer) {
            console.log(`Received answer:\n${answer}`);
            var answerDesc = new RTCSessionDescription(answer);
            self.pcClient.setRemoteDescription(answerDesc);

            let receivers = self.pcClient.getReceivers();
            for(let receiver of receivers)
            {
                receiver.playoutDelayHint = 0;
            }
        };

        this.close = function(){
            if(self.pcClient){
                console.log("Closing existing peerClient")
                self.pcClient.close();
                self.pcClient = null;
            }
            if(self.aggregateStatsIntervalId)
                clearInterval(self.aggregateStatsIntervalId);
        }

        //Sends data across the datachannel
        this.send = function(data){
            if(self.dcClient && self.dcClient.readyState == 'open'){
                //console.log('Sending data on dataconnection', self.dcClient)
                self.dcClient.send(data);
            }
        };

        this.getStats = function(onStats){
            if(self.pcClient && onStats){
                self.pcClient.getStats(null).then((stats) => { 
                    onStats(stats); 
                });
            }
        }

        this.aggregateStats = function(checkInterval){
            let calcAggregatedStats = generateAggregatedStatsFunction();
            let printAggregatedStats = () => { self.getStats(calcAggregatedStats); }
            self.aggregateStatsIntervalId = setInterval(printAggregatedStats, checkInterval);
        }
    };

    return webRtcPlayer;
  
}));
