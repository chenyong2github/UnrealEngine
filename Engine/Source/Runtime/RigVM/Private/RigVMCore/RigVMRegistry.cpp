// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMRegistry.h"
#include "UObject/UObjectIterator.h"

FRigVMRegistry FRigVMRegistry::s_RigVMRegistry;

FRigVMRegistry& FRigVMRegistry::Get()
{
	return s_RigVMRegistry;
}

void FRigVMRegistry::Refresh()
{
}

void FRigVMRegistry::Register(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct)
{
	if (Find(InName) != nullptr)
	{
		return;
	}
	Functions.Add(FRigVMFunction(InName, InFunctionPtr, InStruct));
}

FRigVMFunctionPtr FRigVMRegistry::Find(const TCHAR* InName) const
{
	for (const FRigVMFunction& Function : Functions)
	{
		if (FCString::Strcmp(Function.Name, InName) == 0)
		{
			return Function.FunctionPtr;
		}
	}

	return nullptr;
}

const TArray<FRigVMFunction>& FRigVMRegistry::GetFunctions() const
{
	return Functions;
}

