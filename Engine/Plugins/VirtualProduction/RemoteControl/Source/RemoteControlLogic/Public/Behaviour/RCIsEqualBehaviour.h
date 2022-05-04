// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RCBehaviour.h"
#include "RCIsEqualBehaviour.generated.h"

class URCVirtualPropertySelfContainer;
class URCUserDefinedStruct;

/**
 * Custom behaviour for Is Equal logic
 */
UCLASS()
class REMOTECONTROLLOGIC_API URCIsEqualBehaviour : public URCBehaviour
{
	GENERATED_BODY()

public:
	URCIsEqualBehaviour();
	
	//~ Begin URCBehaviour interface
	virtual void Initialize() override;
	//~ End URCBehaviour interface

public:
	/** Pointer to property container */
	UPROPERTY()
	TObjectPtr<URCVirtualPropertySelfContainer> PropertySelfContainer;
};
