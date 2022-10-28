// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowRenderingActor.h"

#include "CoreMinimal.h"
#include "Dataflow/DataflowRenderingComponent.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowRenderingActor)

DEFINE_LOG_CATEGORY_STATIC(ADataflowLogging, Log, All);

ADataflowRenderingActor::ADataflowRenderingActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	//UE_LOG(ADataflowLogging, Verbose, TEXT("ADataflowRenderingActor::ADataflowRenderingActor()"));
	DataflowRenderingComponent = CreateDefaultSubobject<UDataflowRenderingComponent>(TEXT("DataflowRenderingComponent0"));
	RootComponent = DataflowRenderingComponent;
	PrimaryActorTick.bCanEverTick = true;
}

#if WITH_EDITOR
bool ADataflowRenderingActor::GetReferencedContentObjects(TArray<UObject*>& Objects) const
{
	return Super::GetReferencedContentObjects(Objects);
}
#endif

