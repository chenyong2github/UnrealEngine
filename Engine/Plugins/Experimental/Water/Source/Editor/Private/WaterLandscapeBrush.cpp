// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterLandscapeBrush.h"
#include "CoreMinimal.h"
#include "LandscapeProxy.h"
#include "Landscape.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "WaterBodyActor.h"
#include "WaterBodyIslandActor.h"
#include "Algo/Transform.h"
#include "WaterMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "WaterSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "WaterMeshActor.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Editor.h"
#include "WaterEditorSubsystem.h"
#include "Algo/AnyOf.h"
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "WaterIconHelper.h"
#include "Components/BillboardComponent.h"
#include "Modules/ModuleManager.h"
#include "WaterModule.h"

#define LOCTEXT_NAMESPACE "WaterLandscapeBrush"

AWaterLandscapeBrush::AWaterLandscapeBrush(const FObjectInitializer& ObjectInitializer)
{
	SetAffectsHeightmap(true);

	ActorIcon = FWaterIconHelper::EnsureSpriteComponentCreated(this, TEXT("/Water/Icons/WaterLandscapeBrushSprite"));
}

void AWaterLandscapeBrush::AddActorInternal(AActor* Actor, const UWorld* ThisWorld, UObject* InCache, bool bTriggerEvent, bool bModify)
{
	if (IsActorAffectingLandscape(Actor) &&
		!Actor->HasAnyFlags(RF_Transient | RF_ClassDefaultObject | RF_ArchetypeObject) &&
		!Actor->IsPendingKillOrUnreachable() &&
		Actor->GetLevel() != nullptr &&
		!Actor->GetLevel()->bIsBeingRemoved &&
		ThisWorld == Actor->GetWorld())
	{
		if (bModify)
		{
			const bool bMarkPackageDirty = false;
			Modify(bMarkPackageDirty);
		}

		IWaterBrushActorInterface* WaterBrushActor = CastChecked<IWaterBrushActorInterface>(Actor);
		ActorsAffectingLandscape.Add(TWeakInterfacePtr<IWaterBrushActorInterface>(WaterBrushActor));

		if (InCache)
		{
			Cache.Add(TWeakObjectPtr<AActor>(Actor), InCache);
		}

		if (bTriggerEvent)
		{
			UpdateAffectedWeightmaps();
			OnActorsAffectingLandscapeChanged();
		}
	}
}

void AWaterLandscapeBrush::RemoveActorInternal(AActor* Actor)
{
	IWaterBrushActorInterface* WaterBrushActor = CastChecked<IWaterBrushActorInterface>(Actor);

	const bool bMarkPackageDirty = false;
	Modify(bMarkPackageDirty);
	int32 Index = ActorsAffectingLandscape.IndexOfByKey(TWeakInterfacePtr<IWaterBrushActorInterface>(WaterBrushActor));
	if (Index != INDEX_NONE)
	{
		ActorsAffectingLandscape.RemoveAt(Index);
		Cache.Remove(TWeakObjectPtr<AActor>(Actor));

		OnActorsAffectingLandscapeChanged();

		UpdateAffectedWeightmaps();
	}
}

void AWaterLandscapeBrush::BlueprintWaterBodiesChanged_Implementation()
{
	BlueprintWaterBodiesChanged_Native();
}

void AWaterLandscapeBrush::UpdateAffectedWeightmaps()
{
	AffectedWeightmapLayers.Empty();
	for (const TWeakInterfacePtr<IWaterBrushActorInterface>& WaterBrushActor : ActorsAffectingLandscape)
	{
		if (WaterBrushActor.IsValid())
		{
			for (const TPair<FName, FWaterBodyWeightmapSettings>& Pair : WaterBrushActor->GetLayerWeightmapSettings())
			{
				AffectedWeightmapLayers.AddUnique(Pair.Key);
			}
		}
	}
}

template<class T>
class FGetActorsOfType
{
public:
	void operator()(const AWaterLandscapeBrush* Brush, TSubclassOf<T> ActorClass, TArray<T*>& OutActors)
	{
		OutActors.Empty();
		for (const TWeakInterfacePtr<IWaterBrushActorInterface>& WaterBrushActor : Brush->ActorsAffectingLandscape)
		{
			T* Actor = Cast<T>(WaterBrushActor.GetObject());
			if (Actor && Actor->IsA(ActorClass))
			{
				OutActors.Add(Actor);
			}
		}
	}
};

