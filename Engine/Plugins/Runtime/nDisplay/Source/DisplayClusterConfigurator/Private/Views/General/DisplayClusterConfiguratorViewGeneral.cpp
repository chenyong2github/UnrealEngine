// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/General/DisplayClusterConfiguratorViewGeneral.h"
#include "Views/General/SDisplayClusterConfiguratorViewGeneral.h"
#include "DisplayClusterConfiguratorToolkit.h"

FDisplayClusterConfiguratorViewGeneral::FDisplayClusterConfiguratorViewGeneral(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: ToolkitPtr(InToolkit)
{
}

TSharedRef<SWidget> FDisplayClusterConfiguratorViewGeneral::CreateWidget()
{
	if (!ViewDetails.IsValid())
	{
		 SAssignNew(ViewDetails, SDisplayClusterConfiguratorViewGeneral, ToolkitPtr.Pin().ToSharedRef());
	}

	return ViewDetails.ToSharedRef();
}

void FDisplayClusterConfiguratorViewGeneral::SetEnabled(bool bInEnabled)
{
	if (ViewDetails.IsValid())
	{
		ViewDetails->SetEnabled(bInEnabled);
	}
}
