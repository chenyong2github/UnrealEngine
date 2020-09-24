// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMFunction.h"

FName FRigVMFunction::GetMethodName() const
{
	FString FullName(Name);
	FString Right;
	if (FullName.Split(TEXT("::"), nullptr, &Right))
	{
		return *Right;
	}
	return NAME_None;
}
