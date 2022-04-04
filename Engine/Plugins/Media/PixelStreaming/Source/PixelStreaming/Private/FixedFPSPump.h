// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/Event.h"
#include "HAL/CriticalSection.h"
#include "Misc/SingleThreadRunnable.h"
#include "WebRTCIncludes.h"
#include "PixelStreamingPumpable.h"

namespace UE::PixelStreaming
{
	/*
	* Runs a seperate thread that "pumps" at a fixed FPS interval. "Pumpables" may add themselves to be "pumped" 
	* at a fixed interval - indepedent of render FPS or game thread FPS. This is useful in many cases where a poorly 
	* performing applications should still do a thing at a fixed interval. For example, we want a constant amount of 
	* frames submitted to WebRTC so we are not penalized with a bitrate reduction.
	*/
	class FFixedFPSPump
	{
	public:
		FFixedFPSPump();
		~FFixedFPSPump();
		void Shutdown();
		void RegisterPumpable(rtc::scoped_refptr<FPixelStreamingPumpable> Pumpable);
		void UnregisterPumpable(rtc::scoped_refptr<FPixelStreamingPumpable> Pumpable);
		void PumpAll();

	private:
		class FPumpRunnable : public FRunnable, public FSingleThreadRunnable
		{
		public:
			FPumpRunnable();
			void PumpAll();

			// Begin FRunnable interface.
			virtual bool Init() override;
			virtual uint32 Run() override;
			virtual void Stop() override;
			virtual void Exit() override;
			virtual FSingleThreadRunnable* GetSingleThreadInterface() override
			{
				bIsRunning = true;
				return this;
			};
			// End FRunnable interface

			// Begin FSingleThreadRunnable interface.
			virtual void Tick() override;
			// End FSingleThreadRunnable interface

			bool bIsRunning = false;
			FEvent* NextPumpEvent;
			FCriticalSection SourcesGuard;
			TMap<FPixelStreamingPumpable*, rtc::scoped_refptr<FPixelStreamingPumpable>> Pumpables;
			int32 NextFrameId = 0;
			uint64 LastPumpCycles;
			uint64 NextPumpCycles;
		};
		FPumpRunnable Runnable;
		FRunnableThread* PumpThread;
	};
} // namespace UE::PixelStreaming
