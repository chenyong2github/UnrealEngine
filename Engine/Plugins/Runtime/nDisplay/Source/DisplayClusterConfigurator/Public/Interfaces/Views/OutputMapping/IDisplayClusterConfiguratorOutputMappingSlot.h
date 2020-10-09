// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SGraphPanel;
class SWidget;
class UEdGraphNode;
class UTexture;

/**
 * The Interface for control the position and size of Graph node and holds the node widgets and UObject
 */
class IDisplayClusterConfiguratorOutputMappingSlot
	: public TSharedFromThis<IDisplayClusterConfiguratorOutputMappingSlot>
{ 
public:
	/** Virtual destructor */
	virtual ~IDisplayClusterConfiguratorOutputMappingSlot() {}

	/**
	 * Build the slot, set initial position and spawn all node widgets
	 */
	virtual void Build() = 0;

	/**
	 * Add a child to this slot, for example, you might want to add viewport as a child of the window
	 *
	 * @param InChildSlot				Child slot instance
	 */
	virtual void AddChild(TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> InChildSlot) = 0;

	/**
	 * Get the position of the slot with scaling
	 * 
	 * @return the FVector2D position
	 */
	virtual const FVector2D GetLocalPosition() const = 0;

	/**
	 * Get the location of the slot with scaling
	 *
	 * @return the FVector2D size
	 */
	virtual const FVector2D GetLocalSize() const = 0;

	/**
	 * Get the size from config file
	 *
	 * @return the FVector2D size
	 */
	virtual const FVector2D GetConfigSize() const = 0;

	/**
	 * Get the position from config file
	 *
	 * @return the FVector2D position
	 */
	virtual const FVector2D GetConfigPostion() const = 0;

	/**
	 * Get name of the slot
	 *
	 * @return the FString
	 */
	virtual const FString& GetName() const = 0;

	/**
	 * Get type of the slot
	 *
	 * @return the FString
	 */
	virtual const FName& GetType() const = 0;

	/**
	 * Calculate the local position of the child based on child cluster node coordinates
	 *
	 * @param RealCoordinate			Child slot node coordinates
	 *
	 * @return the FVector2D position of the child
	 */
	virtual const FVector2D CalculateClildLocalPosition(FVector2D RealCoordinate) const = 0;

	/**
	 * Calculate the local position based on cluster node coordinates
	 *
	 * @param RealCoordinate			Node coordinates
	 *
	 * @return the FVector2D position
	 */
	virtual const FVector2D CalculateLocalPosition(FVector2D RealCoordinate) const = 0;

	/**
	 * Calculate the local size based on cluster node size
	 *
	 * @param RealCoordinate	Node size
	 *
	 * @return the FVector2D position
	 */
	virtual const FVector2D CalculateLocalSize(FVector2D RealSize) const = 0;

	/**
	 * Get the slot widget
	 *
	 * @return Widget
	 */
	virtual TSharedRef<SWidget> GetWidget() const = 0;

	/**
	 * Get Zorder of the slot
	 *
	 * @return integer value of ZOrder
	 */
	virtual uint32 GetZOrder() const = 0;

	/**
	 * Get parent slot instance
	 *
	 * @return ParentSlot
	 */
	virtual TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> GetParentSlot() const = 0;

	/**
	 * Get UObject being editing
	 *
	 * @return ParentSlot
	 */
	virtual UObject* GetEditingObject() const = 0;

	/**
	 * Get the Graph node UObject
	 *
	 * @return GraphNode
	 */
	virtual UEdGraphNode* GetGraphNode() const = 0;

	/**
	 * Checking whether this slot completely outside parent slot
	 *
	 * @return true if succeeded
	 */
	virtual bool IsOutsideParent() const = 0;

	/**
	 * Checking whether X or Y coords outside parent slot
	 *
	 * @return true if succeeded
	 */
	virtual bool IsOutsideParentBoundary() const = 0;

	/**
	 * Checking whether this slot slot is active and selected
	 *
	 * @return true if succeeded
	 */
	virtual bool IsActive() const = 0;

	/**
	 * Ticking slot on a Game Thread
	 *
	 * @param InDeltaTime			tick delta time
	 */
	virtual void Tick(float InDeltaTime) = 0;

	/**
	 * Set this slot active.
	 * It sets required ordering and visuals for the slot widgets
	 *
	 * @param bActive				Bool flag
	 */
	virtual void SetActive(bool bActive) = 0;

	/**
	 * Set the Graph Panel owner for the slot
	 *
	 * @param InGraphPanel			Graph Panel widget
	 */
	virtual void SetOwner(TSharedRef<SGraphPanel> InGraphPanel) = 0;

	/**
	 * Set local positin for this slot
	 *
	 * @param InLocalPosition		New position coordinates
	 */
	virtual void SetLocalPosition(FVector2D InLocalPosition) = 0;

	/**
	 * Set local size for this slot
	 *
	 * @param InLocalSize			New slot size
	 */
	virtual void SetLocalSize(FVector2D InLocalSize) = 0;

	/**
	 * Set new position into the config
	 *
	 * @param InConfigPosition		New consig position
	 */
	virtual void SetConfigPosition(FVector2D InConfigPosition) = 0;

	/**
	 * Set new size into the config
	 *
	 * @param InConfigSize			New consig size
	 */
	virtual void SetConfigSize(FVector2D InConfigSize) = 0;

	/**
	 * Set ZOrder for this slot
	 *
	 * @param InZOrder			New ZOrder
	 */
	virtual void SetZOrder(uint32 InZOrder) = 0;

	/**
	 * Set Texture Preivew for the Slot
	 *
	 * @param InTexture			Texture for the Preview
	 *
	 */
	virtual void SetPreviewTexture(UTexture* InTexture) = 0;
};
