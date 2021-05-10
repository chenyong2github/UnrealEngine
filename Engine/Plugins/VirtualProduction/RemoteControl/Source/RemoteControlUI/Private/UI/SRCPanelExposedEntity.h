// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SRCPanelTreeNode.h"

class AActor;
struct FRemoteControlEntity;
class SWidget;

struct SRCPanelExposedEntity : public SRCPanelTreeNode
{
	/**
	 * Get the underlying exposed entity.
	 */
	virtual TSharedPtr<FRemoteControlEntity> GetEntity() const = 0;

	/**
	 * Get the context menu for this item.
	 */
	virtual TSharedPtr<SWidget> GetContextMenu() override;

protected:
	/** Create a widget that displays the rebind button. */
	TSharedRef<SWidget> CreateInvalidWidget();

private:
	/** Handles changing the object this entity is bound to upon selecting an actor in the rebinding dropdown. */
	void OnActorSelected(AActor* InActor) const;
	
	/** Create the content of the rebind button.  */
	TSharedRef<SWidget> CreateRebindMenuContent();
};