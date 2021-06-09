// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"

#include "Engine/Texture.h"

#include "DisplayClusterConfiguratorViewportNode.generated.h"

class UDisplayClusterConfigurationViewport;
class FDisplayClusterConfiguratorBlueprintEditor;
class FDisplayClusterConfiguratorViewportViewModel;
struct FDisplayClusterConfigurationRectangle;

UCLASS(MinimalAPI)
class UDisplayClusterConfiguratorViewportNode final
	: public UDisplayClusterConfiguratorBaseNode
{
	GENERATED_BODY()

public:
	virtual void Initialize(const FString& InNodeName, int32 InNodeZIndex, UObject* InObject, const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit) override;
	virtual void Cleanup() override;

	//~ Begin EdGraphNode Interface
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	virtual bool CanDuplicateNode() const override { return true; }
	virtual bool CanUserDeleteNode() const override { return true; }
	//~ End EdGraphNode Interface
	
	//~ Begin UDisplayClusterConfiguratorBaseNode Interface
	virtual bool IsNodeVisible() const override;
	virtual bool IsNodeEnabled() const override;
	virtual bool CanNodeOverlapSiblings() const override { return false; }
	virtual bool CanNodeHaveNegativePosition() const { return false; }

	virtual void DeleteObject() override;

protected:
	virtual bool CanAlignWithParent() const override { return true; }
	virtual void WriteNodeStateToObject() override;
	virtual void ReadNodeStateFromObject() override;
	//~ End UDisplayClusterConfiguratorBaseNode Interface

public:
	const FDisplayClusterConfigurationRectangle& GetCfgViewportRegion() const;
	bool IsFixedAspectRatio() const;

	UTexture* GetPreviewTexture() const;

private:
	void OnPostEditChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent);

private:
	TSharedPtr<FDisplayClusterConfiguratorViewportViewModel> ViewportVM;
};
