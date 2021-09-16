// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Toolkits/AssetEditorModeUILayer.h"

class FStaticMeshEditorModeUILayer : public FAssetEditorModeUILayer
{
public:

	FStaticMeshEditorModeUILayer(const IToolkitHost* InToolkitHost);

	void OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit) override;
	void OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit) override;
	
	TSharedPtr<FWorkspaceItem> GetModeMenuCategory() const override;
};

