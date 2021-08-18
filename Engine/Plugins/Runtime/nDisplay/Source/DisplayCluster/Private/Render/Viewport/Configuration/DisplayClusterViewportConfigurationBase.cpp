// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportConfigurationBase.h"

#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/Postprocess/DisplayClusterViewportPostProcessManager.h"
#include "Render/Viewport/Postprocess/DisplayClusterViewportPostProcessOutputRemap.h"

#include "DisplayClusterViewportConfigurationHelpers.h"
#include "DisplayClusterConfigurationTypes.h"
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
			FDisplayClusterViewportConfigurationBase::UpdateViewportConfiguration(ViewportManager, RootActor, ExistViewport, CfgIt.Value);
		}
		else
		{
			ViewportManager.CreateViewport(CfgIt.Key, CfgIt.Value);
		}
	}
}

void FDisplayClusterViewportConfigurationBase::UpdateClusterNodePostProcess(const FString& InClusterNodeId)
{
	const UDisplayClusterConfigurationClusterNode* ClusterNode = ConfigurationData.GetClusterNode(InClusterNodeId);
	if (ClusterNode)
	{
		TSharedPtr<FDisplayClusterViewportPostProcessManager, ESPMode::ThreadSafe> PPManager = ViewportManager.GetPostProcessManager();
		if (PPManager.IsValid())
		{
			{
				// Find unused PP:
				TArray<FString> UnusedPP;
				for (const FString& It : PPManager->GetPostprocess())
				{
					if (!ClusterNode->Postprocess.Contains(It))
					{
						UnusedPP.Add(It);
					}
				}

				// Delete unused PP:
				for (const FString& It : UnusedPP)
				{
					PPManager->RemovePostprocess(It);
				}
			}

			// Create and update PP
			for (const TPair<FString, FDisplayClusterConfigurationPostprocess>& It : ClusterNode->Postprocess)
			{
				TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe> ExistPostProcess = PPManager->FindPostProcess(It.Key);
				if (ExistPostProcess.IsValid())
				{
					if (ExistPostProcess->IsConfigurationChanged(&It.Value))
					{
						PPManager->UpdatePostprocess(It.Key, &It.Value);
					}
				}
				else
				{
					PPManager->CreatePostprocess(It.Key, &It.Value);
				}
			}

			// Update OutputRemap PP
			{
				const struct FDisplayClusterConfigurationFramePostProcess_OutputRemap& OutputRemapCfg = ClusterNode->OutputRemap;
				if (OutputRemapCfg.bEnable)
				{
					switch (OutputRemapCfg.DataSource)
					{
					case EDisplayClusterConfigurationFramePostProcess_OutputRemapSource::StaticMesh:
						PPManager->GetOutputRemap()->UpdateConfiguration_StaticMesh(OutputRemapCfg.StaticMesh);
						break;

					case EDisplayClusterConfigurationFramePostProcess_OutputRemapSource::ExternalFile:
						PPManager->GetOutputRemap()->UpdateConfiguration_ExternalFile(OutputRemapCfg.ExternalFile);
						break;

					default:
						PPManager->GetOutputRemap()->UpdateConfiguration_Disabled();
					}
				}
				else
				{
					PPManager->GetOutputRemap()->UpdateConfiguration_Disabled();
				}
			}
		}
	}
}

// Assign new configuration to this viewport <Runtime>
bool FDisplayClusterViewportConfigurationBase::UpdateViewportConfiguration(FDisplayClusterViewportManager& ViewportManager, ADisplayClusterRootActor& RootActor, FDisplayClusterViewport* DesiredViewport, const UDisplayClusterConfigurationViewport* ConfigurationViewport)
{
	check(IsInGameThread());
	check(DesiredViewport);
	check(ConfigurationViewport);

	FDisplayClusterViewportConfigurationHelpers::UpdateBaseViewportSetting(*DesiredViewport, RootActor, *ConfigurationViewport);
	FDisplayClusterViewportConfigurationHelpers::UpdateProjectionPolicy(*DesiredViewport, &(ConfigurationViewport->ProjectionPolicy));

	return true;
}

void FDisplayClusterViewportConfigurationBase::UpdateTextureShare(const FString& ClusterNodeId)
{
	for (FDisplayClusterViewport* ViewportIt : ViewportManager.ImplGetViewports())
	{
		if (ViewportIt)
		{
			const UDisplayClusterConfigurationViewport* ViewportCfg = ConfigurationData.GetViewport(ClusterNodeId, ViewportIt->GetId());
			if (ViewportCfg)
			{
				ViewportIt->TextureShare.UpdateConfiguration(*ViewportIt, ViewportCfg->TextureShare);
			}
		}
	}
}

