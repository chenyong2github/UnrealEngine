// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"

#include "ClusterConfiguration/DisplayClusterConfiguratorClusterUtils.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorViewportNode.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorBlueprintEditor.h"
#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterPreviewComponent.h"

void UDisplayClusterConfiguratorViewportNode::Initialize(const FString& InNodeName, UObject* InObject, const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit)
{
	UDisplayClusterConfiguratorBaseNode::Initialize(InNodeName, InObject, InToolkit);

	UDisplayClusterConfigurationViewport* CfgViewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();
	CfgViewport->OnPostEditChangeChainProperty.Add(UDisplayClusterConfigurationViewport::FOnPostEditChangeChainProperty::FDelegate::CreateUObject(this, &UDisplayClusterConfiguratorViewportNode::OnPostEditChangeChainProperty));
}

TSharedPtr<SGraphNode> UDisplayClusterConfiguratorViewportNode::CreateVisualWidget()
{
	return SNew(SDisplayClusterConfiguratorViewportNode, this, ToolkitPtr.Pin().ToSharedRef());;
}

bool UDisplayClusterConfiguratorViewportNode::IsNodeVisible() const
{
	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();
	return Viewport->bIsVisible;
}

bool UDisplayClusterConfiguratorViewportNode::IsNodeEnabled() const
{
	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();
	return Viewport->bIsEnabled;
}

void UDisplayClusterConfiguratorViewportNode::DeleteObject()
{
	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();
	FDisplayClusterConfiguratorClusterUtils::RemoveViewportFromClusterNode(Viewport);
}

void UDisplayClusterConfiguratorViewportNode::WriteNodeStateToObject()
{
	UDisplayClusterConfigurationViewport* CfgViewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();
	const FVector2D LocalPosition = GetNodeLocalPosition();
	const FVector2D LocalSize = TransformSizeToLocal(GetNodeSize());

	CfgViewport->Region.X = LocalPosition.X;
	CfgViewport->Region.Y = LocalPosition.Y;
	CfgViewport->Region.W = LocalSize.X;
	CfgViewport->Region.H = LocalSize.Y;
}

void UDisplayClusterConfiguratorViewportNode::ReadNodeStateFromObject()
{
	const FDisplayClusterConfigurationRectangle& CfgRegion = GetCfgViewportRegion();
	const FVector2D GlobalPosition = TransformPointToGlobal(FVector2D(CfgRegion.X, CfgRegion.Y));
	const FVector2D GlobalSize = TransformSizeToGlobal(FVector2D(CfgRegion.W, CfgRegion.H));

	NodePosX = GlobalPosition.X;
	NodePosY = GlobalPosition.Y;
	NodeWidth = GlobalSize.X;
	NodeHeight = GlobalSize.Y;
}

const FDisplayClusterConfigurationRectangle& UDisplayClusterConfiguratorViewportNode::GetCfgViewportRegion() const
{
	UDisplayClusterConfigurationViewport* CfgViewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();
	return CfgViewport->Region;
}

bool UDisplayClusterConfiguratorViewportNode::IsFixedAspectRatio() const
{
	UDisplayClusterConfigurationViewport* CfgViewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();
	return CfgViewport->bFixedAspectRatio;
}

UTexture* UDisplayClusterConfiguratorViewportNode::GetPreviewTexture() const
{
	TSharedPtr<FDisplayClusterConfiguratorBlueprintEditor> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	if (ADisplayClusterRootActor* RootActor = Cast<ADisplayClusterRootActor>(Toolkit->GetPreviewActor()))
	{
		UDisplayClusterConfiguratorWindowNode* ParentWindow = GetParentChecked<UDisplayClusterConfiguratorWindowNode>();
		if (UDisplayClusterPreviewComponent* PreviewComp = RootActor->GetPreviewComponent(ParentWindow->GetNodeName(), GetNodeName()))
		{
			if (UTexture2D* Texture = PreviewComp->GetOrCreateRenderTexture2D())
			{
				return Texture;
			}
		}
	}

	return nullptr;
}

void UDisplayClusterConfiguratorViewportNode::OnPostEditChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const UDisplayClusterConfigurationViewport* CfgViewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();

	const FName& PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, X) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, Y))
	{
		Modify();

		// Change slots and children position, config object already updated 
		const FVector2D GlobalPosition = TransformPointToGlobal(FVector2D(CfgViewport->Region.X, CfgViewport->Region.Y));
		NodePosX = GlobalPosition.X;
		NodePosY = GlobalPosition.Y;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, W) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, H))
	{
		Modify();

		// Change node slot size, config object already updated

		const FVector2D GlobalSize = TransformSizeToGlobal(FVector2D(CfgViewport->Region.W, CfgViewport->Region.H));
		NodeWidth = GlobalSize.X;
		NodeHeight = GlobalSize.Y;
	}
}