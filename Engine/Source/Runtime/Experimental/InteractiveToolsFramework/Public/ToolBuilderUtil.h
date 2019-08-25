// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Algo/Count.h"
#include "Algo/Find.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "InteractiveToolBuilder.h"

/**
* Helper functions that can be used in InteractiveToolBuilder implementations
*/
namespace ToolBuilderUtil
{
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
	return CountComponents(InputState, [](UActorComponent* Actor) { return Cast<ComponentType>(Actor) != nullptr; });
}

template<typename ComponentType>
ComponentType* ToolBuilderUtil::FindFirstComponentOfType(const FToolBuilderState& InputState)
{
	return FindFirstComponent(InputState, [](UActorComponent* Actor) { return Cast<ComponentType>(Actor) != nullptr; });
}

