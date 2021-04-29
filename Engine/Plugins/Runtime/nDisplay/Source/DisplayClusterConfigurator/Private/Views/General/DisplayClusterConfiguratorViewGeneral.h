// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/IDisplayClusterConfiguratorView.h"

class FDisplayClusterConfiguratorBlueprintEditor;
class SDisplayClusterConfiguratorViewGeneral;

class FDisplayClusterConfiguratorViewGeneral
	: public IDisplayClusterConfiguratorView
{
public:

	FDisplayClusterConfiguratorViewGeneral(const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit);

public:
	//~ Begin IDisplayClusterConfiguratorView Interface
	virtual TSharedRef<SWidget> CreateWidget() override;
	virtual TSharedRef<SWidget> GetWidget() override;
	virtual void SetEnabled(bool bInEnabled) override;
	//~ End IDisplayClusterConfiguratorView Interface

private:
	TSharedPtr<SDisplayClusterConfiguratorViewGeneral> ViewDetails;

	TWeakPtr<FDisplayClusterConfiguratorBlueprintEditor> ToolkitPtr;
};
