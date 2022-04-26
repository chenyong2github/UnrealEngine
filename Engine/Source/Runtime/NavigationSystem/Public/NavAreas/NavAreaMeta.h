// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "NavAreas/NavArea.h"
#include "NavAreaMeta.generated.h"

class AActor;

/** A convenience class for an area that has IsMetaArea() == true.
 *	Do not use this class when determining whether an area class is "meta". 
 *	Call IsMetaArea instead. */
UCLASS(Abstract)
class NAVIGATIONSYSTEM_API UNavAreaMeta : public UNavArea
{
	GENERATED_BODY()

public:
	UNavAreaMeta(const FObjectInitializer& ObjectInitializer);
};
