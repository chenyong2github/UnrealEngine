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

void FRigVMRegistry::Register(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr)
{
	if (Find(InName) != nullptr)
	{
		return;
	}
	Functions.Add(FRigVMFunction(InName, InFunctionPtr));
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
