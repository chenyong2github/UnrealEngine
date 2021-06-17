// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNodeFunctionRef.h"

void FAnimNodeFunctionRef::Initialize(const UClass* InClass)
{
	if(FunctionName != NAME_None)
	{
		Function = InClass->FindFunctionByName(FunctionName);
	}
}

void FAnimNodeFunctionRef::Call(UObject* InObject, void* InParameters) const
{
	if(IsValid())
	{
		InObject->ProcessEvent(Function, InParameters);
	}
}