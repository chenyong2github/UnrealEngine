// Copyright Epic Games, Inc. All Rights Reserved.

#include "ApplySnapshotFilter.h"

#include "IPropertyComparer.h"
#include "CustomSerialization/CustomSerializationDataManager.h"
#include "Data/LevelSnapshot.h"
#include "Data/PropertySelection.h"
#include "LevelSnapshotFilters.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsModule.h"
#include "PropertyComparisonParams.h"
#include "Restorability/SnapshotRestorability.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"

namespace
{
	UActorComponent* TryFindMatchingComponent(AActor* SnapshotActor, const TInlineComponentArray<UActorComponent*>& SnapshotComponents, AActor* WorldActor, UActorComponent* WorldComponent)
	{
		const FName ComponentName = WorldComponent->GetFName();
		UActorComponent* const* PossibleResult = SnapshotComponents.FindByPredicate([ComponentName](UActorComponent* Component)
		{
			return Component->GetFName().IsEqual(ComponentName);
		});
		
		if (!PossibleResult)
		{
			UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to match world component called '%s' of world actor called '%s' to any components in snapshot actor. Did you change the name of a component?"),
                *WorldComponent->GetName(),
                *WorldActor->GetName()
            );
			return nullptr;
		}

		UActorComponent* SnapshotComponent = *PossibleResult;
		if (!ensure(SnapshotComponent->GetClass() == WorldComponent->GetClass()))
		{
			UE_LOG(LogLevelSnapshots, Error, TEXT("Snapshot component called  %s of snapshot actor called %s has class %s while world component called %s in world actor called %s had class %s. Components are expected to not change classes. Did you change the class of a component?"),
                *SnapshotComponent->GetName(),
                *SnapshotActor->GetName(),
                *SnapshotComponent->GetClass()->GetName(),
                *WorldActor->GetName(),
                *WorldComponent->GetName(),
                *WorldComponent->GetClass()->GetName()
            );
			return nullptr;
		}

		return SnapshotComponent;
	}
}

FApplySnapshotFilter FApplySnapshotFilter::Make(ULevelSnapshot* Snapshot, AActor* DeserializedSnapshotActor, AActor* WorldActor, const ULevelSnapshotFilter* Filter)
{
	return FApplySnapshotFilter(Snapshot, DeserializedSnapshotActor, WorldActor, Filter);
}

void FApplySnapshotFilter::ApplyFilterToFindSelectedProperties(FPropertySelectionMap& MapToAddTo)
{
	SCOPED_SNAPSHOT_CORE_TRACE(ApplyFilters);
	
	if (EnsureParametersAreValid() && FSnapshotRestorability::IsActorDesirableForCapture(WorldActor) && EFilterResult::CanInclude(Filter->IsActorValid({ DeserializedSnapshotActor, WorldActor })))
	{
		FilterActorPair(MapToAddTo);
		AnalyseComponentProperties(MapToAddTo);
	}
}

FApplySnapshotFilter::FPropertyContainerContext::FPropertyContainerContext(
	FPropertySelection& SelectionToAddTo, UStruct* ContainerClass, void* SnapshotContainer, void* WorldContainer, const TArray<FString>& AuthoredPathInformation, const FLevelSnapshotPropertyChain& PropertyChain, UClass* RootClass)
	:
	SelectionToAddTo(SelectionToAddTo),
	ContainerClass(ContainerClass),
	SnapshotContainer(SnapshotContainer),
	WorldContainer(WorldContainer),
	AuthoredPathInformation(AuthoredPathInformation),
	PropertyChain(PropertyChain),
	RootClass(RootClass)
{}

FApplySnapshotFilter::FApplySnapshotFilter(ULevelSnapshot* Snapshot, AActor* DeserializedSnapshotActor, AActor* WorldActor, const ULevelSnapshotFilter* Filter)
    :
    Snapshot(Snapshot),
    DeserializedSnapshotActor(DeserializedSnapshotActor),
    WorldActor(WorldActor),
    Filter(Filter)
{
	EnsureParametersAreValid();
}

