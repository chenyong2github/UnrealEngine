// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

private:

	FLinearColor CachedNodeCommentColor;
};
