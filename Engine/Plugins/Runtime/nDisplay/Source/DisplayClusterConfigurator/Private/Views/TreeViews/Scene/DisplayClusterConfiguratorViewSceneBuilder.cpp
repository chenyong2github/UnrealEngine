// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Scene/DisplayClusterConfiguratorViewSceneBuilder.h"

#include "DisplayClusterConfiguratorEditorData.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"
#include "Views/TreeViews/Scene/TreeItems/DisplayClusterConfiguratorTreeItemScene.h"

FDisplayClusterConfiguratorViewSceneBuilder::FDisplayClusterConfiguratorViewSceneBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: FDisplayClusterConfiguratorTreeBuilder(InToolkit)
{
}

void FDisplayClusterConfiguratorViewSceneBuilder::Build(FDisplayClusterConfiguratorTreeBuilderOutput& Output)
{
	if (UDisplayClusterConfiguratorEditorData* EditorDataPtr = ConfiguratorTreePtr.Pin()->GetEditorData())
	{
		if (UDisplayClusterConfigurationData* Config = ToolkitPtr.Pin()->GetConfig())
		{
			if (Config->Scene != nullptr)
			{
				AddScene(Output, Config, Config->Scene);
			}
		}
	}
}

void FDisplayClusterConfiguratorViewSceneBuilder::AddScene(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, UDisplayClusterConfigurationScene* InObjectToEdit)
{
	// Helper items structure
	struct FComponentData
	{
		FComponentData(const FName& InParentId, const FName& InParentType, const FName& InNodeName, UObject* InObjectToEdit, const FString& InNodeStyle)
			: ParentId(InParentId)
			, ParentType(InParentType)
			, NodeName(InNodeName)
			, ObjectToEdit(InObjectToEdit)
			, NodeStyle(InNodeStyle)
			, bSorted(false)
		{}

		FComponentData()
			: ObjectToEdit(nullptr)
			, bSorted(false)
		{}

		FName ParentId;
		FName ParentType;
		FName NodeName;
		UObject* ObjectToEdit;
		FString NodeStyle;
		bool bSorted;
	};

	TArray<FComponentData> UnsordedComponents;

	// Add Root node
	FName ParentName = NAME_None;
	const FName RootNodeName = "Scene";
	TSharedRef<IDisplayClusterConfiguratorTreeItem> RootNode = MakeShared<FDisplayClusterConfiguratorTreeItemScene>(RootNodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.Scene", true);
	Output.Add(RootNode, ParentName, FDisplayClusterConfiguratorTreeItem::GetTypeId());

	const TMap<FString, UDisplayClusterConfigurationSceneComponentXform*>& Xforms = InConfig->Scene->Xforms;
	for (const TPair<FString, UDisplayClusterConfigurationSceneComponentXform*>& XformPair : Xforms)
	{
		UDisplayClusterConfigurationSceneComponentXform* Component = XformPair.Value;
		UnsordedComponents.Add(FComponentData(*Component->ParentId, FDisplayClusterConfiguratorTreeItemScene::GetTypeId(), *XformPair.Key, Component, "DisplayClusterConfigurator.TreeItems.SceneComponentXform"));
	}

	const TMap<FString, UDisplayClusterConfigurationSceneComponentCamera*>& Cameras = InConfig->Scene->Cameras;
	for (const TPair<FString, UDisplayClusterConfigurationSceneComponentCamera*>& CameraPair : Cameras)
	{
		UDisplayClusterConfigurationSceneComponentCamera* Component = CameraPair.Value;
		UnsordedComponents.Add(FComponentData(*Component->ParentId, FDisplayClusterConfiguratorTreeItemScene::GetTypeId(), *CameraPair.Key, Component, "DisplayClusterConfigurator.TreeItems.SceneComponentCamera"));
	}

	const TMap<FString, UDisplayClusterConfigurationSceneComponentScreen*>& Screens = InConfig->Scene->Screens;
	for (const TPair<FString, UDisplayClusterConfigurationSceneComponentScreen*>& ScreenPair : Screens)
	{
		UDisplayClusterConfigurationSceneComponentScreen* Component = ScreenPair.Value;
		UnsordedComponents.Add(FComponentData(*Component->ParentId, FDisplayClusterConfiguratorTreeItemScene::GetTypeId(), *ScreenPair.Key, Component, "DisplayClusterConfigurator.TreeItems.SceneComponentScreen"));
	}

	const TMap<FString, UDisplayClusterConfigurationSceneComponentMesh*>& Meshes = InConfig->Scene->Meshes;
	for (const TPair<FString, UDisplayClusterConfigurationSceneComponentMesh*>& MeshPair : Meshes)
	{
		UDisplayClusterConfigurationSceneComponentMesh* Component = MeshPair.Value;
		UnsordedComponents.Add(FComponentData(*Component->ParentId, FDisplayClusterConfiguratorTreeItemScene::GetTypeId(), *MeshPair.Key, Component, "DisplayClusterConfigurator.TreeItems.SceneComponentMesh"));
	}

	// Sort the array in order to have valid parents for any tree item
	for (FComponentData& UnsordedComponent : UnsordedComponents)
	{
		// If already sorded, just do nothing
		if (UnsordedComponent.bSorted == false)
		{
			// Add all scene parents into the array
			FName ParentComponentName = UnsordedComponent.ParentId;
			TArray<FComponentData*> ParentComponents;
			while (ParentComponentName != NAME_None)
			{
				// Try to find parent
				FComponentData* ParentInUnsordedComponents = UnsordedComponents.FindByPredicate([this, &ParentComponentName](const FComponentData& InComponent)
				{
					return InComponent.NodeName == ParentComponentName;
				});

				// If we got a parent and it has not sorted scene component then add it to the parent array and go to the next iteration
				if (ParentInUnsordedComponents != nullptr && ParentInUnsordedComponents->bSorted == false)
				{
					ParentComponents.Add(ParentInUnsordedComponents);
					ParentComponentName = ParentInUnsordedComponents->ParentId;
				}
				else
				{
					// If the parent is empty then we rich top component and we can break the loop
					break;
				}
			}

			// Before add the component we need to add all parents
			for (int32 ParentIndexMax = ParentComponents.Num(); ParentIndexMax > 0; ParentIndexMax--)
			{
				FComponentData* ParentComponent = ParentComponents[ParentIndexMax - 1];
				
				ParentComponent->bSorted = true;
				FName ParentNodeName = (ParentComponent->ParentId == NAME_None) ? RootNodeName : ParentComponent->ParentId;

				TSharedRef<IDisplayClusterConfiguratorTreeItem> ConfiguratorTreeItemNode = MakeShared<FDisplayClusterConfiguratorTreeItemScene>(ParentComponent->NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), ParentComponent->ObjectToEdit, ParentComponent->NodeStyle);
				Output.Add(ConfiguratorTreeItemNode, ParentNodeName, ParentComponent->ParentType);
			}

			// Add component itself
			{
				UnsordedComponent.bSorted = true;
				FName ParentNodeName = (UnsordedComponent.ParentId == NAME_None) ? RootNodeName : UnsordedComponent.ParentId;
				TSharedRef<IDisplayClusterConfiguratorTreeItem> ConfiguratorTreeItemNode = MakeShared<FDisplayClusterConfiguratorTreeItemScene>(UnsordedComponent.NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), UnsordedComponent.ObjectToEdit, UnsordedComponent.NodeStyle);
				Output.Add(ConfiguratorTreeItemNode, ParentNodeName, UnsordedComponent.ParentType);
			}
		}
	}
}
