// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMLibraryNode.generated.h"

class URigVMGraph;
class URigVMFunctionEntryNode;
class URigVMFunctionReturnNode;
class URigVMFunctionLibrary;

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
	virtual TArray<int32> GetInstructionsForVM(URigVM* InVM, const FRigVMASTProxy& InProxy = FRigVMASTProxy()) const override; 
	virtual int32 GetInstructionVisitedCount(URigVM* InVM, const FRigVMASTProxy& InProxy = FRigVMASTProxy(), bool bConsolidatePerNode = false) const override; 

	// Library node interface
	virtual FString GetNodeCategory() const { return FString(); }
	virtual FString GetNodeKeywords() const { return FString(); }
	virtual URigVMFunctionLibrary* GetLibrary() const { return nullptr; }
	virtual URigVMGraph* GetContainedGraph() const { return nullptr; }
	virtual const TArray<URigVMNode*>& GetContainedNodes() const;
	virtual const TArray<URigVMLink*>& GetContainedLinks() const;
	virtual URigVMFunctionEntryNode* GetEntryNode() const;
	virtual URigVMFunctionReturnNode* GetReturnNode() const;
	virtual bool Contains(URigVMLibraryNode* InContainedNode, bool bRecursive = true) const;
	virtual TArray<FRigVMExternalVariable> GetExternalVariables() const;

protected:

	const static TArray<URigVMNode*> EmptyNodes;
	const static TArray<URigVMLink*> EmptyLinks;

private:

	friend class URigVMController;
};

