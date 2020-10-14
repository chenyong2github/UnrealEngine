// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/DMXComponent.h"

#include "DMXRuntimeLog.h"
#include "DMXProtocolTypes.h"
#include "DMXStats.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "Library/DMXEntityFixturePatch.h"

#include "Async/Async.h"
#include "UObject/GCObject.h"


// Stats
DECLARE_CYCLE_STAT(TEXT("DMXComponent copy DMXBuffer"), STAT_DMXComponentCopyDMXBuffer, STATGROUP_DMX);

/**
 * FBufferUpdatedReceiver is helper receiver class, which is holding the Packet Receiver which is executed by delegate call
 */
class FBufferUpdatedReceiver
	: public TSharedFromThis<FBufferUpdatedReceiver>
	, public FGCObject
{
public:
	static TSharedPtr<FBufferUpdatedReceiver> TryCreate(UDMXComponent* InDMXComponent)
	{
		TSharedPtr<FBufferUpdatedReceiver> NewReceiver = MakeShared<FBufferUpdatedReceiver>();		
		if (InDMXComponent && InDMXComponent->GetFixturePatch())
		{
			NewReceiver->DMXComponent = InDMXComponent;
			NewReceiver->FixturePatch = InDMXComponent->GetFixturePatch();
			return NewReceiver;
		}

		return nullptr;
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(DMXComponent);
		Collector.AddReferencedObject(FixturePatch);
	}

	void HandleDMXBufferUpdated(FName InProtocol, uint16 InUniverseID, const TArray<uint8>& InValues)
	{
		constexpr ENamedThreads::Type GameThread = ENamedThreads::GameThread;
		static_assert(GameThread == ENamedThreads::GameThread, "Actual receiving must be run on game thread. We assume ChannelBuffer is updated on same thread as Tick to avoid synchronization.");

		AsyncTask(GameThread, [this, InUniverseID, InValues]() {
			// If this gets called after FEngineLoop::Exit(), GetEngineSubsystem() can crash
			if (DMXComponent && DMXComponent->IsValidLowLevel() &&
				FixturePatch && FixturePatch->IsValidLowLevel() &&
				!IsEngineExitRequested())
			{
				SCOPE_CYCLE_COUNTER(STAT_DMXComponentCopyDMXBuffer);

				const int32 FixturePatchRemoteUniverse = FixturePatch->GetRemoteUniverse();

				if (InUniverseID == FixturePatchRemoteUniverse)
				{
					const int32 StartingIndex = FixturePatch->GetStartingChannel() - 1;
					const int32 NumChannels = FixturePatch->GetChannelSpan();

					// In cases where the user changes the num channels of the fixutre type, we have to restart the receiver
					if (NumChannels != DMXComponent->ChannelBuffer.Num())
					{
						DMXComponent->RestartPacketReceiver();
						return;
					}

					// Copy the data to the component's dmx buffer
					TArray<uint8> NewValuesArray(&InValues.GetData()[StartingIndex], NumChannels);

					// Update the component only if values changed
					if (NewValuesArray != DMXComponent->ChannelBuffer)
					{
						// Request the update on next tick
						DMXComponent->bBufferUpdated = true;

						// Explicitly movement assign the array, profiler showed benefits in debug build
						DMXComponent->ChannelBuffer = MoveTemp(NewValuesArray);
					}
				}
			}
		});
	}

private:
	UDMXComponent* DMXComponent;

	UDMXEntityFixturePatch* FixturePatch;
};

UDMXComponent::UDMXComponent()
	: bBufferUpdated(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
}

#if WITH_EDITOR
void UDMXComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXComponent, FixturePatchRef))
	{
		RestartPacketReceiver();
	}
}
#endif // WITH_EDITOR

// Called when the game starts
void UDMXComponent::BeginPlay()
{
	Super::BeginPlay();

	// Do on begin play because fixture patch is changed in editor.
	RestartPacketReceiver();
}

UDMXEntityFixturePatch* UDMXComponent::GetFixturePatch() const
{
	return FixturePatchRef.GetFixturePatch();
}

void UDMXComponent::SetFixturePatch(UDMXEntityFixturePatch* InFixturePatch)
{
	FixturePatchRef.SetEntity(InFixturePatch);

	RestartPacketReceiver();
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

void UDMXComponent::RestartPacketReceiver()
{
	ChannelBuffer.Reset();

	UDMXEntityFixturePatch* FixturePatch = GetFixturePatch();
	if (FixturePatch != nullptr && FixturePatch->GetRelevantControllers().Num() > 0)
	{
		for (UDMXEntityController* Controller : FixturePatch->GetRelevantControllers())
		{
			FName&& Protocol = Controller->GetProtocol();
			if (IDMXProtocolPtr DMXProtocolPtr = IDMXProtocol::Get(Protocol))
			{
				DMXProtocolPtr->GetOnUniverseInputBufferUpdated().RemoveAll(this);

				// Instead binding UObject we bound Shared Pointer
				// Pointer we make sure it is never garbage collected before UDMX Component destroyed itself
				BufferUpdatedReceiver = FBufferUpdatedReceiver::TryCreate(this);

				if (BufferUpdatedReceiver.IsValid())
				{
					DMXProtocolPtr->GetOnUniverseInputBufferUpdated().Add(IDMXProtocol::FOnUniverseInputBufferUpdated::FDelegate::CreateSP(BufferUpdatedReceiver.Get(), &FBufferUpdatedReceiver::HandleDMXBufferUpdated));
					const int32 FixturePatchRemoteUniverse = FixturePatch->GetRemoteUniverse();

					TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> ProtocolUniverse = DMXProtocolPtr->GetUniverseById(FixturePatchRemoteUniverse);
					if (ProtocolUniverse.IsValid())
					{
						FDMXBufferPtr Buffer = ProtocolUniverse.Get()->GetInputDMXBuffer();
						if (Buffer.IsValid())
						{
							const int32 NumOfChannels = FMath::Clamp(FixturePatch->GetChannelSpan(), 0, 512);
							// We do -1 because index 0 = channel 1
							const int32 StartingIndex = FixturePatch->GetStartingChannel() - 1;

							for (int32 ChannelIt = 0; ChannelIt < NumOfChannels; ChannelIt++)
							{
								ChannelBuffer.Add(Buffer->GetDMXDataAddress(StartingIndex + ChannelIt));
							}
						}
					}
				}
			}
		}
	}
}
