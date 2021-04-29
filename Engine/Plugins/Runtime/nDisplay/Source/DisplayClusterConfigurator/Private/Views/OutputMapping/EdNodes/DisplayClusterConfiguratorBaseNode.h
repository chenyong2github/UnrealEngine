// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorOutputMappingItem.h"
#include "EdGraph/EdGraphNode.h"

#include "Views/OutputMapping/Alignment/DisplayClusterConfiguratorNodeAlignmentHelper.h"

#include "DisplayClusterConfiguratorBaseNode.generated.h"

class FDisplayClusterConfiguratorBlueprintEditor;
class IDisplayClusterConfiguratorOutputMappingSlot;
class IDisplayClusterConfiguratorTreeItem;

UCLASS(MinimalAPI)
class UDisplayClusterConfiguratorBaseNode
	: public UEdGraphNode 
	, public IDisplayClusterConfiguratorOutputMappingItem
{
	GENERATED_BODY()

public:
	virtual void Initialize(const FString& InNodeName, UObject* InObject, const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit);

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
	//~ End UObject Interface

	//~ Begin UEdGraphNode Interface
	virtual void PostPlacedNewNode() override;
	virtual void ResizeNode(const FVector2D& NewSize) override;
	virtual bool CanDuplicateNode() const override { return false; }
	virtual bool CanUserDeleteNode() const override { return false; }
	//~ End UEdGraphNode Interface

	//~ Begin IDisplayClusterConfiguratorItem Interface
	virtual void OnSelection() override;
	virtual UObject* GetObject() const override { return ObjectToEdit.Get(); }
	virtual bool IsSelected() override;
	//~ End IDisplayClusterConfiguratorItem Interface

	//~ Begin IDisplayClusterConfiguratorOutputMappingItem Interface
	virtual const FString& GetNodeName() const override;
	//~ End IDisplayClusterConfiguratorOutputMappingItem Interface

	virtual void SetParent(UDisplayClusterConfiguratorBaseNode* InParent);
	virtual UDisplayClusterConfiguratorBaseNode* GetParent() const;
	virtual void AddChild(UDisplayClusterConfiguratorBaseNode* InChild);
	virtual const TArray<UDisplayClusterConfiguratorBaseNode*>& GetChildren() const;

	virtual FVector2D TransformPointToLocal(FVector2D GlobalPosition) const;
	virtual FVector2D TransformPointToGlobal(FVector2D LocalPosition) const;
	virtual FVector2D TransformSizeToLocal(FVector2D GlobalSize) const;
	virtual FVector2D TransformSizeToGlobal(FVector2D LocalSize) const;
	virtual FVector2D GetTranslationOffset() const { return FVector2D::ZeroVector; }

	virtual FBox2D GetNodeBounds(bool bAsParent = false) const;
	virtual FVector2D GetNodePosition() const;
	virtual FVector2D GetNodeLocalPosition() const;
	virtual FVector2D GetNodeSize() const;
	virtual FVector2D GetNodeLocalSize() const;
	virtual FNodeAlignmentAnchors GetNodeAlignmentAnchors(bool bAsParent = false) const;
	virtual bool IsNodeVisible() const { return true; }
	virtual bool IsNodeEnabled() const { return true; }
	virtual bool IsNodeAutoPositioned() const { return false; }
	virtual bool IsNodeAutosized() const { return false; }

	virtual void FillParent(bool bRepositionNode = true);
	virtual void SizeToChildren(bool bRepositionNode = true);

	virtual FBox2D GetChildBounds() const;
	virtual FBox2D GetDescendentBounds() const;
	virtual bool IsOutsideParent() const;
	virtual bool IsOutsideParentBoundary() const;
	virtual void UpdateChildNodes();

	virtual void TickPosition() { }

	virtual bool IsUserInteractingWithNode(bool bCheckDescendents = false) const;
	virtual void MarkUserInteractingWithNode() { bIsUserInteractingWithNode = true; }
	virtual void ClearUserInteractingWithNode() { bIsUserInteractingWithNode = false; }

	virtual void UpdateNode();
	virtual void UpdateObject();
	virtual void DeleteObject() { }

	virtual void OnNodeAligned(bool bUpdateChildren = false);

	virtual bool WillOverlap(UDisplayClusterConfiguratorBaseNode* InNode, const FVector2D& InDesiredOffset = FVector2D::ZeroVector, const FVector2D& InDesiredSizeChange = FVector2D::ZeroVector) const;
	virtual FVector2D FindNonOverlappingOffset(UDisplayClusterConfiguratorBaseNode* InNode, const FVector2D& InDesiredOffset) const;
	virtual FVector2D FindNonOverlappingSize(UDisplayClusterConfiguratorBaseNode* InNode, const FVector2D& InDesiredSize, const bool bFixedApsectRatio) const;

	virtual FVector2D FindNonOverlappingOffsetFromParent(const FVector2D& InDesiredOffset);
	virtual FVector2D FindBoundedOffsetFromParent(const FVector2D& InDesiredOffset);
	virtual FVector2D FindNonOverlappingSizeFromParent(const FVector2D& InDesiredSize, const bool bFixedApsectRatio);
	virtual FVector2D FindBoundedSizeFromParent(const FVector2D& InDesiredSize, const bool bFixedApsectRatio);
	virtual FVector2D FindBoundedSizeFromChildren(const FVector2D& InDesiredSize, const bool bFixedApsectRatio);

	virtual FNodeAlignmentPair GetTranslationAlignments(const FVector2D& InOffset, const FNodeAlignmentParams& AlignmentParams) const;
	virtual FNodeAlignmentPair GetResizeAlignments(const FVector2D& InSizeChange, const FNodeAlignmentParams& AlignmentParams) const;

protected:
	virtual bool CanAlignWithParent() const { return false; }
	virtual FNodeAlignmentPair GetAlignments(const FNodeAlignmentAnchors& TransformedAnchors, const FNodeAlignmentParams& AlignmentParams) const;

	virtual void WriteNodeStateToObject() { }
	virtual void ReadNodeStateFromObject() { }

	virtual float GetViewScale() const;

	template<class TObjectType>
	TObjectType* GetObjectChecked() const
	{
		TObjectType* CastedObject = Cast<TObjectType>(ObjectToEdit.Get());
		check(CastedObject);
		return CastedObject;
	}

	template<class TParentType>
	TParentType* GetParentChecked() const
	{
		TParentType* CastedParent = Cast<TParentType>(Parent.Get());
		check(CastedParent);
		return CastedParent;
	}

protected:
	TWeakObjectPtr<UObject> ObjectToEdit;
	TWeakPtr<FDisplayClusterConfiguratorBlueprintEditor> ToolkitPtr;

	TWeakObjectPtr<UDisplayClusterConfiguratorBaseNode> Parent;
	TArray<UDisplayClusterConfiguratorBaseNode*> Children;

	FString NodeName;

	bool bIsUserInteractingWithNode;
};
