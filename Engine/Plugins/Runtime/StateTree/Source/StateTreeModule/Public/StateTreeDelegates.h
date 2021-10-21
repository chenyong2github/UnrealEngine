// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UStateTree;

namespace UE::StateTree::Delegates
{
	
#if WITH_EDITOR
	// Called when linkable name in a StateTree has changed.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnIdentifierChanged, const UStateTree& /*StateTree*/);
	extern STATETREEMODULE_API FOnIdentifierChanged OnIdentifierChanged;

	// Called when schema of the StateTree has changed.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSchemaChanged, const UStateTree& /*StateTree*/);
	extern STATETREEMODULE_API FOnSchemaChanged OnSchemaChanged;
#endif

}; // UE::StateTree::Delegates

