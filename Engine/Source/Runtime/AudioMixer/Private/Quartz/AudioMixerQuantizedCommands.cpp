// Copyright Epic Games, Inc. All Rights Reserved.

#include "Quartz/AudioMixerQuantizedCommands.h"
#include "AudioMixerSourceManager.h"

namespace Audio
{
	
	FQuantizedPlayCommand::FQuantizedPlayCommand()
	{
	}

	TSharedPtr<IQuartzQuantizedCommand> FQuantizedPlayCommand::GetDeepCopyOfDerivedObject() const
	{
		TSharedPtr<FQuantizedPlayCommand> NewCopy = MakeShared<FQuantizedPlayCommand>();

		NewCopy->OwningClockPtr = OwningClockPtr;
		NewCopy->SourceID = SourceID;

		return NewCopy;
	}

	void FQuantizedPlayCommand::OnQueuedCustom(const FQuartzQuantizedCommandInitInfo& InCommandInitInfo)
	{
		OwningClockPtr = InCommandInitInfo.OwningClockPointer;
		SourceID = InCommandInitInfo.SourceID;

		// access source manager through owning clock (via clock manager)
		FMixerSourceManager* SourceManager = OwningClockPtr->GetSourceManager();
		if (SourceManager)
		{
			SourceManager->PauseSoundForQuantizationCommand(SourceID);
		}
		else
		{
			// cancel ourselves (no source manager is bad news)
			check(SourceManager);
			OwningClockPtr->CancelQuantizedCommand(TSharedPtr<IQuartzQuantizedCommand>(this));
		}
		
	}

	// TODO: think about playback progress of a sound source
	// TODO: AudioComponent "waiting to play" state (cancel-able)
	void FQuantizedPlayCommand::OnFinalCallbackCustom(int32 InNumFramesLeft)
	{
		// Access source manager through owning clock (via clock manager)
		check(OwningClockPtr && OwningClockPtr->GetMixerDevice() && OwningClockPtr->GetMixerDevice()->GetSourceManager());

		// access source manager through owning clock (via clock manager)
		// Owning Clock Ptr may be nullptr if this command was canceled.
		if (OwningClockPtr)
		{
			FMixerSourceManager* SourceManager = OwningClockPtr->GetSourceManager();
			if (SourceManager)
			{
				SourceManager->SetSubBufferDelayForSound(SourceID, InNumFramesLeft);
				SourceManager->UnPauseSoundForQuantizationCommand(SourceID);
			}
			else
			{
				// cancel ourselves
				OwningClockPtr->CancelQuantizedCommand(TSharedPtr<IQuartzQuantizedCommand>(this));
			}
		}

	}

	void FQuantizedPlayCommand::CancelCustom()
	{
		// release hold on pending source
		OnFinalCallbackCustom(0);
	}

	TSharedPtr<IQuartzQuantizedCommand> FQuantizedTickRateChange::GetDeepCopyOfDerivedObject() const
	{
		TSharedPtr<FQuantizedTickRateChange> NewCopy = MakeShared<FQuantizedTickRateChange>();

		NewCopy->OwningClockPtr = OwningClockPtr;
		NewCopy->TickRate = TickRate;

		return NewCopy;
	}
	
	void FQuantizedTickRateChange::OnQueuedCustom(const FQuartzQuantizedCommandInitInfo& InCommandInitInfo)
	{
		OwningClockPtr = InCommandInitInfo.OwningClockPointer;
	}
	
	void FQuantizedTickRateChange::OnFinalCallbackCustom(int32 InNumFramesLeft)
	{
		OwningClockPtr->ChangeTickRate(TickRate, InNumFramesLeft);
	}

	TSharedPtr<IQuartzQuantizedCommand> FQuantizedTransportReset::GetDeepCopyOfDerivedObject() const
	{
		TSharedPtr<FQuantizedTransportReset> NewCopy = MakeShared<FQuantizedTransportReset>();

		NewCopy->OwningClockPtr = OwningClockPtr;

		return NewCopy;
	}

	void FQuantizedTransportReset::OnQueuedCustom(const FQuartzQuantizedCommandInitInfo& InCommandInitInfo)
	{
		OwningClockPtr = InCommandInitInfo.OwningClockPointer;
	}

	void FQuantizedTransportReset::OnFinalCallbackCustom(int32 InNumFramesLeft)
	{
		OwningClockPtr->ResetTransport();
	}

} // namespace Audio
