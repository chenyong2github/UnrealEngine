// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PixelStreamingPlayerId.h"
#include "HAL/Thread.h"
#include "WebRTCIncludes.h"

namespace UE {
	namespace PixelStreaming {

		class IPumpedVideoSource : public rtc::RefCountInterface
		{
			public:
				virtual void OnPump(int32 FrameId) = 0;
				virtual bool IsReadyForPump() const = 0;
				
				/* rtc::RefCountInterface */
				virtual void AddRef() const = 0;
				virtual rtc::RefCountReleaseStatus Release() const = 0;
				virtual bool HasOneRef() const = 0;
		};

		/*
		* Runs a seperate thread that "pumps" at a fixed FPS interval. WebRTC video sources may add themselves to be "pumped" 
		* at a fixed interval indepedent of render FPS or game thread FPS. This is useful so that poorly 
		* performing applications can still submit a constant amount of frames to WebRTC and are not penalized with a 
		* bitrate reduction.
		*/
		class FFixedFPSPump
		{

		private:
			// Private constructor as there should only ever be one instance of this class and the intended design is to use the static Get() method.
			FFixedFPSPump();
			~FFixedFPSPump();
			void PumpLoop();

		public:
			
			// Single accessor.
			static FFixedFPSPump& Get();

			void RegisterVideoSource(FPixelStreamingPlayerId PlayerId, rtc::scoped_refptr<IPumpedVideoSource> Source);
			void UnregisterVideoSource(FPixelStreamingPlayerId PlayerId);

		private:
			FCriticalSection SourcesGuard;
			TMap<FPixelStreamingPlayerId, rtc::scoped_refptr<IPumpedVideoSource>> VideoSources;


			TUniquePtr<FThread> PumpThread;
			bool bThreadRunning = true;
			//FEvent* PlayersChangedEvent;
			FEvent* NextPumpEvent;
			int32 NextFrameId = 0;

		};
	}
}
