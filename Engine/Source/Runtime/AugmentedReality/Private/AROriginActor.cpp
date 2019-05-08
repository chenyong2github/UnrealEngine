// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AROriginActor.h"
#include "EngineUtils.h"

AAROriginActor::AAROriginActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

AAROriginActor* AAROriginActor::GetOriginActor()
{
	if (GWorld != nullptr)
	{
		TActorIterator<AAROriginActor> Iter(GWorld);
		AAROriginActor* FoundActor = *Iter;
		if (FoundActor == nullptr)
		{
			// None spawned yet
			FoundActor = GWorld->SpawnActor<AAROriginActor>(AAROriginActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		}
		return FoundActor;
	}
	return nullptr;
}
