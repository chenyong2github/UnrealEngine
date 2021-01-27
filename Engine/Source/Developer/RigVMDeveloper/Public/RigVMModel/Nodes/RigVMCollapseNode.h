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

	// Library node interface
	virtual FString GetNodeCategory() const override { return NodeCategory; }
	virtual FString GetNodeKeywords() const override { return NodeKeywords; }
	virtual URigVMFunctionLibrary* GetLibrary() const override;
	virtual URigVMGraph* GetContainedGraph() const override { return ContainedGraph; }

	FString GetEditorSubGraphName() const;

private:

	UPROPERTY()
	URigVMGraph* ContainedGraph;

	UPROPERTY()
	FString NodeCategory;

	UPROPERTY()
	FString NodeKeywords;

	friend class URigVMController;
};

