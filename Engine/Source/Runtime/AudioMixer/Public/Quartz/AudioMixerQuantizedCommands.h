// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioMixerClock.h"

namespace Audio
{
	// QuartzQuantizedCommand that plays a sound on a sample-accurate boundary
	class AUDIOMIXER_API FQuantizedPlayCommand : public IQuartzQuantizedCommand
	{
	public:
		// ctor
		FQuantizedPlayCommand();

		// dtor
		~FQuantizedPlayCommand() {}

		virtual TSharedPtr<IQuartzQuantizedCommand> GetDeepCopyOfDerivedObject() const override;

		virtual void OnQueuedCustom(const FQuartzQuantizedCommandInitInfo& InCommandInitInfo) override;

		virtual void OnFinalCallbackCustom(int32 InNumFramesLeft) override;

		virtual void CancelCustom() override;

	private:
		FQuartzClock* OwningClockPtr{ nullptr };

		int32 SourceID{ -1 };

	}; // class FQuantizedPlayCommand 


	// QuartzQuantizedCommand that changes the TickRate of a clock on a sample-accurate boundary (i.e. BPM changes)
	class AUDIOMIXER_API FQuantizedTickRateChange : public IQuartzQuantizedCommand
	{
	public:
		void SetTickRate(const FQuartzClockTickRate& InTickRate)
		{
			TickRate = InTickRate;
		}

		virtual TSharedPtr<IQuartzQuantizedCommand> GetDeepCopyOfDerivedObject() const override;

		virtual void OnQueuedCustom(const FQuartzQuantizedCommandInitInfo& InCommandInitInfo) override;

		virtual void OnFinalCallbackCustom(int32 InNumFramesLeft) override;

		virtual bool IsClockAltering() override { return true; }

	private:
		FQuartzClockTickRate TickRate;
		FQuartzClock* OwningClockPtr{ nullptr };

	}; // class FQuantizedTickRateChange 


	// QuartzQuantizedCommand that resets the transport of a clock's metronome on a sample-accurate boundary
	class AUDIOMIXER_API FQuantizedTransportReset : public IQuartzQuantizedCommand
	{
	public:
		virtual TSharedPtr<IQuartzQuantizedCommand> GetDeepCopyOfDerivedObject() const override;

		virtual void OnQueuedCustom(const FQuartzQuantizedCommandInitInfo& InCommandInitInfo) override;

		virtual void OnFinalCallbackCustom(int32 InNumFramesLeft) override;

		virtual bool IsClockAltering() override { return false; }

	private:
		FQuartzClock* OwningClockPtr{ nullptr };

	}; // class FQuantizedTransportReset 

} // namespace Audio
