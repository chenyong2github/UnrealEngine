// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMModel/RigVMNotifications.h"

class URigVMGraph;
class URigVMNode;
class URigVMPin;

/**
 * The GraphUtils helper struct can be used to perform
 * analysis on the graph, sort the Nodes by execution 
 * order or determine cycles.
 */
class RIGVMEDITOR_API FRigVMGraphUtils
{
public:

	// Default constructor
	FRigVMGraphUtils();

	// Constructor for an ad-hoc cycle check.
	FRigVMGraphUtils(URigVMGraph* InGraph, URigVMPin* InCycleCheckingPin = nullptr, bool InCycleCheckingPinIsInput = true);

	// Default destructor
	~FRigVMGraphUtils();

	// Returns the Graph used by this utility.
	URigVMGraph* GetGraph() { return Graph;  }

	// Returns the current Pin used to perform cycle checking on.
	URigVMPin* GetCycleCheckPin() { return LastCycleCheckingPin; }

	// Returns true if the Pin for cycle checking was used as an input.
	bool GetCycleCheckPinIsInput() { return LastCycleCheckingPinIsInput; }

	// Resets the utility and fees all memory.
	void Reset();

	// Returns the distance for a given node to the output node farthest away
	int32 GetMaxDistanceToLeafOutput(URigVMNode* InNode) const;

	// Returns true if the graph can be sorted and stores the order of execution in the order array.
	// Return false if there's a cycle in the graph and stores the cycle in the potentialcycle array.
	bool TopologicalSort(TArray<URigVMNode*>& OutOrder, TArray<URigVMNode*>& OutPotentialCycle);

	// Finds the first cycle. Returns an empty array if there's no cycle
	TArray<URigVMNode*> FindCycle();

	// Prepares this utility for cycle checking given a Pin and its usage for a link.
	void PrepareCycleChecking(URigVMPin* InCycleCheckingPin = nullptr, bool InCycleCheckingPinIsInput = true);

	// Returns true if a Node is on the last determined cycle.
	bool IsNodeOnCycle(URigVMNode* InNode);

private:

	URigVMGraph* Graph;
	URigVMPin* LastCycleCheckingPin;
	bool LastCycleCheckingPinIsInput;

	bool IsNodeCyclic(int32 NodeIndex);

	FDelegateHandle ModifiedHandle;
	TSet<int32> CycleWhiteList;
	TSet<int32> CycleGrayList;
	TSet<int32> CycleBlackList;
	TMap<int32, int32> CycleDepthTraversal;
	TArray<URigVMNode*> Cycle;
	TArray<bool> NodeIsOnCycle;
};
