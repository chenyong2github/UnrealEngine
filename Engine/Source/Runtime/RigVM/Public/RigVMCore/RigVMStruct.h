// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMStruct.generated.h"

/**
 * The base class for all RigVM enabled structs.
 */
USTRUCT()
struct RIGVM_API FRigVMStruct
{
	GENERATED_BODY()

	virtual ~FRigVMStruct() {}
	virtual FString ProcessPinLabelForInjection(const FString& InLabel) const { return InLabel; }
	virtual FName DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const { return NAME_None; }

public:

	FORCEINLINE virtual int32 GetMaxArraySize(const FName& InParameterName, const FRigVMUserDataArray& RigVMUserData) { return INDEX_NONE; }
};
