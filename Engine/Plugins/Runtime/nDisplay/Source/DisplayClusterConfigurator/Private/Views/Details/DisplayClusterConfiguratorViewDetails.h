// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/Details/IDisplayClusterConfiguratorViewDetails.h"

class SDisplayClusterConfiguratorViewDetails;
class FDisplayClusterConfiguratorToolkit;


class FDisplayClusterConfiguratorViewDetails
	: public IDisplayClusterConfiguratorViewDetails
{
public:

	FDisplayClusterConfiguratorViewDetails(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

public:
	//~ Begin IDisplayClusterConfiguratorView Interface
	virtual TSharedRef<SWidget> CreateWidget() override;
	virtual void SetEnabled(bool bInEnabled) override;
	//~ End IDisplayClusterConfiguratorView Interface

private:
	TSharedPtr<SDisplayClusterConfiguratorViewDetails> ViewDetails;

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;
};
