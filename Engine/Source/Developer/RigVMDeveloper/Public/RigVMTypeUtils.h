// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMDeveloperModule.h"
#include "RigVMCore/RigVMExternalVariable.h"
#include "EdGraphSchema_K2.h"

struct FRigVMGraphVariableDescription;

namespace RigVMTypeUtils
{
	// Returns true if the type specified is an array
	FORCEINLINE bool IsArrayType(const FString& InCPPType)
	{
		return InCPPType.StartsWith(TEXT("TArray<"));
	}

	FORCEINLINE FString ArrayTypeFromBaseType(const FString& InCPPType)
	{
		return "TArray<" + InCPPType + ">";
	}

	
#if WITH_EDITOR
	RIGVMDEVELOPER_API FRigVMExternalVariable ExternalVariableFromBPVariableDescription(const FBPVariableDescription& InVariableDescription);

	RIGVMDEVELOPER_API FRigVMExternalVariable ExternalVariableFromRigVMVariableDescription(const FRigVMGraphVariableDescription& InVariableDescription);
	
	RIGVMDEVELOPER_API FRigVMExternalVariable ExternalVariableFromPinType(const FName& InName, const FEdGraphPinType& InPinType, bool bInPublic = false, bool bInReadonly = false);

	RIGVMDEVELOPER_API FRigVMExternalVariable ExternalVariableFromCPPType(const FName& InName, const FString& InCPPType, bool bInPublic = false, bool bInReadonly = false);

	RIGVMDEVELOPER_API FEdGraphPinType PinTypeFromExternalVariable(const FRigVMExternalVariable& InExternalVariable);

	RIGVMDEVELOPER_API FEdGraphPinType PinTypeFromRigVMVariableDescription(const FRigVMGraphVariableDescription& InVariableDescription);

	RIGVMDEVELOPER_API bool CPPTypeFromPinType(const FEdGraphPinType& InPinType, FString& OutCPPType, UObject** OutCPPTypeObject);

	RIGVMDEVELOPER_API bool CPPTypeFromExternalVariable(const FRigVMExternalVariable& InExternalVariable, FString& OutCPPType, UObject** OutCPPTypeObject);

	
#endif
}
