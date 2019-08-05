// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MultiplexRegistry.h"
#include "UObject/UObjectIterator.h"

FMultiplexRegistry FMultiplexRegistry::s_MultiplexRegistry;

FMultiplexRegistry& FMultiplexRegistry::Get()
{
	return s_MultiplexRegistry;
}

void FMultiplexRegistry::Refresh()
{
}

void FMultiplexRegistry::Register(const TCHAR* InName, FMultiplexFunctionPtr InFunctionPtr)
{
	if (Find(InName) != nullptr)
	{
		return;
	}
	Functions.Add(FMultiplexFunction(InName, InFunctionPtr));
}

FMultiplexFunctionPtr FMultiplexRegistry::Find(const TCHAR* InName) const
{
	for (const FMultiplexFunction& Function : Functions)
	{
		if (FCString::Strcmp(Function.Name, InName) == 0)
		{
			return Function.FunctionPtr;
		}
	}

	return nullptr;
}
