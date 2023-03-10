// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "UObject/ObjectMacros.h"

#include "ValidateVirtualizedContentCommandlet.generated.h"

/**
 * Iterates over all of the packages in a project and identifies which packages contain
 * references to virtualized payloads. The commandlet will then check that all virtualized
 * payloads can be found in persistent storage. Error messages will be logged for
 * packages that contain virtualized payloads that cannot be found in one or more persistent
 * storage backend.
 * 
 * Because the commandlet is the VirtualizationEditor module it needs to be invoked
 * with the command line:
 * -run="VirtualizationEditor.ValidateVirtualizedContent"
 */
UCLASS()
class UValidateVirtualizedContentCommandlet
	: public UCommandlet
{
	GENERATED_UCLASS_BODY()

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

	static int32 StaticMain(const FString& Params);
};
