// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGContext.h"
#include "PCGComponent.h"
#include "GameFramework/Actor.h"

FName FPCGContext::GetTaskName() const
{
	return Node ? Node->GetFName() : TEXT("Anonymous task");
}

FName FPCGContext::GetComponentName() const
{
	return SourceComponent ? SourceComponent->GetOwner()->GetFName() : TEXT("Non-PCG Component");
}