bool FApplySnapshotFilter::EnsureParametersAreValid() const
{
	if (!ensure(WorldActor && DeserializedSnapshotActor && Filter))
	{
		return false;
	}

	UClass* WorldClass = WorldActor->GetClass();
	UClass* DeserializedClass = DeserializedSnapshotActor->GetClass();
	if (WorldClass != DeserializedClass)
	{
		UE_LOG(
            LogLevelSnapshots,
            Error,
            TEXT("FApplySnapshotFilter::ApplyFilterToFindSelectedProperties: WorldActor class '%s' differs from DerserializedSnapshoatActor class '%s'. Differing classes are not supported. (Actor: %s)"),
            *WorldActor->GetClass()->GetName(),
            *DeserializedSnapshotActor->GetClass()->GetName(),
			*WorldActor->GetPathName()
        );
		return false;
	}
	
	return true;
}

void FApplySnapshotFilter::AnalyseComponentProperties(FPropertySelectionMap& MapToAddTo)
{
	TInlineComponentArray<UActorComponent*> WorldComponents;
	TInlineComponentArray<UActorComponent*> SnapshotComponents;
	WorldActor->GetComponents(WorldComponents);
	DeserializedSnapshotActor->GetComponents(SnapshotComponents);
	for (UActorComponent* WorldComp : WorldComponents)
	{
		if (!FSnapshotRestorability::IsComponentDesirableForCapture(WorldComp))
		{
			UE_LOG(LogLevelSnapshots, Verbose, TEXT("Skipping world component '%s' of world actor '%s'"), *WorldComp->GetName(), *WorldActor->GetName());
			continue;
		}
		
		if (UActorComponent* SnapshotMatchedComp = TryFindMatchingComponent(DeserializedSnapshotActor, SnapshotComponents, WorldActor, WorldComp))
		{
			FilterSubobjectPair(MapToAddTo, SnapshotMatchedComp, WorldComp);
		}
	}
}

void FApplySnapshotFilter::FilterActorPair(FPropertySelectionMap& MapToAddTo)
{
	FPropertySelection ActorSelection;
	FPropertyContainerContext ActorContext(
		ActorSelection,
        DeserializedSnapshotActor->GetClass(),
        DeserializedSnapshotActor,
        WorldActor,
        {},
        FLevelSnapshotPropertyChain(),
        WorldActor->GetClass()
        );
	
	AnalyseRootProperties(ActorContext, DeserializedSnapshotActor, WorldActor);
	const EFilterObjectPropertiesResult FilterResult = FindAndFilterCustomSubobjectPairs(MapToAddTo, DeserializedSnapshotActor, WorldActor);
	
	ActorSelection.SetHasCustomSerializedSubobjects(FilterResult == EFilterObjectPropertiesResult::HasCustomSubobjects);
	MapToAddTo.AddObjectProperties(WorldActor, ActorSelection);
}

bool FApplySnapshotFilter::FilterSubobjectPair(FPropertySelectionMap& MapToAddTo, UObject* SnapshotSubobject, UObject* WorldSubobject)
{
	FPropertySelection SubobjectSelection;
	FPropertyContainerContext ComponentContext(
		SubobjectSelection,
        SnapshotSubobject->GetClass(),
        SnapshotSubobject,
        WorldSubobject,
        { WorldSubobject->GetName() },
        FLevelSnapshotPropertyChain(),
		WorldSubobject->GetClass()
        );
	
	AnalyseRootProperties(ComponentContext, SnapshotSubobject, WorldSubobject);
	const EFilterObjectPropertiesResult FilterResult = FindAndFilterCustomSubobjectPairs(MapToAddTo, SnapshotSubobject, WorldSubobject);

	if (ensureMsgf(MapToAddTo.GetSelectedProperties(WorldSubobject) == nullptr, TEXT("Object %s was analysed more than once. Most likely an ICustomObjectSnapshotSerializer implementation returned the same object pair more than once or returned an object pair that was already discovered by the standard system, e.g. UPROPERTY(EditAnywhere, Instanced) UObject* Object."), *WorldSubobject->GetName()))
	{
		SubobjectSelection.SetHasCustomSerializedSubobjects(FilterResult == EFilterObjectPropertiesResult::HasCustomSubobjects);
		return MapToAddTo.AddObjectProperties(WorldSubobject, SubobjectSelection);
	}

	return false;
}

