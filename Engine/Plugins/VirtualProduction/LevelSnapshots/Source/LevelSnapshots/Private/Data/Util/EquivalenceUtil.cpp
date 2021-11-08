// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/Util/EquivalenceUtil.h"

#include "Archive/ApplySnapshotToEditorArchive.h"
#include "Data/WorldSnapshotData.h"
#include "Data/SnapshotCustomVersion.h"
#include "Data/CustomSerialization/CustomObjectSerializationWrapper.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsModule.h"
#include "Params/PropertyComparisonParams.h"
#include "PropertyInfoHelpers.h"
#include "SnapshotRestorability.h"
#include "SnapshotUtil.h"

#include "Components/ActorComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace
{
	/** Iterates properties of SnapshotObject and WorldObject, owned by SnapshotActor and WorldActor, and returns whether at least one property value was different */
	bool HaveDifferentPropertyValues(const FWorldSnapshotData& WorldData, UObject* SnapshotObject, UObject* WorldObject, AActor* SnapshotActor, AActor* WorldActor)
	{
		UClass* ClassToIterate = SnapshotObject->GetClass();
		
		FLevelSnapshotsModule& Module = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
		const FPropertyComparerArray PropertyComparers = Module.GetPropertyComparerForClass(ClassToIterate);
		for (TFieldIterator<FProperty> FieldIt(ClassToIterate); FieldIt; ++FieldIt)
		{
			// Ask external modules about the property
			const FPropertyComparisonParams Params { WorldData, ClassToIterate, *FieldIt, SnapshotObject, WorldObject, SnapshotObject, WorldObject, SnapshotActor, WorldActor} ;
			const IPropertyComparer::EPropertyComparison ComparisonResult = Module.ShouldConsiderPropertyEqual(PropertyComparers, Params);
			
			switch (ComparisonResult)
			{
			case IPropertyComparer::EPropertyComparison::TreatEqual:
				continue;
			case IPropertyComparer::EPropertyComparison::TreatUnequal:
				return true;
			default:
				if (!SnapshotUtil::AreSnapshotAndOriginalPropertiesEquivalent(WorldData, *FieldIt, SnapshotObject, WorldObject, SnapshotActor, WorldActor))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool EnqueueMatchingCustomSubobjects(TInlineComponentArray<TPair<UObject*, UObject*>>& SnapshotOriginalPairsToProcess, const FWorldSnapshotData& WorldData, UObject* SnapshotObject, UObject* WorldObject)
	{
		bool bFailedToMatchAllObjects = false;
		FCustomObjectSerializationWrapper::ForEachMatchingCustomSubobjectPair(WorldData, SnapshotObject, WorldObject,
			[&SnapshotOriginalPairsToProcess, &WorldData, &bFailedToMatchAllObjects](UObject* SnapshotSubobject, UObject* WorldSubobject)
			{
				SnapshotOriginalPairsToProcess.Add(TPair<UObject*, UObject*>(SnapshotSubobject, WorldSubobject));
				if (!bFailedToMatchAllObjects)
				{
					bFailedToMatchAllObjects |= EnqueueMatchingCustomSubobjects(SnapshotOriginalPairsToProcess, WorldData, SnapshotSubobject, WorldSubobject);
				}
			},
			[&bFailedToMatchAllObjects](UObject* UmatchedSnapshotSubobject)
			{
				bFailedToMatchAllObjects = true;
			}
		);

		return bFailedToMatchAllObjects;
	}

	UActorComponent* TryFindMatchingComponent(AActor* ActorToSearchOn, UActorComponent* CounterpartComponentToMatch)
	{
		UActorComponent* MatchedComponent = SnapshotUtil::FindMatchingComponent(ActorToSearchOn, CounterpartComponentToMatch);
		if (MatchedComponent && MatchedComponent->GetClass() != CounterpartComponentToMatch->GetClass())
		{
			UE_LOG(LogLevelSnapshots, Error, TEXT("Components named %s were matched to each other but had differing classes (%s and %s)."),
				*CounterpartComponentToMatch->GetName(),
				*MatchedComponent->GetClass()->GetName(),
				*CounterpartComponentToMatch->GetClass()->GetName()
			);
			return nullptr;
		}

		return MatchedComponent;
	}

	/** @return False if one component could not be matched */
	bool EnqueueMatchingComponents(TInlineComponentArray<TPair<UObject*, UObject*>>& SnapshotOriginalPairsToProcess, const FWorldSnapshotData& WorldData, AActor* SnapshotActor, AActor* WorldActor)
	{
		bool bFoundUnmatchedObjects = false;
		SnapshotUtil::IterateComponents(SnapshotActor, WorldActor,
			[&SnapshotOriginalPairsToProcess, &WorldData, &bFoundUnmatchedObjects](UActorComponent* SnapshotComp, UActorComponent* WorldComp)
			{
				bFoundUnmatchedObjects |= EnqueueMatchingCustomSubobjects(SnapshotOriginalPairsToProcess, WorldData, SnapshotComp, WorldComp);
				SnapshotOriginalPairsToProcess.Add(TPair<UObject*, UObject*>(SnapshotComp, WorldComp));
			},
			[&bFoundUnmatchedObjects](UActorComponent* SnapshotComp)
			{
				bFoundUnmatchedObjects = true;
			},
			[&bFoundUnmatchedObjects](UActorComponent* WorldComp)
			{
				bFoundUnmatchedObjects = true;
			});

		return bFoundUnmatchedObjects;
	}
}

void SnapshotUtil::IterateComponents(AActor* SnapshotActor, AActor* WorldActor, FHandleMatchedActorComponent OnComponentsMatched, FHandleUnmatchedActorComponent OnSnapshotComponentUnmatched, FHandleUnmatchedActorComponent OnWorldComponentUnmatched)
{
	for (UActorComponent* WorldComp : WorldActor->GetComponents())
	{
		if (!FSnapshotRestorability::IsComponentDesirableForCapture(WorldComp))
		{
			continue;
		}
		
		if (UActorComponent* SnapshotMatchedComp = TryFindMatchingComponent(SnapshotActor, WorldComp))
		{
			OnComponentsMatched(SnapshotMatchedComp, WorldComp);
		}
		else
		{
			OnWorldComponentUnmatched(WorldComp);
		}
	}

	for (UActorComponent* SnapshotComp : SnapshotActor->GetComponents())
	{
		if (FSnapshotRestorability::IsComponentDesirableForCapture(SnapshotComp)
			&& TryFindMatchingComponent(WorldActor, SnapshotComp) == nullptr)
		{
			OnSnapshotComponentUnmatched(SnapshotComp);
		}
	}
}

namespace
{
	FString ExtractRootToLeafComponentPath(const FSoftObjectPath& ComponentPath)
	{
		const TOptional<int32> FirstDotAfterActorName = SnapshotUtil::FindDotAfterActorName(ComponentPath);
		// PersistentLevel.SomeActor.SomeParentComp.SomeChildComp becomes SomeParentComp.SomeChildComp
		return ensure(FirstDotAfterActorName) ? ComponentPath.GetSubPathString().RightChop(*FirstDotAfterActorName) : FString();
	}
}

UActorComponent* SnapshotUtil::FindMatchingComponent(AActor* ActorToSearchOn, const FSoftObjectPath& ComponentPath)
{
	const FString RootToLeafPath = ExtractRootToLeafComponentPath(ComponentPath);
	if (!ensure(!RootToLeafPath.IsEmpty()))
	{
		return nullptr;
	}

	for (UActorComponent* Component : ActorToSearchOn->GetComponents())
	{
		const FString OtherRootToLeaf = ExtractRootToLeafComponentPath(Component);
		if (RootToLeafPath.Equals(OtherRootToLeaf))
		{
			return Component;
		}
	}

	return nullptr;
}

bool SnapshotUtil::HasOriginalChangedPropertiesSinceSnapshotWasTaken(const FWorldSnapshotData& WorldData, AActor* SnapshotActor, AActor* WorldActor)
{
	SCOPED_SNAPSHOT_CORE_TRACE(HasOriginalChangedPropertiesSinceSnapshotWasTaken);
	
	if (!IsValid(SnapshotActor) || !IsValid(WorldActor))
	{
		return SnapshotActor != WorldActor;
	}
		
	UClass* SnapshotClass = SnapshotActor->GetClass();
	UClass* WorldClass = WorldActor->GetClass();
	if (SnapshotClass != WorldClass)
	{
		return true;
	}

	TInlineComponentArray<TPair<UObject*, UObject*>> SnapshotOriginalPairsToProcess;
	SnapshotOriginalPairsToProcess.Add(TPair<UObject*, UObject*>(SnapshotActor, WorldActor));
	
	const bool bFailedToMatchAllComponentObjects = EnqueueMatchingComponents(SnapshotOriginalPairsToProcess, WorldData, SnapshotActor, WorldActor);
	if (bFailedToMatchAllComponentObjects)
	{
		return true;
	}
	
	const bool bFailedToMatchAllSubobjects = EnqueueMatchingCustomSubobjects(SnapshotOriginalPairsToProcess, WorldData, SnapshotActor, WorldActor);
	if (bFailedToMatchAllSubobjects)
	{
		return true;
	}
	
	for (const TPair<UObject*, UObject*>& NextPair : SnapshotOriginalPairsToProcess)
	{
		UObject* const SnapshotObject = NextPair.Key;
		UObject* const WorldObject = NextPair.Value;
		if (HaveDifferentPropertyValues(WorldData, SnapshotObject, WorldObject, SnapshotActor, WorldActor))
		{
			return true;
		}
	}
	return false;
}

bool SnapshotUtil::AreSnapshotAndOriginalPropertiesEquivalent(const FWorldSnapshotData& WorldData, const FProperty* LeafProperty, void* SnapshotContainer, void* WorldContainer, AActor* SnapshotActor, AActor* WorldActor)
{
	// Ensure that property's flags are allowed. Skip check collection properties, e.g. FArrayProperty::Inner, etc.: inner properties do not have the same flags.
	const bool bIsInnnerCollectionProperty = FPropertyInfoHelpers::IsPropertyInCollection(LeafProperty);
	if (!bIsInnnerCollectionProperty && !FSnapshotRestorability::IsRestorableProperty(LeafProperty))
	{
		return true;
	}
	
	for (int32 i = 0; i < LeafProperty->ArrayDim; ++i)
	{
		void* SnapshotValuePtr = LeafProperty->ContainerPtrToValuePtr<void>(SnapshotContainer, i);
		void* WorldValuePtr = LeafProperty->ContainerPtrToValuePtr<void>(WorldContainer, i);

		// Check whether float is nearly equal instead of exactly equal
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(LeafProperty))
		{
			return FPropertyInfoHelpers::AreNumericPropertiesNearlyEqual(NumericProperty, SnapshotValuePtr, WorldValuePtr);
		}
		
		// Use our custom equality function for struct properties
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(LeafProperty))
		{
			for (TFieldIterator<FProperty> FieldIt(StructProperty->Struct); FieldIt; ++FieldIt)
			{
				if (!AreSnapshotAndOriginalPropertiesEquivalent(WorldData, *FieldIt, SnapshotValuePtr, WorldValuePtr, SnapshotActor, WorldActor))
				{
					return false;
				}
			}
			return true;
		}

		// Check whether property value points to a subobject
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(LeafProperty))
		{
			return AreObjectPropertiesEquivalent(WorldData, ObjectProperty, SnapshotValuePtr, WorldValuePtr, SnapshotActor, WorldActor);
		}


		// Use our custom equality function for array, set, and map inner properties
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(LeafProperty))
		{
			FScriptArrayHelper SnapshotArray(ArrayProperty, SnapshotValuePtr);
			FScriptArrayHelper WorldArray(ArrayProperty, WorldValuePtr);
			if (SnapshotArray.Num() != WorldArray.Num())
			{
				return false;
			}

			for (int32 j = 0; j < SnapshotArray.Num(); ++j)
			{
				void* SnapshotElementValuePtr = SnapshotArray.GetRawPtr(j);
				void* WorldElementValuePtr = WorldArray.GetRawPtr(j);

				if (!AreSnapshotAndOriginalPropertiesEquivalent(WorldData, ArrayProperty->Inner, SnapshotElementValuePtr, WorldElementValuePtr, SnapshotActor, WorldActor))
				{
					return false;
				}
			}
			return true;
		}
		
		if (const FMapProperty* MapProperty = CastField<FMapProperty>(LeafProperty))
		{
			// TODO: Use custom function. Need to do something similar to UE4MapProperty_Private::IsPermutation
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(LeafProperty))
		{
			// TODO: Use custom function. Need to do something similar to UE4SetProperty_Private::IsPermutation
		}

		if (const FTextProperty* TextProperty = CastField<FTextProperty>(LeafProperty))
		{
			const FText& SnapshotText = TextProperty->GetPropertyValue_InContainer(SnapshotContainer);
			const FText& WorldText = TextProperty->GetPropertyValue_InContainer(WorldContainer);
			return SnapshotText.IdenticalTo(WorldText, ETextIdenticalModeFlags::None) || SnapshotText.ToString().Equals(WorldText.ToString());
		}
		
		// Use normal property comparison for all other properties
		if (!LeafProperty->Identical_InContainer(SnapshotContainer, WorldContainer, i, PPF_DeepComparison | PPF_DeepCompareDSOsOnly))
		{
			return false;
		}
	}
	return true;
}

