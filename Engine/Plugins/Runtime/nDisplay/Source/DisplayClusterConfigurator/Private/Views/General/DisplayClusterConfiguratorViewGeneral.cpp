// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/General/DisplayClusterConfiguratorViewGeneral.h"
#include "Views/General/SDisplayClusterConfiguratorViewGeneral.h"
#include "DisplayClusterConfiguratorBlueprintEditor.h"

FDisplayClusterConfiguratorViewGeneral::FDisplayClusterConfiguratorViewGeneral(const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit)
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

TSharedRef<SWidget> FDisplayClusterConfiguratorViewGeneral::GetWidget()
{
	return ViewDetails.ToSharedRef();
}

void FDisplayClusterConfiguratorViewGeneral::SetEnabled(bool bInEnabled)
{
	if (ViewDetails.IsValid())
	{
		ViewDetails->SetEnabled(bInEnabled);
	}
}