void AWaterLandscapeBrush::UpdateActors(bool bInTriggerEvents)
{
	if (IsTemplate())
	{
		return;
	}

	const bool bMarkPackageDirty = false;
	Modify(bMarkPackageDirty);

	ClearActors();

	// Backup Cache
	TMap<TWeakObjectPtr<AActor>, UObject*> PreviousCache;
	FMemory::Memswap(&Cache, &PreviousCache, sizeof(Cache));

	if (UWorld* World = GetWorld())
	{
		for (FActorIterator It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (IWaterBrushActorInterface* WaterBrushActor = Cast<IWaterBrushActorInterface>(Actor))
			{
				UObject* const* FoundCache = PreviousCache.Find(TWeakObjectPtr<AActor>(Actor));
				const bool bTriggerEvent = false;
				const bool bModify = false;
				AddActorInternal(Actor, World, FoundCache != nullptr ? *FoundCache : nullptr, bTriggerEvent, bModify);
			}
		}
	}

	UpdateAffectedWeightmaps();

	if (bInTriggerEvents)
	{
		OnActorsAffectingLandscapeChanged();
	}
}

void AWaterLandscapeBrush::OnActorChanged(AActor* Actor, bool bWeightmapSettingsChanged, bool bRebuildWaterMesh)
{
	bool bAffectsLandscape = IsActorAffectingLandscape(Actor);
	IWaterBrushActorInterface* WaterBrushActor = CastChecked<IWaterBrushActorInterface>(Actor);
	int32 ActorIndex = ActorsAffectingLandscape.IndexOfByKey(TWeakInterfacePtr<IWaterBrushActorInterface>(WaterBrushActor));
	// if the actor went from affecting landscape to non-affecting landscape (and vice versa), update the brush
	bool bForceUpdateBrush = false;
	if (bAffectsLandscape != (ActorIndex != INDEX_NONE))
	{
		if (bAffectsLandscape)
		{
			AddActorInternal(Actor, GetWorld(), nullptr, /*bTriggerEvent = */true, /*bModify =*/true);
		}
		else
		{
			RemoveActorInternal(Actor);
		}

		// Force rebuild the mesh if a water body actor has been added or removed (islands don't affect the water mesh so it's not necessary for them): 
		bRebuildWaterMesh = WaterBrushActor->CanAffectWaterMesh();
		bForceUpdateBrush = true;
	}

	if (bWeightmapSettingsChanged)
	{
		UpdateAffectedWeightmaps();
	}

	BlueprintWaterBodyChanged(Actor);

	if (bAffectsLandscape || bForceUpdateBrush)
	{
		RequestLandscapeUpdate();
		MarkRenderTargetsDirty();
	}

	if (bRebuildWaterMesh)
	{
		if (UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld()))
		{
			WaterSubsystem->MarkAllWaterMeshesForRebuild();
		}
	}
}

void AWaterLandscapeBrush::MarkRenderTargetsDirty()
{
	bRenderTargetsDirty = true;
}

void AWaterLandscapeBrush::OnWaterBrushActorChanged(const IWaterBrushActorInterface::FWaterBrushActorChangedEventParams& InParams)
{
	AActor* Actor = CastChecked<AActor>(InParams.WaterBrushActor);
	OnActorChanged(Actor, /* bWeightmapSettingsChanged = */InParams.bWeightmapSettingsChanged, /* bRebuildWaterMesh = */InParams.WaterBrushActor->AffectsWaterMesh());
}

void AWaterLandscapeBrush::OnActorsAffectingLandscapeChanged()
{
	BlueprintWaterBodiesChanged();
	RequestLandscapeUpdate();
	if (UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld()))
	{
		WaterSubsystem->MarkAllWaterMeshesForRebuild();
	}

	MarkRenderTargetsDirty();
}

bool AWaterLandscapeBrush::IsActorAffectingLandscape(AActor* Actor) const
{
	IWaterBrushActorInterface* WaterBrushActor = Cast<IWaterBrushActorInterface>(Actor);
	return ((WaterBrushActor != nullptr) && WaterBrushActor->AffectsLandscape());
}

