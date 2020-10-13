// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Viewport/DisplayClusterConfiguratorViewportBuilder.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfiguratorPreviewScene.h"

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"

#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterPreviewComponent.h"
#include "Engine/TextureRenderTarget2D.h"


FDisplayClusterConfiguratorViewportBuilder::FDisplayClusterConfiguratorViewportBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<FDisplayClusterConfiguratorPreviewScene>& InPreviewScene)
	: ToolkitPtr(InToolkit)
	, PreviewScenePtr(InPreviewScene)
{
}

void FDisplayClusterConfiguratorViewportBuilder::BuildViewport()
{
	ToolkitPtr.Pin()->RegisterOnObjectSelected(IDisplayClusterConfiguratorToolkit::FOnObjectSelectedDelegate::CreateSP(this, &FDisplayClusterConfiguratorViewportBuilder::OnObjectSelected));

	TSharedPtr<FDisplayClusterConfiguratorPreviewScene> PreviewScene = PreviewScenePtr.Pin();
	check(PreviewScene.IsValid());

	UWorld* World = PreviewScene->GetWorld();
	check(World);

	ResetScene(World);

	UDisplayClusterConfigurationData* const Config = ToolkitPtr.Pin()->GetConfig();
	if (Config)
	{
		// Spawn actor
		RootActor = World->SpawnActor<ADisplayClusterRootActor>(ADisplayClusterRootActor::StaticClass(), FTransform::Identity);
		RootActor->SetToolkit(ToolkitPtr);
		RootActor->InitializeFromConfig(ToolkitPtr.Pin()->GetConfig());
		RootActor->SetPreviewNodeId(ADisplayClusterRootActor::PreviewNodeAll);

		ObjectsNameMap = RootActor->GenerateObjectsNamingMap();
	}

	TSharedPtr<FDisplayClusterConfiguratorToolkit> Toolkit = ToolkitPtr.Pin();
	if (Toolkit)
	{
		Toolkit->InvalidateViews();
	}
}

void FDisplayClusterConfiguratorViewportBuilder::ResetScene(UWorld* World)
{
	if (World && RootActor)
	{
		World->DestroyActor(RootActor);
		RootActor = nullptr;
	}
}

void FDisplayClusterConfiguratorViewportBuilder::OnObjectSelected()
{
	const TArray<UObject*>& SelectedObjects = ToolkitPtr.Pin()->GetSelectedObjects();

	for (UObject* const Object : SelectedObjects)
	{
		const FString* const CompId = ObjectsNameMap->Find(Object);
		if (CompId)
		{
			RootActor->SelectComponent(*CompId);
		}
	}

	ToolkitPtr.Pin()->InvalidateViews();
}

void FDisplayClusterConfiguratorViewportBuilder::ClearViewportSelection()
{
	RootActor->SelectComponent(FString());

	TSharedPtr<FDisplayClusterConfiguratorToolkit> Toolkit = ToolkitPtr.Pin();
	if (Toolkit)
	{
		TArray<UObject*> SelectedObjects;
		Toolkit->SelectObjects(SelectedObjects);
		Toolkit->InvalidateViews();
	}
}

void FDisplayClusterConfiguratorViewportBuilder::UpdateOutputMappingPreview()
{
	UDisplayClusterConfigurationData* const Config = ToolkitPtr.Pin()->GetConfig();
	if (Config)
	{
		TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMappingView = ToolkitPtr.Pin()->GetViewOutputMapping();

		for (TPair<FString, UDisplayClusterConfigurationClusterNode*> NodePair : Config->Cluster->Nodes)
		{
			for (TPair<FString, UDisplayClusterConfigurationViewport*> ViewportPair : NodePair.Value->Viewports)
			{
				UDisplayClusterPreviewComponent* PreviewComp = RootActor->GetPreviewComponent(NodePair.Key, ViewportPair.Key);
				if (PreviewComp)
				{
					UTexture* PreviewTexture = PreviewComp->GetRenderTexture();
					if (PreviewTexture)
					{
						OutputMappingView->SetViewportPreviewTexture(NodePair.Key, ViewportPair.Key, PreviewTexture);
					}
				}
			}
		}
	}
}
