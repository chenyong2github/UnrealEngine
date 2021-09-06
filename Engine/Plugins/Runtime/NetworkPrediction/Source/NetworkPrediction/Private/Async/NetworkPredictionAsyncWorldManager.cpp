// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/NetworkPredictionAsyncWorldManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace UE_NP {

void FNetworkPredictionAsyncWorldManager::RegisterController(FNetworkPredictionAsyncID ID, APlayerController* PC)
{
	const bool bIsServer = IsServer();
	const bool bEnableClientInputBuffering = false;
	
	bool bIsLocalController = false;
	ENetworkPredictionAsyncInputSource InputSource = ENetworkPredictionAsyncInputSource::None;

	if (PC)
	{
		bIsLocalController = PC->IsLocalController();
		FControllerInfo* ControllerInfo = PlayerControllerMap.FindByKey(ID);
		if (!ControllerInfo)
		{
			ControllerInfo = &PlayerControllerMap.AddDefaulted_GetRef();
		}
		ControllerInfo->ID = ID;
		ControllerInfo->PC = PC;
		ControllerInfo->LastFrame = 0;

		// Server: non local controllers pull from the buffer
		// client: pull from buffer only if local input buffering enabled
		if ((bIsServer && !bIsLocalController) || (!bIsServer && bEnableClientInputBuffering))
		{
			InputSource = ENetworkPredictionAsyncInputSource::Buffered;
		}
		else
		{
			// Otherwise, pull from local PendingInputCmd that this sim registered with
			InputSource = ENetworkPredictionAsyncInputSource::Local;
		}

		ControllerInfo->InputSource = InputSource;
	}
	else
	{
		int32 ExistingIdx = PlayerControllerMap.IndexOfByKey(ID);
		if (ExistingIdx != INDEX_NONE)
		{
			PlayerControllerMap.RemoveAt(ExistingIdx, 1, false);
		}
		if (bIsServer)
		{
			// Non PC backed sim: only pull input cmd if on server
			InputSource = ENetworkPredictionAsyncInputSource::Local;
			bIsLocalController = true;
		}
	}

	SetInputSource(ID, InputSource, bIsLocalController);
}

bool FNetworkPredictionAsyncWorldManager::IsServer() const
{
	npCheckSlow(World);
	return World->GetNetMode() != NM_Client;
}

void FNetworkPredictionAsyncWorldManager::NetSerializePlayerControllerInputCmds(int32 PhysicsStep, TFunctionRef<void(FNetworkPredictionAsyncID, int32, FArchive&)> NetSerializeFunc)
{
	const bool bIsServer = IsServer();

	if (bIsServer)
	{
		for (FControllerInfo& Info : PlayerControllerMap)
		{
			// Server: Non locally controlled. look for a Cmd from the PlayerController InputCmd Bus
			APlayerController::FServerFrameInfo& PCFrameInfo = Info.PC->GetServerFrameInfo();
			APlayerController::FInputCmdBuffer& PCInputBuffer = Info.PC->GetInputBuffer();

			if (PCFrameInfo.LastProcessedInputFrame != INDEX_NONE)
			{
				auto NetSerializeCmd = [&](int32 PCFrame, int32 SimFrame)
				{
					const TArray<uint8>& Data = PCInputBuffer.Get(PCFrame);
					if (Data.Num() > 0)
					{
						uint8* RawData = const_cast<uint8*>(Data.GetData());
						FNetBitReader Ar(nullptr, RawData, ((int64)Data.Num()) << 3);
						NetSerializeFunc(Info.ID, SimFrame, Ar);
						Info.LastFrame = SimFrame;
					}
				};

				if (PCFrameInfo.bFault)
				{
					// While in fault just write the current input to the buffer and don't bother with future inputs
					if (Info.LastFrame != PhysicsStep)
					{
						NetSerializeCmd(PCFrameInfo.LastProcessedInputFrame, PhysicsStep);
					}
				}
				else
				{
					// De-NetSerialize all new InputCmds from PC's buffer ["Client Frame" based] to our internal typed buffer ["local PhysicsStep Frame" based]
					const int32 CmdOffset = PCFrameInfo.LastProcessedInputFrame - PhysicsStep; // Map PC frame to local Physics Step
					const int32 WriteStart = FMath::Max(PhysicsStep, Info.LastFrame+1); // Start at the current PhysicsStep or highest frame we haven't processed
					const int32 WriteEnd = PCInputBuffer.HeadFrame() - CmdOffset; // go until we reach the end of valid data in the PC input buffer

					for (int32 Frame = WriteStart; Frame <= WriteEnd; ++Frame)
					{
						//UE_LOG(LogTemp, Log, TEXT("[S] De-NetSerialize Cmd %d Frame %d. NumBytes: %d"), Frame + CmdOffset, Frame, PCInputBuffer.Get(Frame + CmdOffset).Num());
						NetSerializeCmd(Frame + CmdOffset, Frame);
					}
				}
			}
		}
	}
	else
	{
		for (FControllerInfo& Info : PlayerControllerMap)
		{
			if (Info.ID.IsClientGenerated())
			{
				continue;
			}

			FNetBitWriter Writer(nullptr, 256 << 3);			
			const int32 InputFrame = Info.InputSource == ENetworkPredictionAsyncInputSource::Buffered ? PhysicsStep : INDEX_NONE;
			NetSerializeFunc(Info.ID, InputFrame, Writer);

			if (Writer.GetNumBits() > 0)
			{
				// We will probably need settings or some way to not send client->server at extremely high rates
				// for now, its every game thread frame
				TArray<uint8> SendData;
				if (Writer.IsError() == false)
				{
					int32 NumBytes = (int32)Writer.GetNumBytes();
					SendData = MoveTemp(*const_cast<TArray<uint8>*>(Writer.GetBuffer()));
					SendData.SetNum(NumBytes);
				}


				Info.PC->PushClientInput(PhysicsStep, SendData);
			}
		}
	}
}

}; // namespace UE_NP