void AWaterLandscapeBrush::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_Transient))
	{
		OnWorldPostInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([this](UWorld* World, const UWorld::InitializationValues IVS)
		{
			if (World == GetWorld())
			{
				const bool bTriggerEvents = false;
				UpdateActors(bTriggerEvents);
			}
		});

		OnLevelAddedToWorldHandle = FWorldDelegates::LevelAddedToWorld.AddLambda([this](ULevel* Level, UWorld* World)
		{
			if ((World == GetWorld())
				&& (World->IsEditorWorld()
				&& (Level != nullptr)
				&& Algo::AnyOf(Level->Actors, [this](AActor* Actor) { return IsActorAffectingLandscape(Actor); })))
			{
				UpdateActors(!GIsEditorLoadingPackage);
			}
		});

		OnLevelRemovedFromWorldHandle = FWorldDelegates::LevelRemovedFromWorld.AddLambda([this](ULevel* Level, UWorld* World)
		{
			if ((World == GetWorld())
				&& (World->IsEditorWorld()
				&& (Level != nullptr)
				&& Algo::AnyOf(Level->Actors, [this](AActor* Actor) { return IsActorAffectingLandscape(Actor); })))
			{
				UpdateActors(!GIsEditorLoadingPackage);
			}
		});

		OnLevelActorAddedHandle = GEngine->OnLevelActorAdded().AddLambda([this](AActor* Actor)
		{
			const UWorld* ThisWorld = GetWorld();
			const bool bTriggerEvent = true;
			const bool bModify = true;
			AddActorInternal(Actor, ThisWorld, nullptr, bTriggerEvent, bModify);
		});

		OnLevelActorDeletedHandle = GEngine->OnLevelActorDeleted().AddLambda([this](AActor* Actor)
		{
			if (IsActorAffectingLandscape(Actor))
			{
				RemoveActorInternal(Actor);
			}
		});

		OnActorMovedHandle = GEngine->OnActorMoved().AddLambda([this](AActor* Actor)
		{
			if (IsActorAffectingLandscape(Actor))
			{
				OnActorChanged(Actor, /* bWeightmapSettingsChanged */ false, /* bRebuildWaterMesh */ true);
			}
		});

		IWaterBrushActorInterface::GetOnWaterBrushActorChangedEvent().AddUObject(this, &AWaterLandscapeBrush::OnWaterBrushActorChanged);
	}

	// If we are loading do not trigger events
	UpdateActors(!GIsEditorLoadingPackage);
}

void AWaterLandscapeBrush::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	UpdateActorIcon();
}

void AWaterLandscapeBrush::ClearActors()
{
	ActorsAffectingLandscape.Empty();
}

void AWaterLandscapeBrush::BeginDestroy()
{
	Super::BeginDestroy();

	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_Transient))
	{
		ClearActors();

		FWorldDelegates::OnPostWorldInitialization.Remove(OnWorldPostInitHandle);
		OnWorldPostInitHandle.Reset();

		FWorldDelegates::LevelAddedToWorld.Remove(OnLevelAddedToWorldHandle);
		OnLevelAddedToWorldHandle.Reset();

		FWorldDelegates::LevelRemovedFromWorld.Remove(OnLevelRemovedFromWorldHandle);
		OnLevelRemovedFromWorldHandle.Reset();

		GEngine->OnLevelActorAdded().Remove(OnLevelActorAddedHandle);
		OnLevelActorAddedHandle.Reset();

		GEngine->OnLevelActorDeleted().Remove(OnLevelActorDeletedHandle);
		OnLevelActorDeletedHandle.Reset();

		GEngine->OnActorMoved().Remove(OnActorMovedHandle);
		OnActorMovedHandle.Reset();

		IWaterBrushActorInterface::GetOnWaterBrushActorChangedEvent().RemoveAll(this);
	}
}

void AWaterLandscapeBrush::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	AWaterLandscapeBrush* This = CastChecked<AWaterLandscapeBrush>(InThis);
	Super::AddReferencedObjects(This, Collector);

	// TODO [jonathan.bard] : remove : probably not necessary since it's now a uproperty :
	for (TPair<TWeakObjectPtr<AActor>, UObject*>& Pair : This->Cache)
	{
		Collector.AddReferencedObject(Pair.Value);
	}
}

void AWaterLandscapeBrush::GetWaterBodies(TSubclassOf<AWaterBody> WaterBodyClass, TArray<AWaterBody*>& OutWaterBodies) const
{
	FGetActorsOfType<AWaterBody>()(this, WaterBodyClass, OutWaterBodies);
}

