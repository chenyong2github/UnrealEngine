// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlRigBlueprint.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"

/*
 * The ControlRigTraverser is used to walk the UI graph (UEdGraph).
 * During it's traversal it will find all of the nodes wired to an input execution
 * (a BeginExecution unit) and then find all of the pin links within that graph.
 * The resulting links will be stored in the ControlRigBlueprint's PropertyLinks list.
 */
class CONTROLRIGDEVELOPER_API FControlRigGraphTraverser
{
public:

	FControlRigGraphTraverser(UControlRigBlueprint* InBlueprint, UControlRigGraph* InGraph);

#if WITH_EDITOR
	// Returns true if a given unit is part of a valid execution graph
	bool IsWiredToExecution(const FName& UnitName);
#endif

	// walks the UEdGraph, finds all valid nodes and builds property links
	void TraverseAndBuildPropertyLinks();

private:

	// Returns true if a given node is part of a valid execution graph
	bool IsWiredToExecution(UControlRigGraphNode* Node);

	UControlRigBlueprint* Blueprint;
	UControlRigGraph* Graph;
	TMap<FName, bool> VisitedNodes;

	friend class FControlRigBlueprintCompilerContext;
};