bool SnapshotUtil::AreObjectPropertiesEquivalent(const FWorldSnapshotData& WorldData, const FObjectPropertyBase* ObjectProperty, void* SnapshotValuePtr, void* WorldValuePtr, AActor* SnapshotActor, AActor* WorldActor)
{
	// Native identity check handles:
	// - external references, e.g. UMaterial in content browser
	// - soft object paths: if SnapshotValuePtr is a TSoftObjectPtr<AActor> property, it retains the object path to the editor object.
	if (ObjectProperty->Identical(SnapshotValuePtr, WorldValuePtr, 0))
	{
		return true;
	}
		
	UObject* SnapshotObject = ObjectProperty->GetObjectPropertyValue(SnapshotValuePtr);
	UObject* WorldObject = ObjectProperty->GetObjectPropertyValue(WorldValuePtr);
	return AreReferencesEquivalent(WorldData, SnapshotObject, WorldObject, SnapshotActor, WorldActor);
}

namespace
{
	/** Checks whether the two actors object properties should be considered equivalent */
	bool AreActorsEquivalent(UObject* SnapshotPropertyValue, AActor* OriginalActorReference, const TMap<FSoftObjectPath, FActorSnapshotData>& ActorData)
	{
		// Compare actors
		const FActorSnapshotData* SavedData = ActorData.Find(OriginalActorReference);
		if (SavedData == nullptr)
		{
			return false;
		}

		// The snapshot actor was already allocated, if some other snapshot actor is referencing it
		const TOptional<AActor*> PreallocatedSnapshotVersion = SavedData->GetPreallocatedIfValidButDoNotAllocate();
		return PreallocatedSnapshotVersion.Get(nullptr) == SnapshotPropertyValue;
	}
	
