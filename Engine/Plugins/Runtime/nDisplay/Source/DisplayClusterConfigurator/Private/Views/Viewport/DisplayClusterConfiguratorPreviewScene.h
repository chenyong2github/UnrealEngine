// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/Viewport/IDisplayClusterConfiguratorPreviewScene.h"
#include "EditorUndoClient.h"

class FDisplayClusterConfiguratorToolkit;


class FDisplayClusterConfiguratorPreviewScene
	: public IDisplayClusterConfiguratorPreviewScene
	, public FEditorUndoClient
{
public:
	FDisplayClusterConfiguratorPreviewScene(const ConstructionValues& CVS, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

public:
	//~ Begin FPreviewScene interface 
	virtual void Tick(float InDeltaTime) override;
	//~ End FPreviewScene interface

private:

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;
};
