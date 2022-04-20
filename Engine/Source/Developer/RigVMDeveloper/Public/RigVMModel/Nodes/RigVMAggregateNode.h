// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMUnitNode.h"
#include "RigVMCollapseNode.h"
#include "RigVMModel/Nodes/RigVMUnitNode.h"
#include "RigVMAggregateNode.generated.h"

/**
 * The Aggregate Node contains a subgraph of nodes with aggregate pins (2in+1out or 1out+2in) connected
 * to each other. For example, a unit node IntAdd which adds 2 integers and provides Result=A+B could have
 * A, B and Result as aggregates in order to add additional input pins to construct an Aggregate Node that computes
 * Result=A+B+C.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMAggregateNode : public URigVMCollapseNode
{
	GENERATED_BODY()

public:

	// default constructor
	URigVMAggregateNode();

	// URigVMUnitNode interface
	virtual FString GetNodeTitle() const override;
	virtual FName GetMethodName() const;
	virtual  FText GetToolTipText() const override;
	virtual FText GetToolTipTextForPin(const URigVMPin* InPin) const override;

#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	virtual bool IsAggregate() const override { return true; };
#endif
	virtual bool IsInputAggregate() const override;

	URigVMNode* GetFirstInnerNode() const;
	URigVMNode* GetLastInnerNode() const;
	URigVMPin* GetFirstAggregatePin() const;
	URigVMPin* GetSecondAggregatePin() const;
	URigVMPin* GetOppositeAggregatePin() const;

private:

	virtual void InvalidateCache() override;

	mutable URigVMNode* FirstInnerNodeCache;
	mutable URigVMNode* LastInnerNodeCache;

	friend class URigVMController;
};

