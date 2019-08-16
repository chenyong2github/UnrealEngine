// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AROriginActor.h"
#include "EngineUtils.h"

AAROriginActor::AAROriginActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	bAllowTickBeforeBeginPlay = false;
	bReplicates = false;
	bReplicateMovement = false;
	bCanBeDamaged = false;
}

AAROriginActor* AAROriginActor::GetOriginActor()
{
	if (GWorld != nullptr)
	{
		AAROriginActor* FoundActor = nullptr;
		for (TActorIterator<AAROriginActor> Iter(GWorld); Iter; ++Iter)
		{
			if (!(*Iter)->IsPendingKill())
			{
				FoundActor = *Iter;
				break;
			}
		}
		if (FoundActor == nullptr)
		{
			// None spawned yet
			FoundActor = GWorld->SpawnActor<AAROriginActor>(AAROriginActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		}
		return FoundActor;
	}
	return nullptr;
}
