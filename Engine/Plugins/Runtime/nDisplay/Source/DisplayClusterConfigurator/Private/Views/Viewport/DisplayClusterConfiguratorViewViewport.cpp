// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Viewport/DisplayClusterConfiguratorViewViewport.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfiguratorPreviewScene.h"
#include "Views/Viewport/SDisplayClusterConfiguratorViewViewport.h"
#include "Views/Viewport/DisplayClusterConfiguratorViewportBuilder.h"

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"


FDisplayClusterConfiguratorViewViewport::FDisplayClusterConfiguratorViewViewport(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: ToolkitPtr(InToolkit)
{
	PreviewScene = MakeShared<FDisplayClusterConfiguratorPreviewScene>(FPreviewScene::ConstructionValues().AllowAudioPlayback(true).ShouldSimulatePhysics(true), InToolkit);

	// Create a builder
	PreviewBuilder = MakeShared<FDisplayClusterConfiguratorViewportBuilder>(InToolkit, PreviewScene.ToSharedRef());
}


TSharedRef<SWidget> FDisplayClusterConfiguratorViewViewport::CreateWidget()
{
	// Register delegates
	ToolkitPtr.Pin()->RegisterOnConfigReloaded(IDisplayClusterConfiguratorToolkit::FOnConfigReloadedDelegate::CreateSP(this, &FDisplayClusterConfiguratorViewViewport::OnConfigReloaded));
	ToolkitPtr.Pin()->GetViewOutputMapping()->RegisterOnOutputMappingBuilt(IDisplayClusterConfiguratorViewOutputMapping::FOnOutputMappingBuiltDelegate::CreateSP(this, &FDisplayClusterConfiguratorViewViewport::OnOutputMappingBuilt));

	if (!ViewViewport.IsValid())
	{
		SAssignNew(ViewViewport, SDisplayClusterConfiguratorViewViewport, ToolkitPtr.Pin().ToSharedRef(), PreviewScene.ToSharedRef());
	}

	return ViewViewport.ToSharedRef();
}

void FDisplayClusterConfiguratorViewViewport::SetEnabled(bool bInEnabled)
{
	if (ViewViewport.IsValid())
	{
		ViewViewport->SetEnabled(bInEnabled);
	}
}

TSharedRef<IDisplayClusterConfiguratorPreviewScene> FDisplayClusterConfiguratorViewViewport::GetPreviewScene() const
{
	return PreviewScene.ToSharedRef();
}

void FDisplayClusterConfiguratorViewViewport::OnConfigReloaded()
{
	PreviewBuilder->BuildViewport();
}

void FDisplayClusterConfiguratorViewViewport::OnOutputMappingBuilt()
{
	PreviewBuilder->UpdateOutputMappingPreview();
}
