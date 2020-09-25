// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/ICursor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FDisplayClusterConfiguratorToolkit;
class SDisplayClusterConfiguratorBaseNode;

class SDisplayClusterConfiguratorResizer
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorResizer)
	    {}

	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<SDisplayClusterConfiguratorBaseNode>& InBaseNode);

	//~ Begin SWidget Interface
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	//~ End SWidget Interface

private:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TWeakPtr<SDisplayClusterConfiguratorBaseNode> BaseNodePtr;

	bool bResizing;
};

