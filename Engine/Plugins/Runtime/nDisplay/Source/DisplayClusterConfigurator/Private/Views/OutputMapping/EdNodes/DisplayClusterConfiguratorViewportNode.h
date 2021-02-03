// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"

#include "DisplayClusterConfiguratorViewportNode.generated.h"

class UDisplayClusterConfigurationViewport;
class UDisplayClusterConfiguratorWindowNode;
struct FDisplayClusterConfigurationRectangle;

class UTexture;


UCLASS(MinimalAPI)
class UDisplayClusterConfiguratorViewportNode final
	: public UDisplayClusterConfiguratorBaseNode
{
	GENERATED_BODY()

public:
	void Initialize(const FString& InViewportName, UDisplayClusterConfigurationViewport* InCfgViewport, UDisplayClusterConfiguratorWindowNode* InParentWindow,  const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin EdGraphNode Interface
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	//~ End EdGraphNode Interface

	virtual void UpdateObject() override;

	const FDisplayClusterConfigurationRectangle& GetCfgViewportRegion() const;
	bool IsFixedAspectRatio() const;

	void SetParentWindow(UDisplayClusterConfiguratorWindowNode* InParentWindow);
	UDisplayClusterConfiguratorWindowNode* GetParentWindow() const;

	void SetPreviewTexture(UTexture* InTexture);
	UTexture* GetPreviewTexture() const;

	bool IsOutsideParent() const;
	bool IsOutsideParentBoundary() const;


	FVector2D FindNonOverlappingOffsetFromParent(const FVector2D& InDesiredOffset);
	FVector2D FindNonOverlappingSizeFromParent(const FVector2D& InDesiredSize, const bool bFixedApsectRatio);

private:
	void OnPostEditChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent);

private:
	TWeakObjectPtr<UDisplayClusterConfiguratorWindowNode> ParentWindow;
	TWeakObjectPtr<UTexture> PreviewTexture;
};
