// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlPresetRebindingManager.h"

#include "Algo/AllOf.h"
#include "Algo/Transform.h"
#include "Components/ActorComponent.h"
#include "Engine/Brush.h"
#include "Engine/Classes/Components/ActorComponent.h"
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

static TAutoConsoleVariable<int32> CVarRemoteControlRebindingUseLegacyAlgo(TEXT("RemoteControl.UseLegacyRebinding"), 0, TEXT("Whether to revert to the old algorithm when doing a Rebind All."));

namespace RCPresetRebindingManager
{

	bool IsValidObjectForRebinding(UObject* InObject, UWorld* PresetWorld)
	{
		return InObject
			&& InObject->GetTypedOuter<UPackage>() != GetTransientPackage()
			&& InObject->GetWorld() == PresetWorld;
	}

	bool IsValidActorForRebinding(AActor* InActor, UWorld* PresetWorld)
	{
		return IsValidObjectForRebinding(InActor, PresetWorld) &&
#if WITH_EDITOR
            InActor->IsEditable() &&
            InActor->IsListedInSceneOutliner() &&
#endif
			!InActor->IsTemplate() &&
            InActor->GetClass() != ABrush::StaticClass() && // Workaround Brush being listed as visible in the scene outliner even though it's not.
            !InActor->HasAnyFlags(RF_Transient);
	}

