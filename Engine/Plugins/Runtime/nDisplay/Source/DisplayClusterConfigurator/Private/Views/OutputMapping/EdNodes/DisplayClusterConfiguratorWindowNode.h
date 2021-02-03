// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"

#include "DisplayClusterConfiguratorWindowNode.generated.h"

class UDisplayClusterConfigurationClusterNode;
class UDisplayClusterConfiguratorViewportNode;
class UDisplayClusterConfiguratorCanvasNode;
struct FDisplayClusterConfigurationRectangle;

UCLASS()
class UDisplayClusterConfiguratorWindowNode final
	: public UDisplayClusterConfiguratorBaseNode
{
	GENERATED_BODY()

public:
	void Initialize(const FString& InNodeName, UDisplayClusterConfigurationClusterNode* InCfgNode, uint32 InWindowIndex, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin EdGraphNode Interface
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	//~ End EdGraphNode Interface

	virtual void UpdateObject() override;
	virtual void OnNodeAligned(const FVector2D& PositionChange, bool bUpdateChildren = false) override;

	const FDisplayClusterConfigurationRectangle& GetCfgWindowRect() const;
	FString GetCfgHost() const;
	bool IsFixedAspectRatio() const;

	void SetParentCanvas(UDisplayClusterConfiguratorCanvasNode* InParentCanvas);
	UDisplayClusterConfiguratorCanvasNode* GetParentCanvas() const;
	void AddViewportNode(UDisplayClusterConfiguratorViewportNode* ViewportNode);
	const TArray<UDisplayClusterConfiguratorViewportNode*>& GetChildViewports() const;

	void UpdateChildPositions(const FVector2D& Offset);

	FVector2D FindNonOverlappingOffsetFromParent(const FVector2D& InDesiredOffset);
	FVector2D FindNonOverlappingSizeFromParent(const FVector2D& InDesiredSize, const bool bFixedApsectRatio);

private:
	void OnPostEditChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent);

public:
	FLinearColor CornerColor;

private:
	TArray<UDisplayClusterConfiguratorViewportNode*> ChildViewports;
	TWeakObjectPtr<UDisplayClusterConfiguratorCanvasNode> ParentCanvas;
};

