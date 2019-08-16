// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ToolBuilderUtil.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Components/StaticMeshComponent.h"



bool ToolBuilderUtil::IsMeshDescriptionSourceComponent(UActorComponent* ComponentObject)
{
	UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(ComponentObject);
	if (StaticMeshComp != nullptr)
	{
		return true;
	}
	return false;
}



int ToolBuilderUtil::CountComponents(const FToolBuilderState& InputState, const TFunction<bool(UActorComponent*)>& Predicate)
{
	int nTypedComponents = 0;

	if (InputState.SelectedComponents != nullptr && InputState.SelectedComponents->Num() > 0)
	{
		for (FSelectionIterator Iter(*InputState.SelectedComponents); Iter; ++Iter)
		{
			if (UActorComponent* Component = Cast<UActorComponent>(*Iter))
			{
				if (Predicate(Component))
				{
					nTypedComponents++;
				}
			}
		}
	}
	else
	{
		for (FSelectionIterator Iter(*InputState.SelectedActors); Iter; ++Iter)
		{
			if (AActor* Actor = Cast<AActor>(*Iter))
			{
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (Predicate(Component))
					{
						nTypedComponents++;
					}
				}
			}
		}
	}

	return nTypedComponents;
}




UActorComponent* ToolBuilderUtil::FindFirstComponent(const FToolBuilderState& InputState, const TFunction<bool(UActorComponent*)>& Predicate)
{
	if (InputState.SelectedComponents != nullptr && InputState.SelectedComponents->Num() > 0)
	{
		for (FSelectionIterator Iter(*InputState.SelectedComponents); Iter; ++Iter)
		{
			if (UActorComponent* Component = Cast<UActorComponent>(*Iter))
			{
				if (Predicate(Component))
				{
					return Component;
				}
			}
		}
	}
	else
	{
		for (FSelectionIterator Iter(*InputState.SelectedActors); Iter; ++Iter)
		{
			if (AActor* Actor = Cast<AActor>(*Iter))
			{
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (Predicate(Component))
					{
						return Component;
					}
				}
			}
		}
	}

	return nullptr;
}




TArray<UActorComponent*> ToolBuilderUtil::FindAllComponents(const FToolBuilderState& InputState, const TFunction<bool(UActorComponent*)>& Predicate)
{
	TArray<UActorComponent*> Components;

	if (InputState.SelectedComponents != nullptr && InputState.SelectedComponents->Num() > 0)
	{
		for (FSelectionIterator Iter(*InputState.SelectedComponents); Iter; ++Iter)
		{
			if (UActorComponent* Component = Cast<UActorComponent>(*Iter))
			{
				if (Predicate(Component))
				{
					Components.AddUnique(Component);
				}
			}
		}
	}
	else
	{
		for (FSelectionIterator Iter(*InputState.SelectedActors); Iter; ++Iter)
		{
			if (AActor* Actor = Cast<AActor>(*Iter))
			{
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (Predicate(Component))
					{
						Components.AddUnique(Component);
					}
				}
			}
		}
	}

	return Components;
}

