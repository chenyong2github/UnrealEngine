// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SwitchActor.h"

#include "Algo/Sort.h"
#include "CoreMinimal.h"

ASwitchActor::ASwitchActor(const FObjectInitializer& Init)
	: Super(Init)
{
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SceneComponent->SetMobility(EComponentMobility::Static);

	RootComponent = SceneComponent;
}

TArray<AActor*> ASwitchActor::GetOptions() const
{
	TArray<AActor*> Result;
	GetAttachedActors(Result, false);

	// We have to sort these by FName because the attach order is not guaranteed
	// It seems to invert when going into PIE, for example
	Algo::Sort(Result, [](const AActor* LHS, const AActor* RHS)
	{
		return LHS->GetName() < RHS->GetName();
	});

	return Result;
}

int32 ASwitchActor::GetSelectedOption() const
{
	TArray<AActor*> Actors = GetOptions();

	int32 SingleVisibleChildIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		const AActor* Actor = Actors[Index];
		if (USceneComponent* ActorRoot = Actor->GetRootComponent())
		{
			if (ActorRoot->IsVisible())
			{
				if (SingleVisibleChildIndex == INDEX_NONE)
				{
					SingleVisibleChildIndex = Index;
				}
				else
				{
					SingleVisibleChildIndex = INDEX_NONE;
					break;
				}
			}
		}
	}

	return SingleVisibleChildIndex;
}

void ASwitchActor::SelectOption(int32 OptionIndex)
{
	TArray<AActor*> Actors = GetOptions();

	if (!Actors.IsValidIndex(OptionIndex))
	{
		return;
	}

	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		if (AActor* Actor = Actors[Index])
		{
			if (USceneComponent* ActorRootComponent = Actor->GetRootComponent())
			{
				ActorRootComponent->Modify();
				ActorRootComponent->SetVisibility(Index == OptionIndex, true);
			}
		}
	}

	OnSwitchActorSwitch.Broadcast(OptionIndex);
}

FOnSwitchActorSwitch& ASwitchActor::GetOnSwitchDelegate()
{
	return OnSwitchActorSwitch;
}


