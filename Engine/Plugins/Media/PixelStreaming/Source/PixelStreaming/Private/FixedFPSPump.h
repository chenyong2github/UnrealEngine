// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PixelStreamingPlayerId.h"
#include "HAL/Thread.h"
#include "WebRTCIncludes.h"
#include "IPixelStreamingPumpedVideoSource.h"

namespace UE
{
	namespace PixelStreaming
	{
		/*
		* Runs a seperate thread that "pumps" at a fixed FPS interval. WebRTC video sources may add themselves to be "pumped" 
		* at a fixed interval indepedent of render FPS or game thread FPS. This is useful so that poorly 
		* performing applications can still submit a constant amount of frames to WebRTC and are not penalized with a 
		* bitrate reduction.
		*/
		class FFixedFPSPump
		{

		public:
			FFixedFPSPump();
			~FFixedFPSPump();
			void Shutdown();
			void RegisterVideoSource(FPixelStreamingPlayerId PlayerId, IPumpedVideoSource* Source);
			void UnregisterVideoSource(FPixelStreamingPlayerId PlayerId);
			static FFixedFPSPump* Get();

		private:
			void PumpLoop();

		private:
			FCriticalSection SourcesGuard;
			TMap<FPixelStreamingPlayerId, IPumpedVideoSource*> VideoSources;

			TUniquePtr<FThread> PumpThread;
			bool bThreadRunning = true;
			//FEvent* PlayersChangedEvent;
			FEvent* NextPumpEvent;
			int32 NextFrameId = 0;

			static FFixedFPSPump* Instance;
		};
	} // namespace PixelStreaming
} // namespace UE
