// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/DisplayClusterConfiguratorViewOutputMapping.h"

#include "DisplayClusterConfiguratorToolkit.h"

#include "Views/OutputMapping/DisplayClusterConfiguratorGraph.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorGraphSchema.h"
#include "Views/OutputMapping/SDisplayClusterConfiguratorViewOutputMapping.h"
#include "Views/OutputMapping/SDisplayClusterConfiguratorGraphEditor.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorViewOutputMapping"

FDisplayClusterConfiguratorViewOutputMapping::FDisplayClusterConfiguratorViewOutputMapping(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: ToolkitPtr(InToolkit)
	, bShowWindowInfo(false)
	, bShowWindowCornerImage(true)
	, bShowOutsideViewports(false)
{
	// Create the graph
	FName UniqueGraphName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), FName(*(LOCTEXT("DisplayClusterConfiguratorGraph", "Graph").ToString())));
	GraphObj = TStrongObjectPtr<UDisplayClusterConfiguratorGraph>(NewObject< UDisplayClusterConfiguratorGraph >(GetTransientPackage(), UniqueGraphName));
	GraphObj->Schema = UDisplayClusterConfiguratorGraphSchema::StaticClass();
	GraphObj->Initialize(InToolkit);
}

TSharedRef<SWidget> FDisplayClusterConfiguratorViewOutputMapping::CreateWidget()
{
	if (!ViewOutputMapping.IsValid())
	{
		GraphEditor = SNew(SDisplayClusterConfiguratorGraphEditor, ToolkitPtr.Pin().ToSharedRef(), SharedThis(this))
			.GraphToEdit(GraphObj.Get());

		 SAssignNew(ViewOutputMapping, SDisplayClusterConfiguratorViewOutputMapping, ToolkitPtr.Pin().ToSharedRef(), GraphEditor.ToSharedRef(), SharedThis(this));
	}

	return ViewOutputMapping.ToSharedRef();
}

void FDisplayClusterConfiguratorViewOutputMapping::SetEnabled(bool bInEnabled)
{
	if (GraphEditor.IsValid())
	{
		GraphEditor->SetEnabled(bInEnabled);
	}
}

bool FDisplayClusterConfiguratorViewOutputMapping::IsRulerVisible() const
{
	return true;
}

FDelegateHandle FDisplayClusterConfiguratorViewOutputMapping::RegisterOnShowWindowInfo(const FOnShowWindowInfoDelegate& Delegate)
{
	return OnShowWindowInfo.Add(Delegate);
}

void FDisplayClusterConfiguratorViewOutputMapping::UnregisterOnShowWindowInfo(FDelegateHandle DelegateHandle)
{
	OnShowWindowInfo.Remove(DelegateHandle);
}

FDelegateHandle FDisplayClusterConfiguratorViewOutputMapping::RegisterOnShowWindowCornerImage(const FOnShowWindowCornerImageDelegate& Delegate)
{
	return OnShowWindowCornerImage.Add(Delegate);
}

void FDisplayClusterConfiguratorViewOutputMapping::UnregisterOnShowWindowCornerImage(FDelegateHandle DelegateHandle)
{
	OnShowWindowCornerImage.Remove(DelegateHandle);
}

FDelegateHandle FDisplayClusterConfiguratorViewOutputMapping::RegisterOnShowOutsideViewports(const FOnShowOutsideViewportsDelegate& Delegate)
{
	return OnShowOutsideViewports.Add(Delegate);
}

void FDisplayClusterConfiguratorViewOutputMapping::UnregisterOnShowOutsideViewports(FDelegateHandle DelegateHandle)
{
	OnShowOutsideViewports.Remove(DelegateHandle);
}

FDelegateHandle FDisplayClusterConfiguratorViewOutputMapping::RegisterOnOutputMappingBuilt(const FOnOutputMappingBuiltDelegate& Delegate)
{
	return OnOutputMappingBuilt.Add(Delegate);
}

void FDisplayClusterConfiguratorViewOutputMapping::UnregisterOnOutputMappingBuilt(FDelegateHandle DelegateHandle)
{
	OnOutputMappingBuilt.Remove(DelegateHandle);
}

void FDisplayClusterConfiguratorViewOutputMapping::SetViewportPreviewTexture(const FString& NodeId, const FString& ViewportId, UTexture* InTexture)
{
	if (GraphEditor.IsValid())
	{
		GraphEditor->SetViewportPreviewTexture(NodeId, ViewportId, InTexture);
	}
}

void FDisplayClusterConfiguratorViewOutputMapping::ToggleShowWindowInfo()
{
	bShowWindowInfo = !bShowWindowInfo;

	OnShowWindowInfo.Broadcast(bShowWindowInfo);
}
void FDisplayClusterConfiguratorViewOutputMapping::ToggleShowWindowCornerImage()
{
	bShowWindowCornerImage  = !bShowWindowCornerImage;

	OnShowWindowCornerImage.Broadcast(bShowWindowCornerImage);
}

void FDisplayClusterConfiguratorViewOutputMapping::ToggleShowOutsideViewports()
{
	bShowOutsideViewports = !bShowOutsideViewports;

	OnShowOutsideViewports.Broadcast(bShowOutsideViewports);
}

#undef LOCTEXT_NAMESPACE