// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMLibraryNode.generated.h"

class URigVMGraph;
class URigVMFunctionEntryNode;
class URigVMFunctionReturnNode;

/**
 * The Library Node represents a function invocation of a
 * function specified somewhere else. The function can be 
 * expressed as a sub-graph (RigVMGroupNode) or as a 
 * referenced function (RigVMFunctionNode).
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMLibraryNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Override node functions
	virtual bool IsDefinedAsConstant() const override;
	virtual bool IsDefinedAsVarying() const override;

	// Library node interface
	virtual URigVMGraph* GetContainedGraph() const { return nullptr; }
	virtual TArray<URigVMNode*> GetContainedNodes() const;
	virtual TArray<URigVMLink*> GetContainedLinks() const;
	virtual URigVMFunctionEntryNode* GetEntryNode() const;
	virtual URigVMFunctionReturnNode* GetReturnNode() const;

private:

	UPROPERTY()
	bool bDefinedAsVarying = false;

	friend class URigVMController;
};

