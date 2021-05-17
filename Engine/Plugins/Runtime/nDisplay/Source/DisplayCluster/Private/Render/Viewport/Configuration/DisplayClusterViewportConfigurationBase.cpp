// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportConfigurationBase.h"
#include "DisplayClusterViewportConfigurationHelpers.h"

///////////////////////////////////////////////////////////////////
// FDisplayClusterViewportConfigurationBase
///////////////////////////////////////////////////////////////////

void FDisplayClusterViewportConfigurationBase::Update(const TArray<FString>& InClusterNodeIds)
{
	TMap<FString, UDisplayClusterConfigurationViewport*> DesiredViewports;

	// Get render viewports
	for (const FString& NodeIt : InClusterNodeIds)
	{
		const UDisplayClusterConfigurationClusterNode* ClusterNode = ConfigurationData.GetClusterNode(NodeIt);
		if (ClusterNode)
		{
			for (const TPair<FString, UDisplayClusterConfigurationViewport*>& ViewportIt : ClusterNode->Viewports)
			{
				if (ViewportIt.Key.Len() && ViewportIt.Value)
				{
					DesiredViewports.Add(ViewportIt.Key, ViewportIt.Value);
				}
			}
		}
	}

	// Collect unused viewports and delete
	{
		TArray<FDisplayClusterViewport*> UnusedViewports;
		for (FDisplayClusterViewport* It : ViewportManager.ImplGetViewports())
		{
			// ignore ICVFX internal resources
			if ((It->GetRenderSettingsICVFX().RuntimeFlags & ViewportRuntime_InternalResource) == 0)
			{
				if (!DesiredViewports.Contains(It->GetId()))
				{
					UnusedViewports.Add(It);
				}
			}
		}

		for (FDisplayClusterViewport* DeleteIt : UnusedViewports)
		{
			ViewportManager.ImplDeleteViewport(DeleteIt);
		}
	}

	// Update and Create new viewports
	for (TPair<FString, UDisplayClusterConfigurationViewport*>& CfgIt : DesiredViewports)
	{
		FDisplayClusterViewport* ExistViewport = ViewportManager.ImplFindViewport(CfgIt.Key);
		if (ExistViewport)
		{
			FDisplayClusterViewportConfigurationBase::UpdateViewportConfiguration(ViewportManager, ExistViewport, CfgIt.Value);
		}
		else
		{
			ViewportManager.CreateViewport(CfgIt.Key, CfgIt.Value);
		}
	}
}

// Assign new configuration to this viewport <Runtime>
bool FDisplayClusterViewportConfigurationBase::UpdateViewportConfiguration(FDisplayClusterViewportManager& ViewportManager, FDisplayClusterViewport* DesiredViewport, const UDisplayClusterConfigurationViewport* ConfigurationViewport)
{
	check(IsInGameThread());
	check(DesiredViewport);
	check(ConfigurationViewport);

	DisplayClusterViewportConfigurationHelpers::UpdateBaseViewportSetting(*DesiredViewport, *ConfigurationViewport);

	bool bNeedUpdateProjectionPolicy = false;

	if (DesiredViewport->ProjectionPolicy.IsValid())
	{
		// Current projection policy valid
		bNeedUpdateProjectionPolicy = DesiredViewport->ProjectionPolicy->IsConfigurationChanged(&ConfigurationViewport->ProjectionPolicy);
	}
	else
	if (DesiredViewport->UninitializedProjectionPolicy.IsValid())
	{
		// Current projection policy valid
		bNeedUpdateProjectionPolicy = DesiredViewport->UninitializedProjectionPolicy->IsConfigurationChanged(&ConfigurationViewport->ProjectionPolicy);
	}

	if (bNeedUpdateProjectionPolicy)
	{
		// Release current projection
		DesiredViewport->HandleEndScene();
		DesiredViewport->UninitializedProjectionPolicy.Reset();

		// Create new projection type interface
		DesiredViewport->UninitializedProjectionPolicy = ViewportManager.CreateProjectionPolicy(DesiredViewport->GetId(), &ConfigurationViewport->ProjectionPolicy);
		DesiredViewport->HandleStartScene();
	}
	else
	{
		DisplayClusterViewportConfigurationHelpers::UpdateProjectionPolicy(*DesiredViewport);
	}

	return true;
}

