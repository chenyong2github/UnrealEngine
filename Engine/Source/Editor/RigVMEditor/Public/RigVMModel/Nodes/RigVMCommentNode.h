// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMCommentNode.generated.h"

/**
 * Comment Nodes can be used to annotate a Graph by adding
 * colored grouping as well as user provided text.
 * Comment Nodes are purely cosmetic and don't contribute
 * to the runtime result of the Graph / Function.
 */
UCLASS(BlueprintType)
class RIGVMEDITOR_API URigVMCommentNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Default constructor
	URigVMCommentNode();

	// Override of node title
	virtual FString GetNodeTitle() const override { return GetCommentText(); }

	// Returns the current user provided text of this comment.
	UFUNCTION(BlueprintCallable, Category = RigVMCommentNode)
	FString GetCommentText() const;

private:

	UPROPERTY()
	FString CommentText;

	friend class URigVMController;
};

