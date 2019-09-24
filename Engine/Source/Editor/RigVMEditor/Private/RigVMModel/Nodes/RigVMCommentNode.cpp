// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMCommentNode.h"

URigVMCommentNode::URigVMCommentNode()
{
	Size = FVector2D(400.f, 300.f);
}

FString URigVMCommentNode::GetCommentText() const
{
	return CommentText;
}
