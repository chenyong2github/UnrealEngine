// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "Debug/DebugDrawService.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "WorldPartition/DataLayer/IDataLayerEditorModule.h"
#endif

static int32 GDrawDataLayers = 0;
static FAutoConsoleCommand CVarDrawDataLayers(
	TEXT("wp.Runtime.ToggleDrawDataLayers"),
	TEXT("Toggles debug display of active data layers."),
	FConsoleCommandDelegate::CreateLambda([] { GDrawDataLayers = !GDrawDataLayers; }));

UDataLayerSubsystem::UDataLayerSubsystem()
{}

bool UDataLayerSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	if (UWorld* WorldOuter = Cast<UWorld>(Outer))
	{
		return WorldOuter->GetWorldPartition() != nullptr;
	}

	return false;
}

void UDataLayerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	if (GEditor)
	{
		FModuleManager::LoadModuleChecked<IDataLayerEditorModule>("DataLayerEditor");
	}
#endif
}

void UDataLayerSubsystem::PostInitialize()
{
	Super::PostInitialize();

	if (GetWorld()->IsGameWorld())
	{
		// Initialize Dynamically loaded Data Layers state
		if (const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(GetWorld()))
		{
			WorldDataLayers->ForEachDataLayer([this](class UDataLayer* DataLayer)
			{
				if (DataLayer && DataLayer->IsDynamicallyLoaded())
				{
					SetDataLayerState(DataLayer, DataLayer->GetInitialState());
				}
				return true;
			});
		}
	}

	if (GetWorld()->IsGameWorld() && (GetWorld()->GetNetMode() != NM_DedicatedServer))
	{
		DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UDataLayerSubsystem::Draw));
	}
}

void UDataLayerSubsystem::Deinitialize()
{
	if (DrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawHandle);
		DrawHandle.Reset();
	}

	Super::Deinitialize();
}

UDataLayer* UDataLayerSubsystem::GetDataLayerFromLabel(const FName& InDataLayerLabel) const
{
	const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(GetWorld());
	return WorldDataLayers ? const_cast<UDataLayer*>(WorldDataLayers->GetDataLayerFromLabel(InDataLayerLabel)) : nullptr;
}

UDataLayer* UDataLayerSubsystem::GetDataLayerFromName(const FName& InDataLayerName) const
{
	const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(GetWorld());
	return WorldDataLayers ? const_cast<UDataLayer*>(WorldDataLayers->GetDataLayerFromName(InDataLayerName)) : nullptr;
}

// Deprecated start
void UDataLayerSubsystem::ActivateDataLayer(const FActorDataLayer& InDataLayer, bool bInActivate)
{
	// Keep deprecated behavior (!Activated == Unloaded)
	SetDataLayerState(GetDataLayerFromName(InDataLayer.Name), bInActivate ? EDataLayerState::Activated : EDataLayerState::Unloaded);
}

void UDataLayerSubsystem::ActivateDataLayerByLabel(const FName& InDataLayerLabel, bool bInActivate)
{
	// Keep deprecated behavior (!Activated == Unloaded)
	SetDataLayerState(GetDataLayerFromLabel(InDataLayerLabel), bInActivate ? EDataLayerState::Activated : EDataLayerState::Unloaded);
}

bool UDataLayerSubsystem::IsDataLayerActive(const FActorDataLayer& InDataLayer) const
{
	return GetDataLayerStateByName(InDataLayer.Name) == EDataLayerState::Activated;
}

bool UDataLayerSubsystem::IsDataLayerActiveByLabel(const FName& InDataLayerLabel) const
{
	return GetDataLayerStateByLabel(InDataLayerLabel) == EDataLayerState::Activated;
}
// Deprecation end

void UDataLayerSubsystem::SetDataLayerState(const UDataLayer* InDataLayer, EDataLayerState InState)
{
	if (!InDataLayer || !InDataLayer->IsDynamicallyLoaded())
	{
		return;
	}

	FName DataLayerName = InDataLayer->GetFName();
	EDataLayerState CurrentState = GetDataLayerStateByName(DataLayerName);
	if (CurrentState != InState)
	{
		LoadedDataLayerNames.Remove(DataLayerName);
		ActiveDataLayerNames.Remove(DataLayerName);
		
		if (InState == EDataLayerState::Loaded)
		{
			LoadedDataLayerNames.Add(DataLayerName);
		}
		else if(InState == EDataLayerState::Activated)
		{
			ActiveDataLayerNames.Add(DataLayerName);
		}
		
		// todo_ow: remove
		if (OnDataLayerActivationStateChanged.IsBound())
		{
			OnDataLayerActivationStateChanged.Broadcast(InDataLayer, InState == EDataLayerState::Activated);
		}

		if (OnDataLayerStateChanged.IsBound())
		{
			OnDataLayerStateChanged.Broadcast(InDataLayer, InState);
		}
	}
}

