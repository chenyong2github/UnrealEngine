// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorOutputMappingSlot.h"

#include "Widgets/Layout/SConstraintCanvas.h"
#include "UObject/StrongObjectPtr.h"

class FDisplayClusterConfiguratorToolkit;
class FDisplayClusterConfiguratorOutputMappingBuilder;
class SDisplayClusterConfiguratorWindowNode;
class UDisplayClusterConfigurationClusterNode;
class UDisplayClusterConfiguratorWindowNode;

struct FPropertyChangedChainEvent;

class FDisplayClusterConfiguratorOutputMappingWindowSlot
	: public IDisplayClusterConfiguratorOutputMappingSlot
{
public:
	FDisplayClusterConfiguratorOutputMappingWindowSlot(
		const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
		const TSharedRef<FDisplayClusterConfiguratorOutputMappingBuilder>& InOutputMappingBuilder,
		const TSharedRef<IDisplayClusterConfiguratorOutputMappingSlot>& InParentSlot,
		const FString& InNodeName,
		UDisplayClusterConfigurationClusterNode* InConfigurationClusterNode,
		uint32 InWindowIndex);

	//~ Begin IDisplayClusterConfiguratorOutputMappingSlot Interface
	virtual void Build() override;

	virtual void AddChild(TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> InChildSlot) override;

	virtual const FVector2D GetConfigSize() const override;

	virtual const FVector2D GetLocalPosition() const override;

	virtual const FVector2D GetLocalSize() const override;

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

	virtual void SetPreviewTexture(UTexture* InTexture) override {};
	//~ End IDisplayClusterConfiguratorOutputMappingSlot Interface

private:
	void OnShowWindowInfo(bool IsShow);

	void OnShowWindowCornerImage(bool IsShow);

	void OnPostEditChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent);

	void UpdateSlotsPositionInternal(FVector2D InLocalPosition, FVector2D InDeltaPosition);

	EVisibility GetInfoWidgetVisibility() const;

	EVisibility GetCornerImageVisibility() const;

private:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TWeakPtr<FDisplayClusterConfiguratorOutputMappingBuilder> OutputMappingBuilderPtr;

	TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> ParentSlotPtr;

	FString NodeName;

	TWeakObjectPtr<UDisplayClusterConfigurationClusterNode> CfgClusterNodePtr;

	uint32 WindowIndex;

	FVector2D LocalSize;

	FVector2D LocalPosition;

	uint32 ZOrder;

	float WindowXFactror;

	float WindowYFactror;

	bool bIsActive;

	TSharedPtr<SDisplayClusterConfiguratorWindowNode> NodeWidget;

	TSharedPtr<SWidget> CornerImageWidget;

	TSharedPtr<SWidget> InfoWidget;

	SConstraintCanvas::FSlot* NodeSlot;

	SConstraintCanvas::FSlot* CornerImageSlot;

	SConstraintCanvas::FSlot* InfoSlot;

	TStrongObjectPtr<UDisplayClusterConfiguratorWindowNode> EdGraphWindowNode;

	TArray<TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot>> ChildrenSlots;

	bool bShowWindowInfoVisible;

	bool bShowWindowCornerImageVisible;

private:
	static uint32 const DefaultZOrder;

	static uint32 const ActiveZOrder;

	static uint32 const InfoSlotZOrderOffset;

	static uint32 const CornerImageSlotZOrderOffset;
};
