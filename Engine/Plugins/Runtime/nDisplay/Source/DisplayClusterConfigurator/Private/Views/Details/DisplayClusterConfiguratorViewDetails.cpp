// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Details/DisplayClusterConfiguratorViewDetails.h"
#include "Views/Details/SDisplayClusterConfiguratorViewDetails.h"
#include "DisplayClusterConfiguratorToolkit.h"

FDisplayClusterConfiguratorViewDetails::FDisplayClusterConfiguratorViewDetails(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: ToolkitPtr(InToolkit)
{
}

TSharedRef<SWidget> FDisplayClusterConfiguratorViewDetails::CreateWidget()
{
	if (!ViewDetails.IsValid())
	{
		 SAssignNew(ViewDetails, SDisplayClusterConfiguratorViewDetails, ToolkitPtr.Pin().ToSharedRef());
	}

	return ViewDetails.ToSharedRef();
}

void FDisplayClusterConfiguratorViewDetails::SetEnabled(bool bInEnabled)
{
	if (ViewDetails.IsValid())
	{
		ViewDetails->SetEnabled(bInEnabled);
	}
}