void UDataLayerSubsystem::SetDataLayerStateByName(const FName& InDataLayerName, EDataLayerState InState)
{
	SetDataLayerState(GetDataLayerFromName(InDataLayerName), InState);
}

void UDataLayerSubsystem::SetDataLayerState(const FActorDataLayer& InDataLayer, EDataLayerState InState)
{
	SetDataLayerState(GetDataLayerFromName(InDataLayer.Name), InState);
}

void UDataLayerSubsystem::SetDataLayerStateByLabel(const FName& InDataLayerLabel, EDataLayerState InState)
{
	SetDataLayerState(GetDataLayerFromLabel(InDataLayerLabel), InState);
}

EDataLayerState UDataLayerSubsystem::GetDataLayerState(const UDataLayer* InDataLayer) const
{
	if (!InDataLayer)
	{
		return EDataLayerState::Unloaded;
	}

	return GetDataLayerStateByName(InDataLayer->GetFName());
}

EDataLayerState UDataLayerSubsystem::GetDataLayerStateByName(const FName& InDataLayerName) const
{
	if (ActiveDataLayerNames.Contains(InDataLayerName))
	{
		check(!LoadedDataLayerNames.Contains(InDataLayerName));
		return EDataLayerState::Activated;
	}
	else if (LoadedDataLayerNames.Contains(InDataLayerName))
	{
		check(!ActiveDataLayerNames.Contains(InDataLayerName));
		return EDataLayerState::Loaded;
	}

	return EDataLayerState::Unloaded;
}

EDataLayerState UDataLayerSubsystem::GetDataLayerState(const FActorDataLayer& InDataLayer) const
{
	return GetDataLayerStateByName(InDataLayer.Name);
}

EDataLayerState UDataLayerSubsystem::GetDataLayerStateByLabel(const FName& InDataLayerLabel) const
{
	return GetDataLayerState(GetDataLayerFromLabel(InDataLayerLabel));
}

bool UDataLayerSubsystem::IsAnyDataLayerInState(const TArray<FName>& InDataLayerNames, EDataLayerState InState) const
{
	for (FName DataLayerName : InDataLayerNames)
	{
		if (GetDataLayerStateByName(DataLayerName) == InState)
		{
			return true;
		}
	}
	return false;
}

void UDataLayerSubsystem::Draw(UCanvas* Canvas, class APlayerController* PC)
{
	if (!GDrawDataLayers || !Canvas || !Canvas->SceneView)
	{
		return;
	}

	auto DrawText = [](UCanvas* Canvas, const FString& Text, UFont* Font, const FColor& Color, FVector2D& Pos)
	{
		float TextWidth, TextHeight;
		Canvas->StrLen(Font, Text, TextWidth, TextHeight);
		Canvas->SetDrawColor(Color);
		Canvas->DrawText(Font, Text, Pos.X, Pos.Y);
		Pos.Y += TextHeight + 1;
	};

	const FVector2D CanvasTopLeftPadding(10.f, 10.f);
	FVector2D Pos = CanvasTopLeftPadding;

	auto DrawLayerNames = [this, &Pos, DrawText, Canvas](const FString& Title, FColor Color, const TSet<FName>& LayerNames)
	{
		if (LayerNames.Num() > 0)
		{
			DrawText(Canvas, Title, GEngine->GetSmallFont(), FColor::Yellow, Pos);
			UFont* DataLayerFont = GEngine->GetTinyFont();
			for (const FName& DataLayerName : LayerNames)
			{
				if (UDataLayer* DataLayer = GetDataLayerFromName(DataLayerName))
				{
					const FString Text = DataLayer->GetDataLayerLabel().ToString();
					DrawText(Canvas, Text, DataLayerFont, FColor::White, Pos);
				}
			}
		}
	};

	DrawLayerNames(TEXT("Loaded Data Layers"), FColor::Green, LoadedDataLayerNames);
	DrawLayerNames(TEXT("Active Data Layers"), FColor::White, ActiveDataLayerNames);
}

FAutoConsoleCommand UDataLayerSubsystem::ToggleDataLayerActivation(
	TEXT("wp.Runtime.ToggleDataLayerActivation"),
	TEXT("Toggles DataLayers active state. Args [DataLayerLabels]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		for (const FString& Arg : Args)
		{
			FName DataLayerLabel = FName(Arg);
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && World->IsGameWorld())
				{
					UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>();
					if (const UDataLayer* DataLayer = DataLayerSubsystem ? DataLayerSubsystem->GetDataLayerFromLabel(DataLayerLabel) : nullptr)
					{
						DataLayerSubsystem->SetDataLayerState(DataLayer, DataLayerSubsystem->GetDataLayerState(DataLayer) == EDataLayerState::Activated ? EDataLayerState::Unloaded : EDataLayerState::Activated);
					}
				}
			}
		}
	})
);