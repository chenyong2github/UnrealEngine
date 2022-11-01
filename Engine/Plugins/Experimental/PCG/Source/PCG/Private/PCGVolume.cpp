// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGVolume.h"
#include "PCGComponent.h"
#include "PCGGraph.h"

APCGVolume::APCGVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PCGComponent = ObjectInitializer.CreateDefaultSubobject<UPCGComponent>(this, TEXT("PCG Component"));
}

#if WITH_EDITOR
bool APCGVolume::GetReferencedContentObjects(TArray<UObject*>& Objects) const
{
	Super::GetReferencedContentObjects(Objects);

	if (PCGComponent)
    {
    	if (UPCGGraph* PCGGraph = PCGComponent->GetGraph())
    	{
    		Objects.Add(PCGGraph);
    	}
    }
    return true;
}
#endif // WITH_EDITOR
