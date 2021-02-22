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

void UDataLayerSubsystem::ActivateDataLayer(const FActorDataLayer& InDataLayer, bool bInActivate)
{
	ActivateDataLayerByName(InDataLayer.Name, bInActivate);
}

void UDataLayerSubsystem::ActivateDataLayer(const UDataLayer* InDataLayer, bool bInActivate)
{
	if (!InDataLayer || !InDataLayer->IsDynamicallyLoaded())
	{
		return;
	}

	FName DataLayerName = InDataLayer->GetFName();
	const bool bIsLayerActive = IsDataLayerActiveByName(DataLayerName);
	if (bIsLayerActive != bInActivate)
	{
		if (bInActivate)
		{
			ActiveDataLayerNames.Add(DataLayerName);
		}
		else
		{
			ActiveDataLayerNames.Remove(DataLayerName);
		}
		if (OnDataLayerActivationStateChanged.IsBound())
		{
			OnDataLayerActivationStateChanged.Broadcast(InDataLayer, bInActivate);
		}
	}
}

void UDataLayerSubsystem::ActivateDataLayerByName(const FName& InDataLayerName, bool bInActivate)
{
	ActivateDataLayer(GetDataLayerFromName(InDataLayerName), bInActivate);
}

void UDataLayerSubsystem::ActivateDataLayerByLabel(const FName& InDataLayerLabel, bool bInActivate)
{
	ActivateDataLayer(GetDataLayerFromLabel(InDataLayerLabel), bInActivate);
}

bool UDataLayerSubsystem::IsDataLayerActive(const FActorDataLayer& InDataLayer) const
{
	return IsDataLayerActiveByName(InDataLayer.Name);
}

bool UDataLayerSubsystem::IsDataLayerActive(const UDataLayer* InDataLayer) const
{
	return InDataLayer && IsDataLayerActiveByName(InDataLayer->GetFName());
}

bool UDataLayerSubsystem::IsDataLayerActiveByName(const FName& InDataLayerName) const
{
	return ActiveDataLayerNames.Contains(InDataLayerName);
}

bool UDataLayerSubsystem::IsDataLayerActiveByLabel(const FName& InDataLayerLabel) const
{
	return IsDataLayerActive(GetDataLayerFromLabel(InDataLayerLabel));
}

bool UDataLayerSubsystem::IsAnyDataLayerActive(const TArray<FName>& InDataLayerNames) const
{
	for (FName DataLayerName : InDataLayerNames)
	{
		if (IsDataLayerActiveByName(DataLayerName))
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

	if (ActiveDataLayerNames.Num() > 0)
	{
		const FVector2D CanvasTopLeftPadding(10.f, 10.f);
		FVector2D Pos = CanvasTopLeftPadding;
		FString Title(TEXT("Active Data Layers"));
		DrawText(Canvas, Title, GEngine->GetSmallFont(), FColor::Yellow, Pos);
		UFont* DataLayerFont = GEngine->GetTinyFont();
		for (const FName& DataLayerName : ActiveDataLayerNames)
		{
			if (UDataLayer* DataLayer = GetDataLayerFromName(DataLayerName))
			{
				const FString Text = DataLayer->GetDataLayerLabel().ToString();
				DrawText(Canvas, Text, DataLayerFont, FColor::White, Pos);
			}
		}
	}
}

FAutoConsoleCommand UDataLayerSubsystem::ToggleDataLayerActivation(
	TEXT("wp.Runtime.ToggleDataLayerActivation"),
	TEXT("Toggles a DataLayer. Args [DataLayerLabel]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() == 1)
		{
			FName DataLayerLabel = FName(Args[0]);
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && World->IsGameWorld())
				{
					UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>();
					if (const UDataLayer* DataLayer = DataLayerSubsystem ? DataLayerSubsystem->GetDataLayerFromLabel(DataLayerLabel) : nullptr)
					{
						DataLayerSubsystem->ActivateDataLayer(DataLayer, !DataLayerSubsystem->IsDataLayerActive(DataLayer));
					}
				}
			}
		}
	})
);