void AWaterLandscapeBrush::GetWaterBodyIslands(TSubclassOf<AWaterBodyIsland> WaterBodyIslandClass, TArray<AWaterBodyIsland*>& OutWaterBodyIslands) const
{
	FGetActorsOfType<AWaterBodyIsland>()(this, WaterBodyIslandClass, OutWaterBodyIslands);
}

void AWaterLandscapeBrush::GetActorsAffectingLandscape(TArray<TScriptInterface<IWaterBrushActorInterface>>& OutWaterBrushActors) const
{
	OutWaterBrushActors.Reserve(ActorsAffectingLandscape.Num());
	Algo::TransformIf(ActorsAffectingLandscape, OutWaterBrushActors,
		[](const TWeakInterfacePtr<IWaterBrushActorInterface>& WeakPtr) { return WeakPtr.IsValid(); },
		[](const TWeakInterfacePtr<IWaterBrushActorInterface>& WeakPtr) { return WeakPtr.ToScriptInterface(); });
}

void AWaterLandscapeBrush::BlueprintWaterBodyChanged_Implementation(AActor* Actor)
{
	BlueprintWaterBodyChanged_Native(Actor);
}

void AWaterLandscapeBrush::SetWaterBodyCache(AWaterBody* WaterBody, UObject* InCache)
{
	SetActorCache(WaterBody, InCache);
}

void AWaterLandscapeBrush::SetActorCache(AActor* InActor, UObject* InCache)
{
	if (!InCache)
	{
		return;
	}

	UObject*& Value = Cache.FindOrAdd(TWeakObjectPtr<AActor>(InActor));
	Value = InCache;
}


UObject* AWaterLandscapeBrush::GetWaterBodyCache(AWaterBody* WaterBody, TSubclassOf<UObject> CacheClass) const
{
	return GetActorCache(WaterBody, CacheClass);
}

UObject* AWaterLandscapeBrush::GetActorCache(AActor* InActor, TSubclassOf<UObject> CacheClass) const
{
	UObject* const* ValuePtr = Cache.Find(TWeakObjectPtr<AActor>(InActor));
	if (ValuePtr && (*ValuePtr) && (*ValuePtr)->IsA(*CacheClass))
	{
		return *ValuePtr;
	}
	return nullptr;
}

void AWaterLandscapeBrush::ClearWaterBodyCache(AWaterBody* WaterBody)
{
	ClearActorCache(WaterBody);
}

void AWaterLandscapeBrush::ClearActorCache(AActor* InActor)
{
	Cache.Remove(TWeakObjectPtr<AActor>(InActor));
}

void AWaterLandscapeBrush::BlueprintGetRenderTargets_Implementation(UTextureRenderTarget2D* InHeightRenderTarget, UTextureRenderTarget2D*& OutVelocityRenderTarget)
{
	BlueprintGetRenderTargets_Native(InHeightRenderTarget, OutVelocityRenderTarget);
}

void AWaterLandscapeBrush::SetTargetLandscape(ALandscape* InTargetLandscape)
{
	if (OwningLandscape != InTargetLandscape)
	{
		if (OwningLandscape)
		{
			OwningLandscape->RemoveBrush(this);
		}

		if (InTargetLandscape && InTargetLandscape->CanHaveLayersContent())
		{
			FName WaterLayerName = FName("Water");
			int32 ExistingWaterLayerIndex = InTargetLandscape->GetLayerIndex(WaterLayerName);
			if (ExistingWaterLayerIndex == INDEX_NONE)
			{
				ExistingWaterLayerIndex = InTargetLandscape->CreateLayer(WaterLayerName);
			}
			InTargetLandscape->AddBrushToLayer(ExistingWaterLayerIndex, this);
		}
	}

#if WITH_EDITOR
	UpdateActorIcon();
#endif // WITH_EDITOR
}

