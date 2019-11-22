// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "DataprepParameterizableObject.generated.h"

struct FPropertyChangedChainEvent;

/**
 * The base class of all the object that can interact with the dataprep parameterization
 * This include all the parameterizable object and the parameterization object itself
 */
UCLASS()
class DATAPREPCORE_API UDataprepParameterizableObject : public UObject
{
public:
	GENERATED_BODY()

	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
};
