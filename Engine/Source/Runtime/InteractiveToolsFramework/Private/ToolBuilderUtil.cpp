// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ToolBuilderUtil.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Components/StaticMeshComponent.h"



bool ToolBuilderUtil::IsMeshDescriptionSourceComponent(UObject* ComponentObject)
{
	UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(ComponentObject);
	if (StaticMeshComp != nullptr)
	{
		return true;
	}
	return false;
}



int ToolBuilderUtil::CountComponents(const FToolBuilderState& InputState, const TFunction<bool(UObject*)>& Predicate)
{
	int nTypedComponents = 0;

	if (InputState.SelectedComponents != nullptr && InputState.SelectedComponents->Num() > 0)
	{
		for (FSelectionIterator Iter(*InputState.SelectedComponents); Iter; ++Iter)
		{
			if (Predicate(*Iter))
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




UObject* ToolBuilderUtil::FindFirstComponent(const FToolBuilderState& InputState, const TFunction<bool(UObject*)>& Predicate)
{
	if (InputState.SelectedComponents != nullptr && InputState.SelectedComponents->Num() > 0)
	{
		for (FSelectionIterator Iter(*InputState.SelectedComponents); Iter; ++Iter)
		{
			if (Predicate(*Iter))
			{
				return *Iter;
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




TArray<UObject*> ToolBuilderUtil::FindAllComponents(const FToolBuilderState& InputState, const TFunction<bool(UObject*)>& Predicate)
{
	TArray<UObject*> Components;

	if (InputState.SelectedComponents != nullptr && InputState.SelectedComponents->Num() > 0)
	{
		for (FSelectionIterator Iter(*InputState.SelectedComponents); Iter; ++Iter)
		{
			if (Predicate(*Iter))
			{
				Components.AddUnique(*Iter);
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