void AWaterLandscapeBrush::OnFullHeightmapRenderDone(UTextureRenderTarget2D* InHeightmapRenderTarget)
{
	if (bRenderTargetsDirty)
	{
		FScopedDurationTimeLogger DurationTimer(TEXT("Water Texture Update Time"));

		UTextureRenderTarget2D* VelocityRenderTarget = nullptr;
		BlueprintGetRenderTargets(InHeightmapRenderTarget, VelocityRenderTarget);

		UTexture2D* WaterVelocityTexture = nullptr;
		GEditor->GetEditorSubsystem<UWaterEditorSubsystem>()->UpdateWaterTextures(
			GetWorld(),
			VelocityRenderTarget,
			WaterVelocityTexture);

		if (WaterVelocityTexture)
		{
			BlueprintOnRenderTargetTexturesUpdated(WaterVelocityTexture);
		}

		bRenderTargetsDirty = false;
	}
}

void AWaterLandscapeBrush::SetOwningLandscape(ALandscape* InOwningLandscape)
{
	if (OwningLandscape != nullptr)
	{
		OwningLandscape->OnFullHeightmapRenderDoneDelegate().RemoveAll(this);
	}

	Super::SetOwningLandscape(InOwningLandscape);

	if (OwningLandscape != nullptr)
	{
		OwningLandscape->OnFullHeightmapRenderDoneDelegate().AddUObject(this, &AWaterLandscapeBrush::OnFullHeightmapRenderDone);
	}
}

void AWaterLandscapeBrush::GetRenderDependencies(TSet<UObject*>& OutDependencies)
{
	Super::GetRenderDependencies(OutDependencies);

	for (const TWeakInterfacePtr<IWaterBrushActorInterface>& WaterBrushActor : ActorsAffectingLandscape)
	{
		if (WaterBrushActor.IsValid())
		{
			WaterBrushActor->GetBrushRenderDependencies(OutDependencies);
		}
	}
}

void AWaterLandscapeBrush::ForceUpdate()
{
	OnActorsAffectingLandscapeChanged();
}


void AWaterLandscapeBrush::BlueprintOnRenderTargetTexturesUpdated_Implementation(UTexture2D* VelocityTexture)
{
	BlueprintOnRenderTargetTexturesUpdated_Native(VelocityTexture);
}

void AWaterLandscapeBrush::ForceWaterTextureUpdate()
{
	MarkRenderTargetsDirty();
}

#if WITH_EDITOR

AWaterLandscapeBrush::EWaterBrushStatus AWaterLandscapeBrush::CheckWaterBrushStatus()
{
	if (GetWorld() && !IsTemplate())
	{
		ALandscape* Landscape = GetOwningLandscape();
		if (Landscape == nullptr || !Landscape->CanHaveLayersContent())
		{
			return EWaterBrushStatus::MissingLandscapeWithEditLayers;
		}

		if (Landscape->GetBrushLayer(this) == INDEX_NONE)
		{
			return EWaterBrushStatus::MissingFromLandscapeEditLayers;
		}
	}

	return EWaterBrushStatus::Valid;
}

void AWaterLandscapeBrush::CheckForErrors()
{
	Super::CheckForErrors();

	switch (CheckWaterBrushStatus())
	{
	case EWaterBrushStatus::MissingLandscapeWithEditLayers:
		FMessageLog("MapCheck").Error()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_NonEditLayersLandscape", "The water brush requires a Landscape with Edit Layers enabled.")))
			->AddToken(FMapErrorToken::Create(TEXT("WaterBrushNonEditLayersLandscape")));
	case EWaterBrushStatus::MissingFromLandscapeEditLayers:
		FMessageLog("MapCheck").Error()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_MissingFromLandscapeEditLayers", "The water brush is missing from the owning landscape edit layers.")))
			->AddToken(FMapErrorToken::Create(TEXT("WaterBrushMissingFromLandscapeEditLayers")));
		break;
	}
}

void AWaterLandscapeBrush::UpdateActorIcon()
{
	if (ActorIcon && !bIsEditorPreviewActor)
	{
		UTexture2D* IconTexture = ActorIcon->Sprite;
		IWaterModuleInterface& WaterModule = FModuleManager::GetModuleChecked<IWaterModuleInterface>("Water");
		if (const IWaterEditorServices* WaterEditorServices = WaterModule.GetWaterEditorServices())
		{
			if (CheckWaterBrushStatus() != EWaterBrushStatus::Valid)
			{
				IconTexture = WaterEditorServices->GetErrorSprite();
			}
			else
			{
				IconTexture = WaterEditorServices->GetWaterActorSprite(GetClass());
			}
		}

		FWaterIconHelper::UpdateSpriteComponent(this, IconTexture);
	}
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