	/** Checks whether the two subobject object properties should be considered equivalent */
	bool HaveSameNames(UObject* SnapshotPropertyValue, UObject* OriginalPropertyValue, const TMap<FSoftObjectPath, FActorSnapshotData>& ActorData)
	{
		AActor* SnapshotOwningActor = SnapshotPropertyValue->GetTypedOuter<AActor>();
		AActor* OriginalOwningActor = OriginalPropertyValue->GetTypedOuter<AActor>();
		if (!ensureMsgf(SnapshotOwningActor && OriginalOwningActor, TEXT("This is weird: the objects are part of a world and not actors, so they should be subobjects of actors, like components. Investigate")))
		{
			return false;
		}

		// Are the two subobjects owned by equivalent actors
		const FActorSnapshotData* EquivalentActorData = ActorData.Find(OriginalOwningActor);
		check (EquivalentActorData);
		AActor* EquivalentSnapshotActor = EquivalentActorData->GetPreallocatedIfValidButDoNotAllocate().Get(nullptr);
		const bool bAreOwnedByEquivalentActors = EquivalentSnapshotActor == SnapshotOwningActor; 
		if (!bAreOwnedByEquivalentActors)
		{
			return false;
		}

		// Check that chain of outers correspond to each other.
		UObject* CurrentSnapshotOuter = SnapshotPropertyValue;
		UObject* CurrentOriginalOuter = OriginalPropertyValue;
		for (; CurrentSnapshotOuter != SnapshotOwningActor && CurrentOriginalOuter != OriginalOwningActor; CurrentSnapshotOuter = CurrentSnapshotOuter->GetOuter(), CurrentOriginalOuter = CurrentOriginalOuter->GetOuter())
		{
			const bool bHaveSameName = CurrentSnapshotOuter->GetFName().IsEqual(CurrentOriginalOuter->GetFName());
			// I thought of also checking whether the two outers have the same class but I see no reason to atm
			if (!bHaveSameName)
			{
				return false;
			}
		}

		return true;
	}

