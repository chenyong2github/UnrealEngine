// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/DMXComponent.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXProtocolTypes.h"
#include "Async/Async.h"

/**
 * FBufferUpdatedReceiver is helper receiver class, which is holding the Packet Receiver which is executed by delegate call
 */
class FBufferUpdatedReceiver
	: public TSharedFromThis<FBufferUpdatedReceiver>
{
public:
	FBufferUpdatedReceiver(UDMXComponent* InDMXComponent) 
		: DMXComponentPtr(InDMXComponent) {}

	void PacketReceiver(FName InProtocol, uint16 InUniverseID, const TArray<uint8>& InValues)
	{
		if (UDMXComponent* DMXComponent = DMXComponentPtr.Get())
		{
			// It should be called in Game Thread
			// Otherwise, we might experience creches when Garbage collector destroying UObject on a game thread
			AsyncTask(ENamedThreads::GameThread, [this, DMXComponent, InUniverseID, InValues]() {
				// If this gets called after FEngineLoop::Exit(), GetEngineSubsystem() can crash
				if (DMXComponent->IsValidLowLevel() && !IsEngineExitRequested() && DMXComponent->GetFixturePatch()->GetRelevantControllers().Num() > 0)
				{
					const int32 FixturePatchRemoteUniverse = DMXComponent->GetFixturePatch()->GetRemoteUniverse();

					if (InUniverseID == FixturePatchRemoteUniverse)
					{
						int32 StartingIndex = DMXComponent->GetFixturePatch()->GetStartingChannel() - 1;
						int32 NumOfChannels = DMXComponent->GetFixturePatch()->GetChannelSpan();

						for (uint8 ChannelIt = 0; ChannelIt < NumOfChannels; ChannelIt++)
						{
							if (InValues[StartingIndex + ChannelIt] != DMXComponent->ChannelBuffer[ChannelIt])
							{
								// Requst the update on next tick
								DMXComponent->bBufferUpdated = true;

								checkf(IsInGameThread(), TEXT("We assume ChannelBuffer is updated on same thread as Tick to avoid synchronization."));
								DMXComponent->ChannelBuffer[ChannelIt] = InValues[StartingIndex + ChannelIt];
							}
						}
					}
				}
			});
		}
	}

private:
	TWeakObjectPtr<UDMXComponent> DMXComponentPtr;
};

UDMXComponent::UDMXComponent()
	: bBufferUpdated(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
}

// Called when the game starts
void UDMXComponent::BeginPlay()
{
	Super::BeginPlay();

	// Do on begin play because fixture patch is changed in editor.
	SetupPacketReceiver();
}

UDMXEntityFixturePatch* UDMXComponent::GetFixturePatch() const
{
	return FixturePatchRef.GetFixturePatch();
}

void UDMXComponent::SetFixturePatch(UDMXEntityFixturePatch* InFixturePatch)
{
	FixturePatchRef.SetEntity(InFixturePatch);

	SetupPacketReceiver();
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
	if (FixturePatch != nullptr && FixturePatch->GetRelevantControllers().Num() > 0)
	{
		FName&& Protocol = FixturePatch->GetRelevantControllers()[0]->GetProtocol();
		if (IDMXProtocolPtr DMXProtocolPtr = IDMXProtocol::Get(Protocol))
		{
			// Instead binding UObject we bound Shared Pointer
			// Pointer we make sure it is never garbage collected before UDMX Component destroyed itself
			BufferUpdatedReceiver = MakeShared<FBufferUpdatedReceiver>(this);
			DMXProtocolPtr->GetOnUniverseInputBufferUpdated().Add(IDMXProtocol::FOnUniverseInputBufferUpdated::FDelegate::CreateSP(BufferUpdatedReceiver.Get(), &FBufferUpdatedReceiver::PacketReceiver));

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
