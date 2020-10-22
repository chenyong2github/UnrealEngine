// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorOutputMappingSlot.h"

#include "Widgets/Layout/SConstraintCanvas.h"

class FDisplayClusterConfiguratorOutputMappingBuilder;
class FDisplayClusterConfiguratorToolkit;
class SDisplayClusterConfiguratorCanvasNode;
class SBox;
class UDisplayClusterConfigurationCluster;

class FDisplayClusterConfiguratorOutputMappingCanvasSlot
	: public IDisplayClusterConfiguratorOutputMappingSlot
{
public:
	FDisplayClusterConfiguratorOutputMappingCanvasSlot(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
		const TSharedRef<FDisplayClusterConfiguratorOutputMappingBuilder>& InOutputMappingBuilder,
		UDisplayClusterConfigurationCluster* InConfigurationCluster,
		const TSharedRef<SDisplayClusterConfiguratorCanvasNode>& InCanvasNode);

	//~ Begin IDisplayClusterConfiguratorOutputMappingSlot Interface
	virtual void Build() override;

	virtual void AddChild(TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> InChildSlot) override;

	virtual const FVector2D GetLocalPosition() const override;

	virtual const FVector2D GetLocalSize() const override;

	virtual const FVector2D GetConfigSize() const override;
	virtual const FVector2D GetConfigPostion() const override;

	virtual const FString& GetName() const override;

	virtual const FName& GetType() const override;

	virtual const FVector2D CalculateClildLocalPosition(FVector2D RealCoordinate) const override;

	virtual const FVector2D CalculateLocalPosition(FVector2D RealCoordinate) const override;

	virtual const FVector2D CalculateLocalSize(FVector2D RealSize) const override;

	virtual TSharedRef<SWidget> GetWidget() const override;

	virtual uint32 GetZOrder() const override;

	virtual TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> GetParentSlot() const override;

	virtual UObject* GetEditingObject() const override;

	virtual UEdGraphNode* GetGraphNode() const override;

	virtual bool IsOutsideParent() const override;

	virtual bool IsOutsideParentBoundary() const override;

	virtual bool IsActive() const override
	{ return bIsActive; }

	virtual void Tick(float InDeltaTime) override;

	virtual void SetActive(bool bActive) override;

	virtual void SetOwner(TSharedRef<SGraphPanel> InGraphPanel) override {}

	virtual void SetLocalPosition(FVector2D InLocalPosition) override;

	virtual void SetLocalSize(FVector2D InLocalSize) override;

	virtual void SetConfigPosition(FVector2D InConfigPosition) override {}

	virtual void SetConfigSize(FVector2D InConfigSize) override {}

	virtual void SetZOrder(uint32 InZOrder) override;

	virtual void SetPreviewTexture(UTexture* InTexture) override {};
	//~ End IDisplayClusterConfiguratorOutputMappingSlot Interface

private:
	const FSlateBrush* GetSelectedBrush() const;

	FText GetCanvasSizeText() const;

private:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TWeakPtr<FDisplayClusterConfiguratorOutputMappingBuilder> OutputMappingBuilderPtr;

	TWeakObjectPtr<UDisplayClusterConfigurationCluster> CfgClusterPtr;

	TWeakPtr<SDisplayClusterConfiguratorCanvasNode> CanvasNodePtr;

	FVector2D LocalSize;

	FVector2D RealSize;

	FVector2D RealPosition;

	FVector2D LocalPosition;

	uint32 ZOrder;

	float CanvasScaleFactor;

	bool bIsActive;

	TSharedPtr<SWidget> Widget;

	SConstraintCanvas::FSlot* Slot;

	TSharedPtr<SBox> CanvasBox;

	TArray<TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot>> ChildrenSlots;

private:
	static uint32 const DefaultZOrder;
};