	bool AreEquivalentSubobjects(const FWorldSnapshotData& WorldData, UObject* SnapshotPropertyValue, UObject* OriginalPropertyValue, AActor* SnapshotActor, AActor* OriginalActor)
	{
		AActor* OwningSnapshotActor = SnapshotPropertyValue->GetTypedOuter<AActor>();
		AActor* OwningWorldActor = OriginalPropertyValue->GetTypedOuter<AActor>();
		if (ensure(OwningSnapshotActor && OwningWorldActor))
		{
			return AreActorsEquivalent(OwningSnapshotActor, OwningWorldActor, WorldData.ActorData) && !HaveDifferentPropertyValues(WorldData, SnapshotPropertyValue, OriginalPropertyValue, SnapshotActor, OriginalActor);
		}
		return false;
	}
}

bool SnapshotUtil::AreReferencesEquivalent(const FWorldSnapshotData& WorldData, UObject* SnapshotPropertyValue, UObject* OriginalPropertyValue, AActor* SnapshotActor, AActor* OriginalActor)
{
	if (SnapshotPropertyValue == nullptr || OriginalPropertyValue == nullptr)
	{
		// Migration: We did not save subobject data. In this case snapshot version resolves to nullptr.
		const bool bWorldObjectIsSubobject = OriginalPropertyValue && !OriginalPropertyValue->IsA<AActor>() && OriginalPropertyValue->IsInA(AActor::StaticClass());
		const bool bIsOldSnapshot = WorldData.GetSnapshotVersionInfo().GetSnapshotCustomVersion() < FSnapshotCustomVersion::SubobjectSupport;
		const bool bOldSnapshotDataDidNotCaptureSubobjects = bIsOldSnapshot && bWorldObjectIsSubobject && SnapshotPropertyValue == nullptr;
		
		return bOldSnapshotDataDidNotCaptureSubobjects || SnapshotPropertyValue == OriginalPropertyValue;
	}
	if (SnapshotPropertyValue->GetClass() != OriginalPropertyValue->GetClass())
	{
		return false;
	}

	if (AActor* OriginalActorReference = Cast<AActor>(OriginalPropertyValue))
	{
		return AreActorsEquivalent(SnapshotPropertyValue, OriginalActorReference, WorldData.ActorData);
	}
	
	const bool bIsWorldObject = SnapshotPropertyValue->IsInA(UWorld::StaticClass()) && OriginalPropertyValue->IsInA(UWorld::StaticClass());
	if (bIsWorldObject)
	{
		// Note: Only components are required to have same names. Other subobjects only need to have equal properties.
		const bool bAreComponents = SnapshotPropertyValue->IsA<UActorComponent>() && OriginalPropertyValue->IsA<UActorComponent>();
		return (bAreComponents && HaveSameNames(SnapshotPropertyValue, OriginalPropertyValue, WorldData.ActorData))
			|| !FSnapshotRestorability::IsSubobjectDesirableForCapture(OriginalPropertyValue)
			|| (!bAreComponents && AreEquivalentSubobjects(WorldData, SnapshotPropertyValue, OriginalPropertyValue, SnapshotActor, OriginalActor));
	}
	
	return SnapshotPropertyValue == OriginalPropertyValue;
}