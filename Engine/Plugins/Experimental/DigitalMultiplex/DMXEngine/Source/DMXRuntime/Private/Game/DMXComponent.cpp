// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/DMXComponent.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXProtocolTypes.h"
#include "Async/Async.h"

UDMXComponent::UDMXComponent()
	: bBufferUpdated(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
}

UDMXComponent::~UDMXComponent()
{
	ReleasePacketReceiver();
}

// Called when the game starts
void UDMXComponent::BeginPlay()
{
	Super::BeginPlay();
	// Do on begin play because fixture patch is changed in editor.
	ResetPacketReceiver();
}

UDMXEntityFixturePatch* UDMXComponent::GetFixturePatch() const
{
	return FixturePatchRef.GetFixturePatch();
}

void UDMXComponent::SetFixturePatch(UDMXEntityFixturePatch* InFixturePatch)
{
	FixturePatchRef.SetEntity(InFixturePatch);
	ResetPacketReceiver();
}

void UDMXComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Update on Tick if we receive at least one new packet with changes for fixture
	if (bBufferUpdated)
	{
		// There is no Lock because the array is small and we copy it
		OnFixturePatchReceived.Broadcast(GetFixturePatch(), ChannelBuffer);
	}

	// Reset update flag
	bBufferUpdated = false;
}

void UDMXComponent::SetupPacketReceiver()
{
	UDMXEntityFixturePatch* FixturePatch = GetFixturePatch();
	if (!DMXComponentReceiveHandle.IsValid() && FixturePatch != nullptr && FixturePatch->GetRelevantControllers().Num() > 0)
	{
		FName&& Protocol = FixturePatch->GetRelevantControllers()[0]->GetProtocol();
		if (IDMXProtocolPtr DMXProtocolPtr = IDMXProtocol::Get(Protocol))
		{
			DMXComponentReceiveHandle = DMXProtocolPtr->GetOnUniverseInputBufferUpdated().AddUObject(this, &UDMXComponent::PacketReceiver);

			const int32 FixturePatchRemoteUniverse = FixturePatch->GetRemoteUniverse();

			TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> ProtocolUniverse = DMXProtocolPtr->GetUniverseById(FixturePatchRemoteUniverse);
			if (ProtocolUniverse.IsValid())
			{
				FDMXBufferPtr Buffer = ProtocolUniverse.Get()->GetInputDMXBuffer();
				if (Buffer.IsValid())
				{
					const int32 NumOfChannels = FixturePatch->GetChannelSpan();
					// We do -1 because index 0 = channel 1
					const int32 StartingIndex = FixturePatch->GetStartingChannel() - 1;

					for (uint8 ChannelIt = 0; ChannelIt < NumOfChannels; ChannelIt++)
					{
						ChannelBuffer.Add(Buffer->GetDMXDataAddress(StartingIndex + ChannelIt));
					}
				}
			}
		}
	}
}

void UDMXComponent::ReleasePacketReceiver()
{
	if (DMXComponentReceiveHandle.IsValid())
	{
		DMXComponentReceiveHandle.Reset();
	}
}

void UDMXComponent::ResetPacketReceiver()
{
	ReleasePacketReceiver();
	SetupPacketReceiver();
}

void UDMXComponent::PacketReceiver(FName InProtocol, uint16 InUniverseID, const TArray<uint8>& InValues)
{
	// It should be called in Game Thread
	// Otherwise, we might experience creches when Garbage collector destroying UObject on a game thread
	AsyncTask(ENamedThreads::GameThread, [this, InUniverseID, InValues]() {
		// If this gets called after FEngineLoop::Exit(), GetEngineSubsystem() can crash
		if (IsValidLowLevel() && !IsEngineExitRequested() && GetFixturePatch()->GetRelevantControllers().Num() > 0)
		{
			const int32 FixturePatchRemoteUniverse = GetFixturePatch()->GetRemoteUniverse();

			if (InUniverseID == FixturePatchRemoteUniverse)
			{
				int32 StartingIndex = GetFixturePatch()->GetStartingChannel() - 1;
				int32 NumOfChannels = GetFixturePatch()->GetChannelSpan();

				for (uint8 ChannelIt = 0; ChannelIt < NumOfChannels; ChannelIt++)
				{
					if (InValues[StartingIndex + ChannelIt] != ChannelBuffer[ChannelIt])
					{
						// Requst the update on next tick
						bBufferUpdated = true;

						// There is no Lock because the array is small and we copy it
						ChannelBuffer[ChannelIt] = InValues[StartingIndex + ChannelIt];
					}
				}
			}
		}
	});
}
