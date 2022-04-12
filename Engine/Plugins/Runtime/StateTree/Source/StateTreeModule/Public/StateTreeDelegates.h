// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"

class UStateTree;

namespace UE::StateTree::Delegates
{
	
#if WITH_EDITOR
	/** Called when linkable name in a StateTree has changed. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnIdentifierChanged, const UStateTree& /*StateTree*/);
	extern STATETREEMODULE_API FOnIdentifierChanged OnIdentifierChanged;

	/**
	 * Called when schema of the StateTree EditorData has changed.
	 * This is used to refresh the asset editor.
	 * Note that this is NOT called when updating the StateTree schema from the EditorData on successful compilation.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSchemaChanged, const UStateTree& /*StateTree*/);
	extern STATETREEMODULE_API FOnSchemaChanged OnSchemaChanged;

	/**
	 * Called when parameters of the StateTree EditorData changed.
	 * This should mainly used by the asset editor to maintain consistency in the UI for manipulations on the EditorData
	 * until the tree gets compiled.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnParametersChanged, const UStateTree& /*StateTree*/);
	extern STATETREEMODULE_API FOnParametersChanged OnParametersChanged;
	
	/** Called when compilation succeeds */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostCompile, const UStateTree& /*StateTree*/);
	extern STATETREEMODULE_API FOnPostCompile OnPostCompile;
#endif

}; // UE::StateTree::Delegates

