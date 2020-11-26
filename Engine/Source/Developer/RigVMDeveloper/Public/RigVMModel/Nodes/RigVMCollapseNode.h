// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/Nodes/RigVMLibraryNode.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMCollapseNode.generated.h"

/**
 * The Collapse Node is a library node which stores the 
 * function and its nodes directly within the node itself.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMCollapseNode : public URigVMLibraryNode
{
	GENERATED_BODY()

public:

	// Override node functions
	virtual FString GetNodeTitle() const override;
	virtual FText GetToolTipText() const override;

	// Library node interface
	virtual URigVMGraph* GetContainedGraph() const override { return ContainedGraph; }

private:

	UPROPERTY()
	URigVMGraph* ContainedGraph;

	friend class URigVMController;
};

