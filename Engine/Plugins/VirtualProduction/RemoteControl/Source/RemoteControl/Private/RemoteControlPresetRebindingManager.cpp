// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlPresetRebindingManager.h"

#include "Algo/Transform.h"
#include "Components.h"
#include "Engine/Brush.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/Char.h"
#include "RemoteControlBinding.h"
#include "RemoteControlEntity.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ActorEditorUtils.h"
#endif

namespace RCPresetRebindingManager
{
	UWorld* GetCurrentWorld()
	{
		// Never use PIE world when searching for a new binding.
		UWorld* World = nullptr;

#if WITH_EDITOR
		if (GEditor)
		{
			World = GEditor->GetEditorWorldContext(false).World();
		}
#endif

		if (World)
		{
			return World;
		}

		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType == EWorldType::Game)
			{
				return World;
			}
		}
		
		return nullptr;
	}

	bool IsValidObjectForRebinding(UObject* InObject)
	{
		return InObject
			&& InObject->GetTypedOuter<UPackage>() != GetTransientPackage()
			&& InObject->GetWorld() == GetCurrentWorld();
	}

	bool IsValidActorForRebinding(AActor* InActor)
	{
		return IsValidObjectForRebinding(InActor) &&
#if WITH_EDITOR
            InActor->IsEditable() &&
            InActor->IsListedInSceneOutliner() &&
#endif
			!InActor->IsTemplate() &&
            InActor->GetClass() != ABrush::StaticClass() && // Workaround Brush being listed as visible in the scene outliner even though it's not.
            !InActor->HasAnyFlags(RF_Transient);
	}

	bool IsValidComponentForRebinding(UActorComponent* InComponent)
	{
		if (!IsValidObjectForRebinding(InComponent))
		{
			return false;
		}
		
		if (AActor* OuterActor = InComponent->GetOwner())
		{
			if (!IsValidActorForRebinding(OuterActor))
			{
				return false;
			}
		}

		return true;
	}

	TArray<UClass*> GetRelevantClassesForObject(const TArray<UClass*>& InClasses, UObject* InObject)
	{
		return InClasses.FilterByPredicate([&InObject](UClass* InClass) { return InClass && InObject->IsA(InClass); });
	};

	/**
	 * Returns a map of actors grouped by their relevant class.
	 * (Relevant class being the base class that supports a given entity)
	 */
	TMap<UClass*, TArray<UObject*>> GetLevelObjectsGroupedByClass(const TArray<UClass*>& RelevantClasses)
	{
		TMap<UClass*, TArray<UObject*>> ObjectMap;

		auto AddRelevantClassesForObject = [&ObjectMap, &RelevantClasses] (UObject* Object)
		{
			if (!Object
				|| (Object->IsA<AActor>() && !IsValidActorForRebinding(CastChecked<AActor>(Object)))
				|| (Object->IsA<UActorComponent>() && !IsValidComponentForRebinding(CastChecked<UActorComponent>(Object))))
			{
				return;
			}
			
			for (UClass* Class : GetRelevantClassesForObject(RelevantClasses, Object))
			{
				ObjectMap.FindOrAdd(Class).Add(Object);
			}
		};

		if (UWorld* World = GetCurrentWorld())
		{
			for (TActorIterator<AActor> It(World, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
			{
        		AddRelevantClassesForObject(*It);
			}
		}

		const EObjectFlags ExcludeFlags = RF_ClassDefaultObject | RF_ArchetypeObject | RF_Transient;
		for (TObjectIterator<UActorComponent> It(ExcludeFlags, true, EInternalObjectFlags::PendingKill); It; ++It)
		{
			AddRelevantClassesForObject(*It);
		}

		return ObjectMap;
	}

	TMap<UClass*, TMap<uint32, TSet<UObject*>>> CreateInitialBoundObjectMap(TConstArrayView<TSharedPtr<FRemoteControlEntity>> BoundEntities)
	{
		TMap<UClass*, TMap<uint32, TSet<UObject*>>> BoundObjectMap;
		for (const TSharedPtr<FRemoteControlEntity>& BoundEntity : BoundEntities)
		{
			if (UClass* SupportedClass = BoundEntity->GetSupportedBindingClass())
			{
				TMap<uint32, TSet<UObject*>>& BoundObjectsBySupportedOwnerClass = BoundObjectMap.FindOrAdd(SupportedClass);
				for (UObject* BoundObject : BoundEntity->GetBoundObjects())
				{
					BoundObjectsBySupportedOwnerClass.FindOrAdd(BoundEntity->GetUnderlyingEntityIdentifier()).Add(BoundObject);
				}
			}
		}
		return BoundObjectMap;
	}
}

UObject* FDefaultRebindingPolicy::FindMatch(const FFindBindingForEntityArgs& Args)
{
	UObject* const* Match = nullptr;
	
	if (const TSet<UObject*>* ObjectsBoundToEntity = Args.ExistingEntityMatches.Find(Args.Entity->GetUnderlyingEntityIdentifier()))
	{
		Match = Args.PotentialMatches.FindByPredicate([ObjectsBoundToEntity](UObject* InObject) { return !ObjectsBoundToEntity->Contains(InObject); });
	}
	else if (Args.PotentialMatches.Num())
	{
		Match = &Args.PotentialMatches[0];
	}

	return Match ? *Match : nullptr;
}

UObject* FNameBasedRebindingPolicy::FindMatch(const FFindBindingForEntityArgs& Args)
{
	UObject* const* Match = nullptr;
	
	// Do a slow search comparing object names with the name of the binding.
	if (Args.Entity->GetBindings().Num() && Args.Entity->GetBindings()[0].IsValid())
	{
		const TSet<UObject*>* ObjectsBoundToEntity = Args.ExistingEntityMatches.Find(Args.Entity->GetUnderlyingEntityIdentifier());

		// Strip digits from the end of the object name to increase chances of finding a match
		int32 LastCharIndex = Args.Entity->GetBindings()[0]->Name.FindLastCharByPredicate([](TCHAR InChar) { return FChar::IsAlpha(InChar); } );
		if (LastCharIndex != INDEX_NONE && LastCharIndex >0)
		{
			FStringView NameWithoutDigits = FStringView{ Args.Entity->GetBindings()[0]->Name };
			NameWithoutDigits.LeftInline(LastCharIndex);
			Match = Args.PotentialMatches.FindByPredicate(
	            [ObjectsBoundToEntity, &NameWithoutDigits](UObject* InObject)
	            {
					if (ObjectsBoundToEntity)
					{
						return !ObjectsBoundToEntity->Contains(InObject) && InObject->GetName().StartsWith(NameWithoutDigits.GetData());
					}

					return InObject->GetName().StartsWith(NameWithoutDigits.GetData());
	            });
		}
	}

	return Match ? *Match : FDefaultRebindingPolicy::FindMatch(Args);
}

void FRemoteControlPresetRebindingManager::Rebind(URemoteControlPreset* Preset)
{
	check(Preset);

	Context.Reset();

	ReinitializeBindings(Preset);

	TArray<TWeakPtr<FRemoteControlEntity>> WeakExposedEntities = Preset->GetExposedEntities<FRemoteControlEntity>();
	
	TArray<TSharedPtr<FRemoteControlEntity>> ExposedEntities;
	ExposedEntities.Reserve(WeakExposedEntities.Num());

	Algo::TransformIf(WeakExposedEntities, ExposedEntities,
		[](const TWeakPtr<FRemoteControlEntity>& InWeakEntity){ return InWeakEntity.IsValid(); },
		[](const TWeakPtr<FRemoteControlEntity>& InWeakEntity){ return InWeakEntity.Pin(); }
		);
	
	TArray<TSharedPtr<FRemoteControlEntity>> BoundEntities = ExposedEntities.FilterByPredicate([](const TSharedPtr<FRemoteControlEntity>& InEntity) { return InEntity->IsBound(); });
	TArray<TSharedPtr<FRemoteControlEntity>> UnboundProperties = ExposedEntities.FilterByPredicate([](const TSharedPtr<FRemoteControlEntity>& InEntity) { return !InEntity->IsBound(); });
	
	// Build a map of entities already bound in order to avoid the case where we bind an object to an exposed entity that 
	// was rebound in the ReinitializeBindings step.
	Context.BoundObjectsBySupportedOwnerClass = RCPresetRebindingManager::CreateInitialBoundObjectMap(BoundEntities);
	TMap<UClass*, TArray<TSharedPtr<FRemoteControlEntity>>> EntitiesGroupedBySupportedOwnerClass = GroupByEntitySupportedOwnerClass(UnboundProperties);
	
	// Fetch any relevant object from the level based on if their class is relevant for the set of entities we have to rebind.
	TArray<UClass*> EntitiesOwnerClasses;
	EntitiesGroupedBySupportedOwnerClass.GenerateKeyArray(EntitiesOwnerClasses);
	Context.ObjectsGroupedByRelevantClass = RCPresetRebindingManager::GetLevelObjectsGroupedByClass(EntitiesOwnerClasses);

	for (TPair<UClass*, TArray<TSharedPtr<FRemoteControlEntity>>>& Entry : EntitiesGroupedBySupportedOwnerClass)
	{
		RebindEntitiesForClass(Entry.Key, Entry.Value);
	}
}

TMap<UClass*, TArray<TSharedPtr<FRemoteControlEntity>>> FRemoteControlPresetRebindingManager::GroupByEntitySupportedOwnerClass(TConstArrayView<TSharedPtr<FRemoteControlEntity>> Entities) const
{
	TMap<UClass*, TArray<TSharedPtr<FRemoteControlEntity>>> GroupedEntities;
	for (const TSharedPtr<FRemoteControlEntity>& RCEntity : Entities)
	{
		GroupedEntities.FindOrAdd(RCEntity->GetSupportedBindingClass()).Add(RCEntity);
	}
	return GroupedEntities;
}

void FRemoteControlPresetRebindingManager::ReinitializeBindings(URemoteControlPreset* Preset) const
{
	if (ensure(Preset))
	{
		for (URemoteControlBinding* Binding : Preset->Bindings)
		{
			if (URemoteControlLevelDependantBinding* LevelDependantBinding = Cast<URemoteControlLevelDependantBinding>(Binding))
			{
				LevelDependantBinding->InitializeForNewLevel();
			}
		}
	}
}

void FRemoteControlPresetRebindingManager::RebindEntitiesForClass(UClass* Class, const TArray<TSharedPtr<FRemoteControlEntity>>& UnboundEntities)
{
	// Keep track of matched properties by keeping a record of FieldPaths hash -> Objects.
	// This is to make sure we don't bind the same property to the same object twice.
	TMap<uint32, TSet<UObject*>> MatchedProperties = Context.BoundObjectsBySupportedOwnerClass.FindOrAdd(Class);
	MatchedProperties.Reserve(MatchedProperties.Num() + UnboundEntities.Num());

	if (const TArray<UObject*>* ObjectsToConsider = Context.ObjectsGroupedByRelevantClass.Find(Class))
	{
		for (const TSharedPtr<FRemoteControlEntity>& RCEntity : UnboundEntities)
		{
			if (UObject* Match = FDefaultRebindingPolicy::FindMatch({RCEntity, *ObjectsToConsider, MatchedProperties}))
			{
				MatchedProperties.FindOrAdd(RCEntity->GetUnderlyingEntityIdentifier()).Add(Match);
				RCEntity->BindObject(Match);
			}
		}
	}
}
