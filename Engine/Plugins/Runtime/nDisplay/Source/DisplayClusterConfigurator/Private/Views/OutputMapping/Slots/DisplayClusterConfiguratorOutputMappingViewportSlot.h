// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorOutputMappingSlot.h"

#include "Widgets/Layout/SConstraintCanvas.h"
#include "UObject/StrongObjectPtr.h"

class FDisplayClusterConfiguratorOutputMappingBuilder;
class FDisplayClusterConfiguratorToolkit;
class SDisplayClusterConfiguratorViewportNode;
class UDisplayClusterConfigurationViewport;
class UDisplayClusterConfiguratorViewportNode;

struct FPropertyChangedChainEvent;

class FDisplayClusterConfiguratorOutputMappingViewportSlot
	: public IDisplayClusterConfiguratorOutputMappingSlot
{
public:
	FDisplayClusterConfiguratorOutputMappingViewportSlot(
		const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
		const TSharedRef<FDisplayClusterConfiguratorOutputMappingBuilder>& InOutputMappingBuilder,
		const TSharedRef<IDisplayClusterConfiguratorOutputMappingSlot>& InParentSlot,
		const FString& InViewportName,
		UDisplayClusterConfigurationViewport* InConfigurationViewport);

	//~ Begin IDisplayClusterConfiguratorOutputMappingSlot Interface
	virtual void Build() override;

	virtual void AddChild(TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> InChildSlot) override {}

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

	virtual bool IsOutsideParent() const override;

	virtual bool IsOutsideParentBoundary() const override;

	virtual bool IsActive() const override
	{ return bIsActive; }

	virtual UEdGraphNode* GetGraphNode() const override;

	virtual void Tick(float InDeltaTime) override {}

	virtual void SetActive(bool bActive) override;

	virtual void SetOwner(TSharedRef<SGraphPanel> InGraphPanel) override;

	virtual void SetLocalPosition(FVector2D InLocalPosition) override;

	virtual void SetLocalSize(FVector2D InLocalSize) override;

	virtual void SetConfigPosition(FVector2D InConfigPosition) override;

	virtual void SetConfigSize(FVector2D InConfigSize) override;

	virtual void SetZOrder(uint32 InZOrder) override;

	virtual void SetPreviewTexture(UTexture* InTexture) override;
	//~ End IDisplayClusterConfiguratorOutputMappingSlot Interface

private:
	void OnShowOutsideViewports(bool bShow);

	void OnPostEditChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent);

private:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TWeakPtr<FDisplayClusterConfiguratorOutputMappingBuilder> OutputMappingBuilderPtr;

	TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> ParentSlotPtr;

	FString ViewportName;

	TWeakObjectPtr<UDisplayClusterConfigurationViewport> CfgViewportPtr;

	FVector2D LocalSize;

	FVector2D LocalPosition;

	uint32 ZOrder;

	bool bIsActive;

	TSharedPtr<SDisplayClusterConfiguratorViewportNode> NodeWidget;

	SConstraintCanvas::FSlot* NodeSlot;

	TStrongObjectPtr<UDisplayClusterConfiguratorViewportNode> EdGraphViewportNode;

private:
	static uint32 const DefaultZOrder;

	static uint32 const ActiveZOrder;
};
