// Copyright Epic Games, Inc. All Rights Reserved.

#include "ApplySnapshotFilter.h"

#include "LevelSnapshot.h"
#include "LevelSnapshotFilters.h"
#include "LevelSnapshotsLog.h"

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
		if (SnapshotComponent->GetClass() != WorldComponent->GetClass())
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
	if (EnsureParametersAreValid() && ULevelSnapshot::IsActorDesirableForCapture(WorldActor))
	{
		FilterActorPair(MapToAddTo);
		AnalyseComponentProperties(MapToAddTo);
	}
}

FApplySnapshotFilter::FPropertyContainerContext::FPropertyContainerContext(FPropertySelection& SelectionToAddTo, UStruct* ContainerClass, void* SnapshotContainer, void* WorldContainer, TArray<FString> AuthoredPathInformation)
	:
	SelectionToAddTo(SelectionToAddTo),
	ContainerClass(ContainerClass),
	SnapshotContainer(SnapshotContainer),
	WorldContainer(WorldContainer),
	AuthoredPathInformation(AuthoredPathInformation)
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
	
	if (!ensure(WorldActor->GetClass() == DeserializedSnapshotActor->GetClass()))
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
		if (!ULevelSnapshot::IsComponentDesirableForCapture(WorldComp))
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
        {}
        );
	
	AnalyseProperties(ActorContext);
	MapToAddTo.AddObjectProperties(WorldActor, ActorSelection.SelectedPropertyPaths);
}

void FApplySnapshotFilter::FilterComponentPair(FPropertySelectionMap& MapToAddTo, UActorComponent* SnapshotComponent, UActorComponent* WorldComponent)
{
	FPropertySelection ComponentSelection;
	FPropertyContainerContext ComponentContext(
		ComponentSelection,
        SnapshotComponent->GetClass(),
        SnapshotComponent,
        WorldComponent,
        { WorldComponent->GetName() }
        );

	AnalyseProperties(ComponentContext);
	MapToAddTo.AddObjectProperties(WorldComponent, ComponentSelection.SelectedPropertyPaths);
}

void FApplySnapshotFilter::FilterStructPair(FPropertyContainerContext& Parent, FStructProperty* StructProperty)
{
	FPropertyContainerContext StructContext(Parent.SelectionToAddTo,
        StructProperty->Struct,
        StructProperty->ContainerPtrToValuePtr<uint8>(Parent.SnapshotContainer),
        StructProperty->ContainerPtrToValuePtr<uint8>(Parent.WorldContainer),
        Parent.AuthoredPathInformation
        );
	StructContext.AuthoredPathInformation.Add(StructProperty->GetAuthoredName());
	
	AnalyseProperties(StructContext);
}

void FApplySnapshotFilter::AnalyseProperties(FPropertyContainerContext& ContainerContext)
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

	// Only show editable or visible properties
	const bool bHasRequiredFlags = bAllowNonEditableProperties || PropertyInCommon->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
	const bool bHasForbiddenFlags = PropertyInCommon->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated);
	if (!bHasRequiredFlags || bHasForbiddenFlags)
	{
		return SkipSubproperties;
	}

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
		// TODO: Track the path to the property (similar to AuthoredPathInformation) instead TFieldPath; TFieldPath gives insufficient info to identify struct members, e.g. Script/MyFooStruct:FooProperty.
		TArray<TFieldPath<FProperty>>& SelectedProperties = ContainerContext.SelectionToAddTo.SelectedPropertyPaths;
		SelectedProperties.Add(PropertyInCommon);
		return CheckSubproperties;
	}
	return SkipSubproperties;
}
