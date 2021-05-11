// Copyright Epic Games, Inc. All Rights Reserved.

#include "ApplySnapshotFilter.h"

#include "IPropertyComparer.h"
#include "Data/LevelSnapshot.h"
#include "Data/PropertySelection.h"
#include "LevelSnapshotFilters.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsModule.h"
#include "PropertyComparisonParams.h"
#include "Restorability/SnapshotRestorability.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

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
	if (!ensure(WorldClass == DeserializedClass))
	{
		UE_LOG(
            LogLevelSnapshots,
            Error,
            TEXT("FApplySnapshotFilter::ApplyFilterToFindSelectedProperties: WorldActor class '%s' differs from DerserializedSnapshoatActor class '%s'. Differing classes are not supported."),
            *WorldActor->GetClass()->GetName(),
            *DeserializedSnapshotActor->GetClass()->GetName()
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
			FilterComponentPair(MapToAddTo, SnapshotMatchedComp, WorldComp);
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
	MapToAddTo.AddObjectProperties(WorldActor, ActorSelection);
}

void FApplySnapshotFilter::FilterComponentPair(FPropertySelectionMap& MapToAddTo, UActorComponent* SnapshotComponent, UActorComponent* WorldComponent)
{
	FPropertySelection ComponentSelection;
	FPropertyContainerContext ComponentContext(
		ComponentSelection,
        SnapshotComponent->GetClass(),
        SnapshotComponent,
        WorldComponent,
        { WorldComponent->GetName() },
        FLevelSnapshotPropertyChain(),
		WorldComponent->GetClass()
        );
	
	AnalyseRootProperties(ComponentContext, SnapshotComponent, WorldComponent);
	MapToAddTo.AddObjectProperties(WorldComponent, ComponentSelection);
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

void FApplySnapshotFilter::AnalyseRootProperties(FPropertyContainerContext& ContainerContext, UObject* SnapshotObject, UObject* WorldObject)
{
	FLevelSnapshotsModule& Module = FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
	const FPropertyComparerArray PropertyComparers = Module.GetPropertyComparerForClass(ContainerContext.RootClass);
	
	for (TFieldIterator<FProperty> FieldIt(ContainerContext.ContainerClass); FieldIt; ++FieldIt)
	{
		// Ask external modules about the property
		const FPropertyComparisonParams Params { ContainerContext.RootClass, *FieldIt, ContainerContext.SnapshotContainer, ContainerContext.WorldContainer, SnapshotObject, WorldObject, DeserializedSnapshotActor, WorldActor} ;
		const IPropertyComparer::EPropertyComparison ComparisonResult = Module.ShouldConsiderPropertyEqual(PropertyComparers, Params);
		if (ComparisonResult == IPropertyComparer::EPropertyComparison::TreatEqual)
		{
			continue;
		} 
		
		const ECheckSubproperties CheckSubpropertyBehaviour = AnalyseProperty(ContainerContext, *FieldIt);
		if (CheckSubpropertyBehaviour == ECheckSubproperties::CheckSubproperties)
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

FApplySnapshotFilter::ECheckSubproperties FApplySnapshotFilter::AnalyseProperty(FPropertyContainerContext& ContainerContext, FProperty* PropertyInCommon)
{
	check(ContainerContext.WorldContainer);
	check(ContainerContext.SnapshotContainer); 

	if (!bAllowUnchangedProperties && Snapshot->AreSnapshotAndOriginalPropertiesEquivalent(PropertyInCommon, ContainerContext.SnapshotContainer, ContainerContext.WorldContainer, DeserializedSnapshotActor, WorldActor))
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
