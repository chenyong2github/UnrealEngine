// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLODBuilderInstancing.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HLODBuilderInstancing)


UHLODBuilderInstancing::UHLODBuilderInstancing(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TArray<UActorComponent*> UHLODBuilderInstancing::Build(const FHLODBuildContext& InHLODBuildContext, const TArray<UActorComponent*>& InSourceComponents) const
{
	return UHLODBuilder::BatchInstances(InSourceComponents);
}

