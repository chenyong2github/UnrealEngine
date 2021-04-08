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

protected:
	/**
	 * Create a widget that displays the rebind button.
	 */
	TSharedRef<SWidget> CreateInvalidWidget();

private:
	/** Handles changing the object this entity is bound to upon selecting an actor in the rebinding dropdown. */
	void OnActorSelected(AActor* InActor) const;
	/** Returns whether or not the actor is selectable for a binding replacement. */
	bool IsActorSelectable(const AActor* Parent) const;
};