// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorOutputMappingItem.h"
#include "EdGraph/EdGraphNode.h"

#include "DisplayClusterConfiguratorBaseNode.generated.h"

class FDisplayClusterConfiguratorToolkit;
class IDisplayClusterConfiguratorOutputMappingSlot;

UCLASS(MinimalAPI)
class UDisplayClusterConfiguratorBaseNode
	: public UEdGraphNode 
	, public IDisplayClusterConfiguratorOutputMappingItem
{
	GENERATED_BODY()

public:
	void Initialize(const FString& InNodeName, UObject* InObject, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin UObject Interface
	virtual void PostEditUndo() override;
	//~ End UObject Interface

	//~ Begin UEdGraphNode Interface
	virtual void ResizeNode(const FVector2D& NewSize) override;
	//~ End UEdGraphNode Interface

	//~ Begin IDisplayClusterConfiguratorItem Interface
	virtual void OnSelection() override;
	virtual UObject* GetObject() const override { return ObjectToEdit.Get(); }
	virtual bool IsSelected() override;
	//~ End IDisplayClusterConfiguratorItem Interface

	//~ Begin IDisplayClusterConfiguratorOutputMappingItem Interface
	virtual const FString& GetNodeName() const override;
	//~ End IDisplayClusterConfiguratorOutputMappingItem Interface

	FBox2D GetNodeBounds() const;
	FVector2D GetNodePosition() const;
	FVector2D GetNodeSize() const;

	virtual void UpdateObject() {}

	virtual void OnNodeAligned(const FVector2D& PositionChange, bool bUpdateChildren = false);

	bool WillOverlap(UDisplayClusterConfiguratorBaseNode* InNode, const FVector2D& InDesiredOffset = FVector2D::ZeroVector, const FVector2D& InDesiredSizeChange = FVector2D::ZeroVector) const;
	FVector2D FindNonOverlappingOffset(UDisplayClusterConfiguratorBaseNode* InNode, const FVector2D& InDesiredOffset) const;
	FVector2D FindNonOverlappingSize(UDisplayClusterConfiguratorBaseNode* InNode, const FVector2D& InDesiredSize, const bool bFixedApsectRatio) const;

protected:
	template<class TObjectType>
	TObjectType* GetObjectChecked() const
	{
		TObjectType* CastedObject = Cast<TObjectType>(ObjectToEdit.Get());
		check(CastedObject);
		return CastedObject;
	}

protected:
	TWeakObjectPtr<UObject> ObjectToEdit;
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	FString NodeName;
};
