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
				// cancel ourselves (no source manager is bad news)
				check(SourceManager);
				OwningClockPtr->CancelQuantizedCommand(TSharedPtr<IQuartzQuantizedCommand>(this));
			}
		}

	}

	void FQuantizedPlayCommand::CancelCustom()
	{
		// release hold on pending source
		OnFinalCallbackCustom(0);
	}

	static const FName PlayCommandName("Play Command");
	FName FQuantizedPlayCommand::GetCommandName() const
	{
		return PlayCommandName;
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

	static const FName TickRateChangeCommandName("Tick Rate Change Command");
	FName FQuantizedTickRateChange::GetCommandName() const
	{
		return TickRateChangeCommandName;
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

	static const FName TransportResetCommandName("Transport Reset Command");
	FName FQuantizedTransportReset::GetCommandName() const
	{
		return TransportResetCommandName;
	}


	TSharedPtr<IQuartzQuantizedCommand> FQuantizedOtherClockStart::GetDeepCopyOfDerivedObject() const
	{
		TSharedPtr<FQuantizedOtherClockStart> NewCopy = MakeShared<FQuantizedOtherClockStart>();

		NewCopy->OwningClockPtr = OwningClockPtr;
		NewCopy->NameOfClockToStart = NameOfClockToStart;

		return NewCopy;
	}

	void FQuantizedOtherClockStart::OnQueuedCustom(const FQuartzQuantizedCommandInitInfo& InCommandInitInfo)
	{
		OwningClockPtr = InCommandInitInfo.OwningClockPointer;
		check(OwningClockPtr.IsValid());

		NameOfClockToStart = InCommandInitInfo.OtherClockName;

		// shouldn't be trying to start the same clock that owns this command
		if (OwningClockPtr->GetName() == NameOfClockToStart)
		{
			UE_LOG(LogAudioQuartz, Warning, TEXT("Clock: (%s) is attempting to start itself on a quantization boundary.  Ignoring command"), *NameOfClockToStart.ToString());
			OwningClockPtr->CancelQuantizedCommand(TSharedPtr<IQuartzQuantizedCommand>(this));
		}
	}

	void FQuantizedOtherClockStart::OnFinalCallbackCustom(int32 InNumFramesLeft)
	{
		// get access to the source & clock manager
		// access source manager through owning clock (via clock manager)
		FMixerSourceManager* SourceManager = OwningClockPtr->GetSourceManager();
		FQuartzClockManager* ClockManager = OwningClockPtr->GetClockManager();

		check(SourceManager && ClockManager && OwningClockPtr.IsValid());
		bool bShouldStart = SourceManager && ClockManager && OwningClockPtr.IsValid();
		bShouldStart |= !ClockManager->IsClockRunning(NameOfClockToStart);

		if (bShouldStart)
		{
			// ...start the clock
			ClockManager->ResumeClock(NameOfClockToStart, InNumFramesLeft);

			if (ClockManager->HasClockBeenTickedThisUpdate(NameOfClockToStart))
			{
				ClockManager->UpdateClock(NameOfClockToStart, ClockManager->GetLastUpdateSizeInFrames());
			}
		}
	}

	static const FName StartOtherClockName("Start Other Clock Command");
	FName FQuantizedOtherClockStart::GetCommandName() const
	{
		return StartOtherClockName;
	}

} // namespace Audio
