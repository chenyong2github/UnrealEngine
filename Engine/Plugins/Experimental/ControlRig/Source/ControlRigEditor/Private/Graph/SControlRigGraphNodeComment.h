// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNodeComment.h"

class UEdGraphNode_Comment;

class SControlRigGraphNodeComment : public SGraphNodeComment
{
public:

	SControlRigGraphNodeComment();

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void EndUserInteraction() const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

protected:

	virtual bool IsNodeUnderComment(UEdGraphNode_Comment* InCommentNode, const TSharedRef<SGraphNode> InNodeWidget) const override;

private:

	FLinearColor CachedNodeCommentColor;
};
