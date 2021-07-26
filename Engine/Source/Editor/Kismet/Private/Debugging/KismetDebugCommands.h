// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

//////////////////////////////////////////////////////////////////////////
// FDebuggingActionCallbacks

struct FBlueprintBreakpoint;
class UBlueprint;
class UEdGraphNode;
class UEdGraphPin;

class FDebuggingActionCallbacks
{
public:
	static void ClearWatches(UBlueprint* Blueprint);
	static void ClearWatch(UEdGraphPin* WatchedPin);
	static void ClearBreakpoints(UBlueprint* OwnerBlueprint);
	static void ClearBreakpoint(TSoftObjectPtr<UEdGraphNode> BreakpointNode, const UBlueprint* OwnerBlueprint);
	static void SetBreakpointEnabled(TSoftObjectPtr<UEdGraphNode> BreakpointNode, const UBlueprint* BreakpointBlueprint, bool bEnabled);
	static void SetEnabledOnAllBreakpoints(const UBlueprint* OwnerBlueprint, bool bShouldBeEnabled);
};

//////////////////////////////////////////////////////////////////////////
