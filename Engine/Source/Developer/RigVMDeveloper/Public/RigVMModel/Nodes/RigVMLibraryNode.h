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
	virtual const TArray<URigVMNode*>& GetContainedNodes() const;
	virtual const TArray<URigVMLink*>& GetContainedLinks() const;
	virtual URigVMFunctionEntryNode* GetEntryNode() const;
	virtual URigVMFunctionReturnNode* GetReturnNode() const;

protected:

	const static TArray<URigVMNode*> EmptyNodes;
	const static TArray<URigVMLink*> EmptyLinks;

private:

	friend class URigVMController;
};

