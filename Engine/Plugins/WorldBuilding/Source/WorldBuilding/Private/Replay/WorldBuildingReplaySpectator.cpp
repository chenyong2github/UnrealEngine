// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldBuildingReplaySpectator.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldBuildingReplaySpectatorHUD.h"
#include "Engine/DemoNetDriver.h"
#include "GameFramework/PlayerInput.h"

AWorldBuildingReplaySpectator::AWorldBuildingReplaySpectator(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

void AWorldBuildingReplaySpectator::GetPlayerViewPoint(FVector& out_Location, FRotator& out_Rotation) const
{
	if (AWorldBuildingReplaySpectatorHUD* HUD = Cast<AWorldBuildingReplaySpectatorHUD>(MyHUD))
	{
		if (UWorldPartition* WorldPartition = GetWorld()->GetWorldPartition())
		{
			const TArray<FWorldPartitionStreamingSource>& StreamingSources = WorldPartition->GetStreamingSources();
			FString SelectedSourceName = HUD->GetSelectedViewPoint();
			int32 Index = StreamingSources.IndexOfByPredicate([SelectedSourceName](const FWorldPartitionStreamingSource& StreamingSource)
				{
					return SelectedSourceName == StreamingSource.Name.ToString();
				});

			if (Index != INDEX_NONE)
			{
				out_Location = WorldPartition->GetInstanceTransform().TransformPosition(StreamingSources[Index].Location);
				out_Rotation = WorldPartition->GetInstanceTransform().TransformRotation(StreamingSources[Index].Rotation.Quaternion()).Rotator();
				return;
			}
		}
	}

	// FREE CAM
	return Super::GetPlayerViewPoint(out_Location, out_Rotation);
}

void AWorldBuildingReplaySpectator::SpawnDefaultHUD()
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = GetInstigator();
	SpawnParams.ObjectFlags |= RF_Transient;	// We never want to save HUDs into a map
	MyHUD = GetWorld()->SpawnActor<AWorldBuildingReplaySpectatorHUD>(HUDClass ? HUDClass.Get() : AWorldBuildingReplaySpectatorHUD::StaticClass(), SpawnParams);
}

void AWorldBuildingReplaySpectator::TickActor(float DeltaSeconds, ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	Super::TickActor(DeltaSeconds, TickType, ThisTickFunction);

	if (AWorldBuildingReplaySpectatorHUD* HUD = Cast<AWorldBuildingReplaySpectatorHUD>(MyHUD))
	{
		if (UWorldPartition* WorldPartition = GetWorld()->GetWorldPartition())
		{
			const TArray<FWorldPartitionStreamingSource>& StreamingSources = WorldPartition->GetStreamingSources();
			TArray<FString> ViewPointNames;
			ViewPointNames.Reserve(StreamingSources.Num() + 1);
			Algo::Transform(StreamingSources, ViewPointNames, [](const FWorldPartitionStreamingSource& StreamingSource) { return StreamingSource.Name.ToString(); });
			ViewPointNames.Add(FString("FREE CAM"));
			HUD->SetAvailableViewPoints(ViewPointNames);
		}
	}
}

void AWorldBuildingReplaySpectator::SetupInputComponent()
{
	Super::SetupInputComponent();

	static bool bBindingsAdded = false;
	if (!bBindingsAdded)
	{
		bBindingsAdded = true;

		UPlayerInput::AddEngineDefinedActionMapping(FInputActionKeyMapping("ReplaySpectator_NextViewPoint", EKeys::Up));
		UPlayerInput::AddEngineDefinedActionMapping(FInputActionKeyMapping("ReplaySpectator_PreviousViewPoint", EKeys::Down));
	}

	InputComponent->BindAction("ReplaySpectator_NextViewPoint", IE_Pressed, this, &AWorldBuildingReplaySpectator::OnNextViewPoint);
	InputComponent->BindAction("ReplaySpectator_PreviousViewPoint", IE_Pressed, this, &AWorldBuildingReplaySpectator::OnPreviousViewPoint);
}

void AWorldBuildingReplaySpectator::OnNextViewPoint()
{
	if (AWorldBuildingReplaySpectatorHUD* HUD = Cast<AWorldBuildingReplaySpectatorHUD>(MyHUD))
	{
		HUD->NextViewPoint();
	}
}

void AWorldBuildingReplaySpectator::OnPreviousViewPoint()
{
	if (AWorldBuildingReplaySpectatorHUD* HUD = Cast<AWorldBuildingReplaySpectatorHUD>(MyHUD))
	{
		HUD->PreviousViewPoint();
	}
}
