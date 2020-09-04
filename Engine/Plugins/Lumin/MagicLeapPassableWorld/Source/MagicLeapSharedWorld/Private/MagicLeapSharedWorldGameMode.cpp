// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapSharedWorldGameMode.h"
#include "MagicLeapSharedWorldPlayerController.h"
#include "MagicLeapSharedWorldGameState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"

constexpr float kPinSelectionConfidenceThreshold = 0.895f;

AMagicLeapSharedWorldGameMode::AMagicLeapSharedWorldGameMode(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, PinSelectionConfidenceThreshold(kPinSelectionConfidenceThreshold)
, ChosenOne(nullptr)
{
	GameStateClass = AMagicLeapSharedWorldGameState::StaticClass();
	PlayerControllerClass = AMagicLeapSharedWorldPlayerController::StaticClass();
}

void AMagicLeapSharedWorldGameMode::DetermineSharedWorldData_Implementation(FMagicLeapSharedWorldSharedData& NewSharedWorldData)
{
	int32 NumNonSpectatorPlayers = 0;

	for (const auto& PlayerData : PlayerToLocalPins)
	{
		check(PlayerData.Key != nullptr);
		if (PlayerData.Key->PlayerState != nullptr)
		{
			if (PlayerData.Key->PlayerState->IsSpectator())
			{
				// ignore spectators
				continue;
			}
		}
		else
		{
			UE_LOG(LogMagicLeapSharedWorld, Warning, TEXT("PlayerState for %s was null. Unable to determine if player is a spectator. Counting it as a contributor for shared pins."), *(PlayerData.Key->GetName()));
		}

		++NumNonSpectatorPlayers;
	}

	NewSharedWorldData.PinIDs.Reset();
	for (const auto& Pair : PinToOwnersToStates)
	{
		if (Pair.Value.Num() == NumNonSpectatorPlayers)
		{
			bool bSatisfiesConfidenceThreshold = true;
			for (const auto& OwnerToStatePair : Pair.Value)
			{
				// TODO : use epsilon error tolerace
				if (OwnerToStatePair.Value.Confidence < PinSelectionConfidenceThreshold)
				{
					bSatisfiesConfidenceThreshold = false;
					break;
				}
			}

			if (bSatisfiesConfidenceThreshold)
			{
				NewSharedWorldData.PinIDs.Add(Pair.Key);
			}
		}
	}
}

void AMagicLeapSharedWorldGameMode::SelectChosenOne_Implementation()
{
	TMap<AMagicLeapSharedWorldPlayerController*, float> PlayerToTotalConfidence;
	AMagicLeapSharedWorldPlayerController* PotentialChosenOne = nullptr;
	float MaxConfidence = 0.0f;

	for (const FGuid& PinID : SharedWorldData.PinIDs)
	{
		const TMap<AMagicLeapSharedWorldPlayerController*, FMagicLeapARPinState>* OwnersToStates = PinToOwnersToStates.Find(PinID);
		if (OwnersToStates != nullptr)
		{
			for (const auto& OwnersAndStates : *OwnersToStates)
			{
				float& TotalConfidence = PlayerToTotalConfidence.FindOrAdd(OwnersAndStates.Key, 0.0f);
				TotalConfidence += OwnersAndStates.Value.Confidence;

				if (TotalConfidence > MaxConfidence)
				{
					MaxConfidence = TotalConfidence;
					PotentialChosenOne = OwnersAndStates.Key;
				}
			}
		}
	}

	if (PotentialChosenOne != ChosenOne && PotentialChosenOne != nullptr)
	{
		if (ChosenOne != nullptr)
		{
			ChosenOne->ClientSetChosenOne(false);
		}

		ChosenOne = PotentialChosenOne;
		UE_LOG(LogMagicLeapSharedWorld, Display, TEXT("Selected PC (%s) as chosen one"), *PotentialChosenOne->GetName());
		PotentialChosenOne->ClientSetChosenOne(true);
	}
}

bool AMagicLeapSharedWorldGameMode::SendSharedWorldDataToClients()
{
	AMagicLeapSharedWorldGameState* SharedWorldGameState = GetSharedWorldGameState();
	if (SharedWorldGameState != nullptr)
	{
		SharedWorldGameState->SharedWorldData = SharedWorldData;

		UE_LOG(LogMagicLeapSharedWorld, Display, TEXT("Sending shared pins to clients"));
		for (const FGuid& PinID : SharedWorldData.PinIDs)
		{
			UE_LOG(LogMagicLeapSharedWorld, Display, TEXT("Sending pin id %s to clients."), *PinID.ToString());
		}

		return true;
	}

	UE_CLOG(SharedWorldGameState == nullptr, LogMagicLeapSharedWorld, Error, TEXT("Game state is not ready. Cannot send shared world data to clients."));

	return false;
}

void AMagicLeapSharedWorldGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Get local world data for each player
	for (auto It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		AMagicLeapSharedWorldPlayerController* PC = Cast<AMagicLeapSharedWorldPlayerController>(It->Get());
		if (PC != nullptr)
		{
			// if PC is not in the map, means its new.
			if (!PlayerToLocalPins.Contains(PC))
			{
				PlayerToLocalPins.Add(PC, TArray<FGuid>());
				PC->ClientMarkReadyForSendingLocalData();
			}

			if (PC->HasNewLocalWorldData())
			{
				const FMagicLeapSharedWorldLocalData& PlayerLocalWorldData = PC->GetLocalWorldData();

				// iterate through previous local data of this player to check if something needs to be deleted
				for (const FGuid& PinID : PlayerToLocalPins[PC])
				{
					const FMagicLeapSharedWorldPinData* PinDataPtr = PlayerLocalWorldData.LocalPins.FindByPredicate([&](const FMagicLeapSharedWorldPinData& PinData) {
						return PinData.PinID == PinID;
					});

					// if this player previously had a PinID which is no longer in the current local data, remove it from the PinToOwnersToStates map
					if (PinDataPtr == nullptr)
					{
						TMap<AMagicLeapSharedWorldPlayerController*, FMagicLeapARPinState>* OwnersToStates = PinToOwnersToStates.Find(PinID);
						if (OwnersToStates != nullptr)
						{
							OwnersToStates->Remove(PC);
							if (OwnersToStates->Num() == 0)
							{
								PinToOwnersToStates.Remove(PinID);
							}
						}
					}
				}

				PlayerToLocalPins[PC].Reset(PlayerLocalWorldData.LocalPins.Num());
				for (const FMagicLeapSharedWorldPinData& PinData : PlayerLocalWorldData.LocalPins)
				{
					PlayerToLocalPins[PC].Add(PinData.PinID);

					TMap<AMagicLeapSharedWorldPlayerController*, FMagicLeapARPinState>& OwnersToStates = PinToOwnersToStates.FindOrAdd(PinData.PinID);
					FMagicLeapARPinState& State = OwnersToStates.FindOrAdd(PC);
					// Update PinToOwnersToStates map with new pin states
					State = PinData.PinState;
				}

				// Reset the flag so we dont continue to consume the same data every frame
				PC->ResetHasNewLocalWorldData();

				OnNewLocalDataFromClients.Broadcast();
			}
		}
	}
}

void AMagicLeapSharedWorldGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	AMagicLeapSharedWorldPlayerController* PC = Cast<AMagicLeapSharedWorldPlayerController>(Exiting);
	if (PC != nullptr && PlayerToLocalPins.Contains(PC))
	{
		UE_LOG(LogMagicLeapSharedWorld, Display, TEXT("Removing PC (%s)"), *PC->GetName());

		for (const FGuid& PinID : PlayerToLocalPins[PC])
		{
			TMap<AMagicLeapSharedWorldPlayerController*, FMagicLeapARPinState>* OwnersToStates = PinToOwnersToStates.Find(PinID);
			if (OwnersToStates != nullptr)
			{
				OwnersToStates->Remove(PC);
				if (OwnersToStates->Num() == 0)
				{
					PinToOwnersToStates.Remove(PinID);
				}
			}
		}

		PlayerToLocalPins.Remove(PC);

		if (ChosenOne == PC)
		{
			ChosenOne = nullptr;
			SelectChosenOne();
		}
	}
}

AMagicLeapSharedWorldGameState* AMagicLeapSharedWorldGameMode::GetSharedWorldGameState() const
{
	return CastChecked<AMagicLeapSharedWorldGameState>(GameState);
}
