// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/FleshActor.h"


//#include "ChaosFlesh/PB.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(FleshActor)

DEFINE_LOG_CATEGORY_STATIC(AFleshLogging, Log, All);

AFleshActor::AFleshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	//UE_LOG(AFleshLogging, Verbose, TEXT("AFleshActor::AFleshActor()"));
	FleshComponent = CreateDefaultSubobject<UFleshComponent>(TEXT("FleshComponent0"));
	RootComponent = FleshComponent;
	PrimaryActorTick.bCanEverTick = true;
}

#if WITH_EDITOR
bool AFleshActor::GetReferencedContentObjects(TArray<UObject*>& Objects) const
{
	Super::GetReferencedContentObjects(Objects);

	if (FleshComponent && FleshComponent->GetRestCollection())
	{
		Objects.Add(const_cast<UFleshAsset*>(FleshComponent->GetRestCollection()));
	}
	return true;
}
#endif

void AFleshActor::EnableSimulation(ADeformableSolverActor* InActor)
{
	if (InActor)
	{
		if (FleshComponent && FleshComponent->GetRestCollection())
		{
			FleshComponent->EnableSimulation(InActor);
		}
	}
}

