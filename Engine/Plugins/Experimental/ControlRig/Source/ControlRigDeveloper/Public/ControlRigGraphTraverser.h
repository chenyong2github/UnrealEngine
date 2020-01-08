// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlRigBlueprint.h"
#include "ControlRigModel.h"

/*
 * The ControlRigTraverser is used to walk the UI graph (UEdGraph).
 * During it's traversal it will find all of the nodes wired to an input execution
 * (a BeginExecution unit) and then find all of the pin links within that graph.
 * The resulting links will be stored in the ControlRigBlueprint's PropertyLinks list.
 */
class CONTROLRIGDEVELOPER_API FControlRigGraphTraverser
{
public:

	FControlRigGraphTraverser(UControlRigModel* InModel);

#if WITH_EDITORONLY_DATA
	// Returns true if a given unit is part of a valid execution graph
	bool IsWiredToExecution(const FName& NodeName);
#endif

	// walks the UEdGraph, finds all valid nodes and builds property links
	void TraverseAndBuildPropertyLinks(UControlRigBlueprint* Blueprint);

private:

	// Returns true if a given node is part of a valid execution graph
	bool IsWiredToExecution(const FControlRigModelNode* Node);

	UControlRigModel* Model;
	TMap<FName, bool> VisitedNodes;

	friend class FControlRigBlueprintCompilerContext;
};
