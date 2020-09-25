// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/IDisplayClusterConfiguratorView.h"

class FDisplayClusterConfiguratorToolkit;
class SDisplayClusterConfiguratorViewGeneral;

class FDisplayClusterConfiguratorViewGeneral
	: public IDisplayClusterConfiguratorView
{
public:

	FDisplayClusterConfiguratorViewGeneral(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

public:
	//~ Begin IDisplayClusterConfiguratorView Interface
	TSharedRef<SWidget> CreateWidget() override;
	//~ End IDisplayClusterConfiguratorView Interface

private:
	TSharedPtr<SDisplayClusterConfiguratorViewGeneral> ViewDetails;

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;
};
