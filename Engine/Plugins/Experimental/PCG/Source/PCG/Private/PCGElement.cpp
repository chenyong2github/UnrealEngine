// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGElement.h"
#include "PCGContext.h"
#include "PCGSettings.h"

bool IPCGElement::Execute(FPCGContextPtr Context) const
{
	const UPCGSettings* Settings = Context->GetInputSettings<UPCGSettings>();
	if (Settings && !Settings->bEnabled)
	{
		//Pass-through
		Context->OutputData = Context->InputData;
		return true;
	}
	else
	{
		return ExecuteInternal(Context);
	}
}

FPCGContextPtr FSimplePCGElement::Initialize(const FPCGDataCollection& InputData, const UPCGComponent* SourceComponent)
{
	FPCGContextPtr Context = MakeShared<FPCGContext>();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;

	return Context;
}