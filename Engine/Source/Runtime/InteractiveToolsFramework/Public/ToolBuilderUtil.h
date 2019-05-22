// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "InteractiveToolBuilder.h"

/**
* Helper functions that can be used in InteractiveToolBuilder implementations
*/
namespace ToolBuilderUtil
{
	/** Returns true if this UObject can provide a FMeshDescription */
	INTERACTIVETOOLSFRAMEWORK_API
	bool IsMeshDescriptionSourceComponent(UActorComponent* ComponentObject);
	
	/** Count number of selected components that pass predicate. If Component selection is not empty, returns that count, otherwise counts in all selected Actors */
	INTERACTIVETOOLSFRAMEWORK_API
	int CountComponents(const FToolBuilderState& InputState, const TFunction<bool(UActorComponent*)>& Predicate);

	/** First first available component that passes predicate. Searches Components selection list first, then all Actors */
	INTERACTIVETOOLSFRAMEWORK_API
	UActorComponent* FindFirstComponent(const FToolBuilderState& InputState, const TFunction<bool(UActorComponent*)>& Predicate);

	/** First all components that passes predicate. Searches Components selection list first, then all Actors */
	INTERACTIVETOOLSFRAMEWORK_API
	TArray<UActorComponent*> FindAllComponents(const FToolBuilderState& InputState, const TFunction<bool(UActorComponent*)>& Predicate);

	// @todo not sure that actors with multiple components are handled properly...
	/** Count number of components of given type. If Component selection is not empty, returns that count, otherwise counts in all selected Actors */
	template<typename ComponentType>
	int CountSelectedComponentsOfType(const FToolBuilderState& InputState);

	/** First first available component of given type. Searches Components selection list first, then all Actors */
	template<typename ComponentType>
	ComponentType* FindFirstComponentOfType(const FToolBuilderState& InputState);

}




/*
 * Template Implementations
 */


template<typename ComponentType>
int ToolBuilderUtil::CountSelectedComponentsOfType(const FToolBuilderState& InputState)
{
	int nTypedComponents = 0;

	if (InputState.SelectedComponents != nullptr && InputState.SelectedComponents->Num() > 0)
	{
		for (FSelectionIterator Iter(*InputState.SelectedComponents); Iter; ++Iter)
		{
			if (ComponentType* TypedComponent = Cast<ComponentType>(*Iter))
			{
				nTypedComponents++;
			}
		}
	}
	else
	{
		for (FSelectionIterator Iter(*InputState.SelectedActors); Iter; ++Iter)
		{
			if (AActor* Actor = Cast<AActor>(*Iter))
			{
				// [RMS] is there a more efficient way to do this?
				TArray<UActorComponent*> TypedComponents = Actor->GetComponentsByClass(ComponentType::StaticClass());
				nTypedComponents += TypedComponents.Num();
			}
		}
	}

	return nTypedComponents;
}






template<typename ComponentType>
ComponentType* ToolBuilderUtil::FindFirstComponentOfType(const FToolBuilderState& InputState)
{
	if (InputState.SelectedComponents != nullptr && InputState.SelectedComponents->Num() > 0)
	{
		for (FSelectionIterator Iter(*InputState.SelectedComponents); Iter; ++Iter)
		{
			if (ComponentType* TypedComponent = Cast<ComponentType>(*Iter))
			{
				return TypedComponent;
			}
		}
	}
	else
	{
		for (FSelectionIterator Iter(*InputState.SelectedActors); Iter; ++Iter)
		{
			if (AActor* Actor = Cast<AActor>(*Iter))
			{
				// [RMS] is there a more efficient way to do this?
				TArray<UActorComponent*> TypedComponents = Actor->GetComponentsByClass(ComponentType::StaticClass());
				if (TypedComponents.Num() > 0)
				{
					return (ComponentType*)TypedComponents[0];
				}
			}
		}
	}

	return nullptr;
}

