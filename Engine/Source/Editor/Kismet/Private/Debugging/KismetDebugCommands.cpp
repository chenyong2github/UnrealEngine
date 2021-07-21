// Copyright Epic Games, Inc. All Rights Reserved.


#include "Debugging/KismetDebugCommands.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Breakpoint.h"
#include "Kismet2/KismetDebugUtilities.h"

//////////////////////////////////////////////////////////////////////////
// FDebuggingActionCallbacks

void FDebuggingActionCallbacks::ClearWatches(UBlueprint* Blueprint)
{
	FKismetDebugUtilities::ClearPinWatches(Blueprint);
}

void FDebuggingActionCallbacks::ClearWatch(UEdGraphPin* WatchedPin)
{
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(WatchedPin->GetOwningNode());
	if (Blueprint != NULL)
	{
		FKismetDebugUtilities::RemovePinWatch(Blueprint, WatchedPin);
	}
}

void FDebuggingActionCallbacks::ClearBreakpoints(UBlueprint* OwnerBlueprint)
{
	FKismetDebugUtilities::ClearBreakpoints(OwnerBlueprint);
}

void FDebuggingActionCallbacks::ClearBreakpoint(TSoftObjectPtr<UEdGraphNode> BreakpointNode, const UBlueprint* OwnerBlueprint)
{
	FKismetDebugUtilities::RemoveBreakpointFromNode(BreakpointNode.Get(), OwnerBlueprint);
}

void FDebuggingActionCallbacks::SetBreakpointEnabled(TSoftObjectPtr<UEdGraphNode> BreakpointNode, const UBlueprint* BreakpointBlueprint, bool bEnabled)
{
	FKismetDebugUtilities::SetBreakpointEnabled(BreakpointNode.Get(), BreakpointBlueprint, bEnabled);
}

void FDebuggingActionCallbacks::SetEnabledOnAllBreakpoints(const UBlueprint* OwnerBlueprint, bool bShouldBeEnabled)
{
	FKismetDebugUtilities::ForeachBreakpoint(
		OwnerBlueprint,
		[bShouldBeEnabled](FBreakpoint& Breakpoint)
		{
			FKismetDebugUtilities::SetBreakpointEnabled(Breakpoint, bShouldBeEnabled);
		}
	);
}