void FApplySnapshotFilter::FilterStructPair(FPropertyContainerContext& Parent, FStructProperty* StructProperty)
{
	FPropertyContainerContext StructContext(Parent.SelectionToAddTo,
        StructProperty->Struct,
        StructProperty->ContainerPtrToValuePtr<uint8>(Parent.SnapshotContainer),
        StructProperty->ContainerPtrToValuePtr<uint8>(Parent.WorldContainer),
        Parent.AuthoredPathInformation,
        Parent.PropertyChain.MakeAppended(StructProperty),
        Parent.RootClass
        );
	StructContext.AuthoredPathInformation.Add(StructProperty->GetAuthoredName());
	AnalyseStructProperties(StructContext);
}

FApplySnapshotFilter::EFilterObjectPropertiesResult FApplySnapshotFilter::FindAndFilterCustomSubobjectPairs(FPropertySelectionMap& MapToAddTo, UObject* SnapshotOwner, UObject* WorldOwner)
{
	FLevelSnapshotsModule& LevelSnapshots = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
	TSharedPtr<ICustomObjectSnapshotSerializer> ExternalSerializer = LevelSnapshots.GetCustomSerializerForClass(SnapshotOwner->GetClass());
	if (!ExternalSerializer.IsValid())
	{
		return EFilterObjectPropertiesResult::HasOnlyNormalProperties;
	}

	const FCustomSerializationData* SerializationData = Snapshot->GetSerializedData().GetCustomSubobjectData_ForActorOrSubobject(WorldOwner);
	if (!SerializationData)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Custom ICustomObjectSnapshotSerializer is registered for class %s but no data was saved for it."), *WorldOwner->GetClass()->GetName());
		return EFilterObjectPropertiesResult::HasOnlyNormalProperties;
	}

	const FCustomSerializationDataReader SubobjectDataReader(
			FCustomSerializationDataGetter_ReadOnly::CreateLambda([SerializationData]() -> const FCustomSerializationData* { return SerializationData; }),
			Snapshot->GetSerializedData()
			);
	bool bAtLeastOneSubobjectWasAdded = false;
	for (int32 i = 0; i < SubobjectDataReader.GetNumSubobjects(); ++i)
	{	
		TSharedPtr<ISnapshotSubobjectMetaData> SubobjectMetadata = SubobjectDataReader.GetSubobjectMetaData(i);
		UObject* SnapshotSubobject = ExternalSerializer->FindOrRecreateSubobjectInSnapshotWorld(SnapshotOwner, *SubobjectMetadata, SubobjectDataReader);
		UObject* EditorSubobject = ExternalSerializer->FindSubobjectInEditorWorld(WorldOwner, *SubobjectMetadata, SubobjectDataReader);
		
		// External modules implement this: must be validated
		if (!SnapshotSubobject || !EditorSubobject)
		{
			UE_CLOG(SnapshotSubobject && !EditorSubobject, LogLevelSnapshots, Warning, TEXT("Restoring missing subobjects is not supported"));
			continue;
		}

		if (ensureAlwaysMsgf(SnapshotSubobject->IsIn(SnapshotOwner) && EditorSubobject->IsIn(WorldOwner), TEXT("Your interface must return subobjects")))
		{
			bAtLeastOneSubobjectWasAdded |= FilterSubobjectPair(MapToAddTo, SnapshotSubobject, EditorSubobject);
		}
	}

	return bAtLeastOneSubobjectWasAdded ? EFilterObjectPropertiesResult::HasCustomSubobjects : EFilterObjectPropertiesResult::HasOnlyNormalProperties;
}