	bool IsValidComponentForRebinding(UActorComponent* InComponent, UWorld* PresetWorld)
	{
		if (!IsValidObjectForRebinding(InComponent, PresetWorld))
		{
			return false;
		}
		
		if (AActor* OuterActor = InComponent->GetOwner())
		{
			if (!IsValidActorForRebinding(OuterActor, PresetWorld))
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
	TMap<UClass*, TArray<UObject*>> GetLevelObjectsGroupedByClass(const TArray<UClass*>& RelevantClasses, UWorld* PresetWorld)
	{
		TMap<UClass*, TArray<UObject*>> ObjectMap;

		auto AddRelevantClassesForObject = [&ObjectMap, &RelevantClasses, PresetWorld] (UObject* Object)
		{
			if (!Object
				|| (Object->IsA<AActor>() && !IsValidActorForRebinding(CastChecked<AActor>(Object), PresetWorld))
				|| (Object->IsA<UActorComponent>() && !IsValidComponentForRebinding(CastChecked<UActorComponent>(Object), PresetWorld)))
			{
				return;
			}
			
			for (UClass* Class : GetRelevantClassesForObject(RelevantClasses, Object))
			{
				ObjectMap.FindOrAdd(Class).Add(Object);
			}
		};

		if (PresetWorld)
		{
			for (TActorIterator<AActor> It(PresetWorld, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
			{
        		AddRelevantClassesForObject(*It);
			}
		}

		const EObjectFlags ExcludeFlags = RF_ClassDefaultObject | RF_ArchetypeObject | RF_Transient;
		for (TObjectIterator<UActorComponent> It(ExcludeFlags, true, EInternalObjectFlags::Garbage); It; ++It)
		{
			AddRelevantClassesForObject(*It);
		}

		return ObjectMap;
	}

	bool GetActorsOfClass(UWorld* PresetWorld, UClass* InTargetClass, TArray<AActor*>& OutActors)
	{
		auto IsObjectOfClass = [InTargetClass, PresetWorld](UObject* Object)
		{
			return Object
				&& IsValidActorForRebinding(CastChecked<AActor>(Object), PresetWorld)
				&& Object->IsA(InTargetClass);
		};

		if (PresetWorld)
		{
			for (TActorIterator<AActor> It(PresetWorld, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
			{
				if (IsObjectOfClass(*It))
				{
					OutActors.Add(*It);
				}
			}
		}

		return OutActors.Num() != 0;
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
	if (CVarRemoteControlRebindingUseLegacyAlgo.GetValueOnAnyThread() == 1)
	{
		Rebind_Legacy(Preset);
	}
	else
	{
		Rebind_NewAlgo(Preset);
	}
}
void FRemoteControlPresetRebindingManager::Rebind_Legacy(URemoteControlPreset* Preset)
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
	constexpr bool bAllowPIE = false;
	Context.ObjectsGroupedByRelevantClass = RCPresetRebindingManager::GetLevelObjectsGroupedByClass(EntitiesOwnerClasses, Preset->GetWorld(bAllowPIE));

	for (TPair<UClass*, TArray<TSharedPtr<FRemoteControlEntity>>>& Entry : EntitiesGroupedBySupportedOwnerClass)
	{
		if (Entry.Key->IsChildOf<AActor>())
		{
			RebindEntitiesForActorClass_Legacy(Preset, Entry.Key, Entry.Value);
		}
	}

	for (TPair<UClass*, TArray<TSharedPtr<FRemoteControlEntity>>>& Entry : EntitiesGroupedBySupportedOwnerClass)
	{
		// Do an additional pass in case the bindings did not have the necessary info to use the previous rebinding process. 
		RebindEntitiesForClass_Legacy(Entry.Key, Entry.Value);
	}
}


void FRemoteControlPresetRebindingManager::Rebind_NewAlgo(URemoteControlPreset* Preset)
{
	check(Preset);

	// Separate bindings based on if they are bound or not.
	TArray<URemoteControlLevelDependantBinding*> ValidBindings;
	TArray<URemoteControlLevelDependantBinding*> InvalidBindings;
	
	auto ClassifyBindings = [&ValidBindings, &InvalidBindings, Preset] ()
	{
		ValidBindings.Reset();
		InvalidBindings.Reset();

		for (URemoteControlBinding* Binding : Preset->Bindings)
		{
			if (URemoteControlLevelDependantBinding* LevelDependantBinding = Cast<URemoteControlLevelDependantBinding>(Binding))
			{
				if (LevelDependantBinding->Resolve())
				{
					ValidBindings.Add(LevelDependantBinding);
				}
				else
				{
					InvalidBindings.Add(LevelDependantBinding);
				}
			}
		}
	};

	ClassifyBindings();
	const bool bAllContextsEmpty = Algo::AllOf(InvalidBindings, [](URemoteControlLevelDependantBinding* Binding) { return Binding->BindingContext.IsEmpty(); });
	if (bAllContextsEmpty)
	{
		Rebind_Legacy(Preset);
		return;
	}

	// First, try to 'rebind for all properties' bindings that are already bound.
	// to make sure that we don't accidentally rebind a property to a different actor
	for (URemoteControlLevelDependantBinding* ValidBinding : ValidBindings)
	{
		UObject* ResolvedObject = ValidBinding->Resolve();
		if (AActor* ResolvedActor = Cast<AActor>(ResolvedObject))
		{
			RebindAllEntitiesUnderSameActor(Preset, ValidBinding, ResolvedActor);
		}
		else
		{
			RebindAllEntitiesUnderSameActor(Preset, ValidBinding, ResolvedObject->GetTypedOuter<AActor>());
		}
	}

	// Reclassify bindings after we rebound them.
	ClassifyBindings();

	// Create a list of what objects and components are already bound.
	TSet<TPair<AActor*, UObject*>> BoundObjects;
	for (URemoteControlLevelDependantBinding* ValidBinding : ValidBindings)
	{
		const bool bTargetingActor = !ValidBinding->BindingContext.OwnerActorName.IsNone() && ValidBinding->BindingContext.ComponentName.IsNone();
		const bool bTargetingComponent = !ValidBinding->BindingContext.OwnerActorName.IsNone() && !ValidBinding->BindingContext.ComponentName.IsNone();

		if (bTargetingActor)
		{
			BoundObjects.Add(TPair<AActor*, UObject*>{ Cast<AActor>(ValidBinding->Resolve()), nullptr });
		}
		else if (bTargetingComponent)
		{
			UObject* Object = ValidBinding->Resolve();
			AActor* Actor = Object->GetTypedOuter<AActor>();
			BoundObjects.Add(TPair<AActor*, UObject*>{ Actor, Object });
		}
	}

	auto FindByName = [](const TArray<AActor*>& PotentialMatches, FName TargetName) -> UObject*
	{
		for (UObject* Object : PotentialMatches)
		{
			// Attempt finding by name.
			if (Object->GetFName() == TargetName)
			{
				return Object;
			}
		}
		return nullptr;
	};

	auto TryRebindPair = [&BoundObjects](URemoteControlBinding* InBindingToRebind, AActor* InActor, UObject* InActorComponent)
	{
		TPair<AActor*, UObject*> Pair = TPair<AActor*, UObject*> { InActor, InActorComponent };
		if (!BoundObjects.Contains(Pair))
		{
			BoundObjects.Add(Pair);
			InBindingToRebind->Modify();
			if (InActorComponent)
			{
				InBindingToRebind->SetBoundObject(InActorComponent);
			}
			else
			{
				InBindingToRebind->SetBoundObject(InActor);
			}
			return true;
		}
		return false;
	};

	auto GetComponentBasedOnName = [](URemoteControlLevelDependantBinding* InBinding, UObject* InObject) -> UObject*
	{
		FName InitialComponentName = InBinding->BindingContext.ComponentName;
		UObject* TargetComponent = FindObject<UObject>(InObject, *InitialComponentName.ToString());
		if (TargetComponent && TargetComponent->GetClass()->IsChildOf(InBinding->BindingContext.SupportedClass.LoadSynchronous()))
		{
			return TargetComponent;
		}
		return nullptr;
	};

	auto RebindComponentBasedOnClass = [TryRebindPair](URemoteControlLevelDependantBinding* InBinding, AActor* InActorMatch)
	{
		if (UClass* SupportedClass = InBinding->BindingContext.SupportedClass.LoadSynchronous())
		{
			if (SupportedClass->IsChildOf(UActorComponent::StaticClass()))
			{
				TArray<UActorComponent*> Components;
				Cast<AActor>(InActorMatch)->GetComponents(InBinding->BindingContext.SupportedClass.LoadSynchronous(), Components);
				
				for (UActorComponent* Component : Components)
				{
					if (TryRebindPair(InBinding, Component->GetOwner(), Component))
					{
						return true;
					}
				}
			}
		}
		return false;
	};

	auto RebindUsingActorName = [FindByName, &BoundObjects, TryRebindPair](URemoteControlLevelDependantBinding* BindingToRebind, const TArray<AActor*>& ObjectsWithSupportedClass)
	{
		if (UObject* Match = FindByName(ObjectsWithSupportedClass, BindingToRebind->BindingContext.OwnerActorName))
		{
			return TryRebindPair(BindingToRebind, Cast<AActor>(Match), nullptr);
		}
		return false;
	};

	auto RebindUsingClass = [FindByName, &BoundObjects, TryRebindPair](URemoteControlLevelDependantBinding* BindingToRebind, const TArray<AActor*>& ObjectsWithSupportedClass)
	{
		for (UObject* PotentialMatch : ObjectsWithSupportedClass)
		{
			if (TryRebindPair(BindingToRebind, Cast<AActor>(PotentialMatch), nullptr))
			{
				return true;
			}
		}
		return false;
	};

	auto TryRebindComponent = [GetComponentBasedOnName, TryRebindPair, RebindComponentBasedOnClass](URemoteControlLevelDependantBinding* BindingToRebind, UObject* PotentialMatch)
	{
		if (UObject* TargetComponent = GetComponentBasedOnName(BindingToRebind, PotentialMatch))
		{
			return TryRebindPair(BindingToRebind, TargetComponent->GetTypedOuter<AActor>(), TargetComponent);
		}
		else
		{
			return RebindComponentBasedOnClass(BindingToRebind, Cast<AActor>(PotentialMatch));
		}
	};

	// Core of the rebinding algo
	for (URemoteControlLevelDependantBinding* InvalidBinding : InvalidBindings)
	{
		if (UClass* OwnerClass = InvalidBinding->BindingContext.OwnerActorClass.LoadSynchronous())
		{
			const bool bRebindingComponent = !InvalidBinding->BindingContext.ComponentName.IsNone();
			TArray<AActor*> ObjectsWithSupportedClass;
			constexpr bool bAllowPIE = false;
			if (!RCPresetRebindingManager::GetActorsOfClass(Preset->GetWorld(bAllowPIE), OwnerClass, ObjectsWithSupportedClass))
			{
				continue;
			}

			if (bRebindingComponent)
			{
				if (UObject* Match = FindByName(ObjectsWithSupportedClass, InvalidBinding->BindingContext.OwnerActorName))
				{
					// Found an owner object with matching name. Try to find a component under it with a matching name.
					TryRebindComponent(InvalidBinding, Match);
				}
				else
				{
					// Could not find an actor with the same name as the initial binding, rely only on class instead.
					for (UObject* Object : ObjectsWithSupportedClass)
					{
						if (TryRebindComponent(InvalidBinding, Object))
						{
							break;
						}
					}
				}
			}
			else
			{
				if (!RebindUsingActorName(InvalidBinding, ObjectsWithSupportedClass))
				{
					RebindUsingClass(InvalidBinding, ObjectsWithSupportedClass);
				}
			}
		}
	}

	Preset->RemoveUnusedBindings();
}


TArray<FGuid> FRemoteControlPresetRebindingManager::RebindAllEntitiesUnderSameActor(URemoteControlPreset* Preset, URemoteControlBinding* InitialBinding, AActor* NewActor)
{
	check(Preset && InitialBinding && NewActor);

	TArray<FGuid> ReboundEntityIds;

	URemoteControlLevelDependantBinding* InitialLevelDependantBinding = Cast<URemoteControlLevelDependantBinding>(InitialBinding);
	if (!InitialLevelDependantBinding)
	{
		return ReboundEntityIds;
	}

	TArray<URemoteControlLevelDependantBinding*> BindingsToRebind;

	// Find bindings with the same actor as the original binding
	for (URemoteControlBinding* Binding : Preset->Bindings)
	{
		if (URemoteControlLevelDependantBinding* LevelDependantBinding = Cast<URemoteControlLevelDependantBinding>(Binding))
		{
			// Todo: Make this more robust by comparing full actor paths instead of names.
			if (LevelDependantBinding->BindingContext.OwnerActorClass == InitialLevelDependantBinding->BindingContext.OwnerActorClass
				&& LevelDependantBinding->BindingContext.OwnerActorName == InitialLevelDependantBinding->BindingContext.OwnerActorName)
			{
				BindingsToRebind.Add(LevelDependantBinding);
			}
		}
	}

	// Gather entities to rebind
	TArray<TSharedPtr<FRemoteControlEntity>> EntitiesToRebind;
	for (const TWeakPtr<FRemoteControlEntity>& WeakEntity : Preset->GetExposedEntities<FRemoteControlEntity>())
	{
		TSharedPtr<FRemoteControlEntity> RCEntity = WeakEntity.Pin();
		if (RCEntity && RCEntity->GetBindings().Num())
		{
			if (BindingsToRebind.Contains(RCEntity->GetBindings()[0].Get()))
			{
				EntitiesToRebind.Add(RCEntity);
			}
		}
	}

	ReboundEntityIds.Reserve(EntitiesToRebind.Num());
	Algo::Transform(EntitiesToRebind, ReboundEntityIds, [](const TSharedPtr<FRemoteControlEntity>& RCEntityPtr) { return RCEntityPtr->GetId();  });

	// Find the new binding target, then rebind.
	for (const TSharedPtr<FRemoteControlEntity>& EntityToRebind : EntitiesToRebind)
	{
		URemoteControlLevelDependantBinding* EntityBinding = Cast<URemoteControlLevelDependantBinding>(EntityToRebind->GetBindings()[0].Get());

		UObject* RebindTarget = NewActor;

		// If the binding is targeting a component rather than an actor
		if (!EntityBinding->BindingContext.ComponentName.IsNone())
		{
			if (UObject* ResolvedComponent = StaticFindObject(UObject::StaticClass(), NewActor, *EntityBinding->Name, false))
			{
				RebindTarget = ResolvedComponent;
			}
		}

		EntityToRebind->BindObject(RebindTarget);

		if (Preset->GetExposedEntityType(EntityToRebind->GetId()) == FRemoteControlProperty::StaticStruct())
		{
			StaticCastSharedPtr<FRemoteControlProperty>(EntityToRebind)->EnableEditCondition();
		}
	}

	Preset->RemoveUnusedBindings();

	return ReboundEntityIds;
}

TArray<FGuid> FRemoteControlPresetRebindingManager::RebindAllEntitiesUnderSameActor_Legacy(URemoteControlPreset* Preset, const TSharedPtr<FRemoteControlEntity>& Entity, AActor* NewActor)
{
	check(Preset && Entity && NewActor);

	TArray<FGuid> ReboundEntities;

	TMap<FName, TSet<URemoteControlBinding*>> ActorsToBindings;
	ActorsToBindings.Reserve(Preset->Bindings.Num());

	auto GetBindingLastLevel = [](URemoteControlBinding* Binding) -> FSoftObjectPath
	{
		FSoftObjectPath LevelPath;
		if (Binding && Binding->IsA<URemoteControlLevelDependantBinding>())
		{
			LevelPath = CastChecked<URemoteControlLevelDependantBinding>(Binding)->LevelWithLastSuccessfulResolve.ToSoftObjectPath();
		}
		return LevelPath;
	};

	auto GetBindingActorName = [](URemoteControlBinding* Binding) -> FName
	{
		FName ActorName;
		if (Binding && Binding->IsA<URemoteControlLevelDependantBinding>())
		{
			URemoteControlLevelDependantBinding* LDBinding = CastChecked<URemoteControlLevelDependantBinding>(Binding);
			FString Path = LDBinding->BoundObjectMapByPath.FindRef(LDBinding->LevelWithLastSuccessfulResolve.ToSoftObjectPath()).ToString();
			static const TCHAR* PersistentLevel = TEXT("PersistentLevel.");
			const int32 PersistentLevelStringLength = 16;

			int32 Position = Path.Find(PersistentLevel);

			if (Position != INDEX_NONE)
			{
				int32 DotPosition = Path.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromStart, Position + PersistentLevelStringLength);
				if (DotPosition != INDEX_NONE)
				{
					ActorName = *Path.Mid(Position + PersistentLevelStringLength, DotPosition - (Position + PersistentLevelStringLength));
				}
			}
		}

		return ActorName;
	};


	FName InitialBindingActorName;
	FSoftObjectPath InitialBindingLastLevel;

	const TArray<TWeakObjectPtr<URemoteControlBinding>>& EntityBindings = Entity->GetBindings();
	if (EntityBindings.Num())
	{
		InitialBindingLastLevel = GetBindingLastLevel(EntityBindings[0].Get());
		InitialBindingActorName = GetBindingActorName(EntityBindings[0].Get());
	}

	TMap<URemoteControlBinding*, FString> BindingToComponentName;

	// Find bindings with the same actor as the original binding
	for (URemoteControlBinding* Binding : Preset->Bindings)
	{
		FSoftObjectPath BindingItLastLevel = GetBindingLastLevel(Binding);
		FName BindingItActorName = GetBindingActorName(Binding);

		// To add a binding to the list of bindings to rebind, make sure it had the same level and actor as the initial binding
		if (BindingItActorName == InitialBindingActorName && BindingItLastLevel == InitialBindingLastLevel)
		{
			BindingToComponentName.Add(Binding, BindingItActorName.ToString());
		}
	}

	// Gather entities to rebind
	TArray<TSharedPtr<FRemoteControlEntity>> EntitiesToRebind;
	for (const TWeakPtr<FRemoteControlEntity>& WeakEntity : Preset->GetExposedEntities<FRemoteControlEntity>())
	{
		TSharedPtr<FRemoteControlEntity> RCEntity = WeakEntity.Pin();
		if (RCEntity && RCEntity->GetBindings().Num())
		{
			if (BindingToComponentName.Contains(RCEntity->GetBindings()[0].Get()))
			{
				EntitiesToRebind.Add(RCEntity);
			}
		}
	}

	// Find the new binding target, then rebind.
	for (const TSharedPtr<FRemoteControlEntity>& EntityToRebind : EntitiesToRebind)
	{
		URemoteControlBinding* EntityBinding = EntityToRebind->GetBindings()[0].Get();
		FString ActorName = BindingToComponentName.FindChecked(EntityBinding);

		UObject* RebindTarget = NewActor;

		// If the binding is targetting a component rather than an actor
		if (ActorName != EntityBinding->Name)
		{
			if (UObject* ResolvedComponent = StaticFindObject(UObject::StaticClass(), NewActor, *EntityBinding->Name, false))
			{
				RebindTarget = ResolvedComponent;
			}
		}

		EntityToRebind->BindObject(RebindTarget);
		ReboundEntities.Add(EntityToRebind->GetId());

		if (Preset->GetExposedEntityType(EntityToRebind->GetId()) == FRemoteControlProperty::StaticStruct())
		{
			StaticCastSharedPtr<FRemoteControlProperty>(EntityToRebind)->EnableEditCondition();
		}
	}

	Preset->RemoveUnusedBindings();

	return ReboundEntities;
}

TArray<FGuid> FRemoteControlPresetRebindingManager::RebindAllEntitiesUnderSameActor(URemoteControlPreset* Preset, const TSharedPtr<FRemoteControlEntity>& Entity, AActor* NewActor, bool bUseRebindingContext)
{
	check(Preset && Entity && NewActor);
	TArray<FGuid> ReboundEntityIds;

	if (Entity->GetBindings().Num())
	{
		if (bUseRebindingContext)
		{
			ReboundEntityIds = RebindAllEntitiesUnderSameActor(Preset, Entity->GetBindings()[0].Get(), NewActor);
		}
		else
		{
			ReboundEntityIds = RebindAllEntitiesUnderSameActor_Legacy(Preset, Entity, NewActor);
		}
	}

	return ReboundEntityIds;
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

void FRemoteControlPresetRebindingManager::RebindEntitiesForClass_Legacy(UClass* Class, const TArray<TSharedPtr<FRemoteControlEntity>>& UnboundEntities)
{
	// Keep track of matched properties by keeping a record of FieldPaths hash -> Objects.
	// This is to make sure we don't bind the same property to the same object twice.
	TMap<uint32, TSet<UObject*>> MatchedProperties = Context.BoundObjectsBySupportedOwnerClass.FindOrAdd(Class);
	MatchedProperties.Reserve(MatchedProperties.Num() + UnboundEntities.Num());

	if (const TArray<UObject*>* ObjectsToConsider = Context.ObjectsGroupedByRelevantClass.Find(Class))
	{
		for (const TSharedPtr<FRemoteControlEntity>& RCEntity : UnboundEntities)
		{
			if (!RCEntity->IsBound())
			{
				if (UObject* Match = FDefaultRebindingPolicy::FindMatch({RCEntity, *ObjectsToConsider, MatchedProperties}))
				{
					MatchedProperties.FindOrAdd(RCEntity->GetUnderlyingEntityIdentifier()).Add(Match);
					RCEntity->BindObject(Match);
				}
			}
		}
	}
}

void FRemoteControlPresetRebindingManager::RebindEntitiesForActorClass_Legacy(URemoteControlPreset* Preset, UClass* Class, const TArray<TSharedPtr<FRemoteControlEntity>>& UnboundEntities)
{
	TSet<FGuid> BoundProperties;
	TSet<AActor*> BoundActors;

	if (const TArray<UObject*>* ObjectsToConsider = Context.ObjectsGroupedByRelevantClass.Find(Class))
	{
		for (const TSharedPtr<FRemoteControlEntity>& RCEntity : UnboundEntities)
		{
			if (!BoundProperties.Contains(RCEntity->GetId()))
			{
				for (UObject* ObjToConsider : *ObjectsToConsider)
				{
					if (AActor* ActorToConsider = Cast<AActor>(ObjToConsider))
					{
						if (!BoundActors.Contains(ActorToConsider))
						{
							constexpr bool bUseRebindingContext = false;
							BoundProperties.Append(RebindAllEntitiesUnderSameActor(Preset, RCEntity, ActorToConsider, bUseRebindingContext));
							BoundActors.Add(ActorToConsider);
							break;
						}
					}
				}
			}
		}
	}
}

