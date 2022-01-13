// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGElement.h"
#include "PCGContext.h"

FPCGContextPtr FSimplePCGElement::Initialize(const FPCGDataCollection& InputData, const UPCGComponent* SourceComponent)
{
	FPCGContextPtr Context = MakeShared<FPCGContext>();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;

	return Context;
}