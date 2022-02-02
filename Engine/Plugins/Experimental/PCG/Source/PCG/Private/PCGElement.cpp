// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGElement.h"
#include "PCGContext.h"
#include "PCGSettings.h"
#include "Elements/PCGDebugElement.h"

bool IPCGElement::Execute(FPCGContextPtr Context) const
{
	// Early out to stop execution
	if (Context->InputData.bCancelExecution)
	{
		Context->OutputData.bCancelExecution = true;

		if (IsCancellable())
		{
			return true;
		}
	}

	const UPCGSettings* Settings = Context->GetInputSettings<UPCGSettings>();
	if (Settings && Settings->ExecutionMode == EPCGSettingsExecutionMode::Disabled)
	{
		//Pass-through
		Context->OutputData = Context->InputData;
		return true;
	}
	else
	{
		bool bDone = ExecuteInternal(Context);

#if WITH_EDITOR
		if (bDone && Settings && (Settings->ExecutionMode == EPCGSettingsExecutionMode::Debug || Settings->ExecutionMode == EPCGSettingsExecutionMode::Isolated))
		{
			PCGDebugElement::ExecuteDebugDisplay(Context);

			// Null out the output if this node is executed in isolation
			if (Settings->ExecutionMode == EPCGSettingsExecutionMode::Isolated)
			{
				Context->OutputData.bCancelExecution = true;
			}
		}
#endif

		return bDone;
	}
}

FPCGContextPtr FSimplePCGElement::Initialize(const FPCGDataCollection& InputData, const UPCGComponent* SourceComponent)
{
	FPCGContextPtr Context = MakeShared<FPCGContext>();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;

	return Context;
}