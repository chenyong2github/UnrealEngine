// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "BaseModifierGroup.generated.h"

/** Implements base logic to keep group names unique within a hierachy */
UCLASS(Abstract)
class VCAMEXTENSIONS_API UBaseModifierGroup : public UObject
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = "Hierarchy")
	FName GroupName;

	//~ Begin UObject Interface
	virtual void PostInitProperties() override;
	//~ End UObject Interface

#if WITH_EDITOR
	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif
};