void FApplySnapshotFilter::AnalyseRootProperties(FPropertyContainerContext& ContainerContext, UObject* SnapshotObject, UObject* WorldObject)
{
	FLevelSnapshotsModule& Module = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
	const FPropertyComparerArray PropertyComparers = Module.GetPropertyComparerForClass(ContainerContext.RootClass);
	
	for (TFieldIterator<FProperty> FieldIt(ContainerContext.ContainerClass); FieldIt; ++FieldIt)
	{
		// Ask external modules about the property
		const FPropertyComparisonParams Params { ContainerContext.RootClass, *FieldIt, ContainerContext.SnapshotContainer, ContainerContext.WorldContainer, SnapshotObject, WorldObject, DeserializedSnapshotActor, WorldActor} ;
		const IPropertyComparer::EPropertyComparison ComparisonResult = Module.ShouldConsiderPropertyEqual(PropertyComparers, Params);

		bool bSkipEqualityTest = false;
		switch (ComparisonResult)
		{
		case IPropertyComparer::EPropertyComparison::TreatEqual:
			continue;

		case IPropertyComparer::EPropertyComparison::TreatUnequal:
			bSkipEqualityTest = true;

		default:
			break;
		}
		
		const ECheckSubproperties CheckSubpropertyBehaviour = AnalyseProperty(ContainerContext, *FieldIt, bSkipEqualityTest);
		if (!bSkipEqualityTest && CheckSubpropertyBehaviour == ECheckSubproperties::CheckSubproperties)
		{
			HandleStructProperties(ContainerContext, *FieldIt);
			// TODO: To analyse subobjects, you'd add another utility function here
		}
	}
}

void FApplySnapshotFilter::AnalyseStructProperties(FPropertyContainerContext& ContainerContext)
{
	for (TFieldIterator<FProperty> FieldIt(ContainerContext.ContainerClass); FieldIt; ++FieldIt)
	{
		const ECheckSubproperties CheckSubpropertyBehaviour = AnalyseProperty(ContainerContext, *FieldIt);
		if (CheckSubpropertyBehaviour == ECheckSubproperties::CheckSubproperties)
		{
			HandleStructProperties(ContainerContext, *FieldIt);
			// TODO: To analyse subobjects, you'd add another utility function here
		}
	}
}

void FApplySnapshotFilter::HandleStructProperties(FPropertyContainerContext& ContainerContext, FProperty* PropertyToHandle)
{
	if (FStructProperty* StructProperty = CastField<FStructProperty>(PropertyToHandle))
	{
		FilterStructPair(ContainerContext, StructProperty);
	}
}

FApplySnapshotFilter::ECheckSubproperties FApplySnapshotFilter::AnalyseProperty(FPropertyContainerContext& ContainerContext, FProperty* PropertyInCommon, bool bSkipEqualityTest)
{
	check(ContainerContext.WorldContainer);
	check(ContainerContext.SnapshotContainer); 

	if (!bSkipEqualityTest && !bAllowUnchangedProperties && Snapshot->AreSnapshotAndOriginalPropertiesEquivalent(PropertyInCommon, ContainerContext.SnapshotContainer, ContainerContext.WorldContainer, DeserializedSnapshotActor, WorldActor))
	{
		return SkipSubproperties;
	}

	// Now we can ask user whether they care about the property
	TArray<FString> PropertyPath = ContainerContext.AuthoredPathInformation;
	PropertyPath.Add(PropertyInCommon->GetAuthoredName());
	const bool bIsPropertyValid = EFilterResult::CanInclude(Filter->IsPropertyValid(
        { DeserializedSnapshotActor, WorldActor, ContainerContext.SnapshotContainer, ContainerContext.WorldContainer, PropertyInCommon, PropertyPath }
        ));
						
	if (bIsPropertyValid)
	{
		ContainerContext.SelectionToAddTo.AddProperty(ContainerContext.PropertyChain.MakeAppended(PropertyInCommon));
		return CheckSubproperties;
	}
	return SkipSubproperties;
}
