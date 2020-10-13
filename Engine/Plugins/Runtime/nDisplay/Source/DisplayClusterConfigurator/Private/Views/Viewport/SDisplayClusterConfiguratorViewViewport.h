// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/SDisplayClusterConfiguratorViewBase.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "EditorUndoClient.h"

class FDisplayClusterConfiguratorToolkit;
class SDisplayClusterConfiguratorViewport;
class FDisplayClusterConfiguratorPreviewScene;
class SVerticalBox;


class SDisplayClusterConfiguratorViewViewport
	: public SDisplayClusterConfiguratorViewBase
	, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorViewViewport)
	{ }

	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<FDisplayClusterConfiguratorPreviewScene>& InPreviewScene);

protected:
	//~ Begin FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	//~ End of FEditorUndoClient interface

private:
	/** Viewport widget */
	TSharedPtr<SDisplayClusterConfiguratorViewport> ViewportWidget;
	/** Box that contains notifications */
	TSharedPtr<SVerticalBox> ViewportNotificationsContainer;
	/** Owning toolkit */
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;
};
