// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "kiwi/kiwi.h"
#include "Misc/Optional.h"
#include "SlotBase.h"

struct FCommonUILayoutPanelSlot : public TSlotBase<FCommonUILayoutPanelSlot>
{
public:
	/** Position within parent. */
	FVector2D Position = FVector2D::ZeroVector;

	/** Class of the associated widget. */
	TSoftClassPtr<UUserWidget> WidgetClass;

	/** Z order of this slot. (Higher number are drawn in front of lower number) */
	int32 ZOrder = 1000;

	/** Store the UserWidget as we may want to tweak its values for ghost widgets */
	TWeakObjectPtr<UUserWidget> SpawnedWidget;

	/** Optional unique ID for this slot */
	FName UniqueID;

	/** Left position variable for the layout constraints. */
	kiwi::Variable Left;

	/** Top position variable for the layout constraint. */
	kiwi::Variable Top;

	/** Is using the safe zone. */
	bool bIsUsingSafeZone = true;

	/** Should this slot always use the full allotted geometry size. */
	bool bAlwaysUseFullAllotedSize = false;

	/** Do the layout constraints need to be recalculated. */
	bool bAreConstraintsDirty = false;

	/** Returns adjusted size if set, desired size otherwise. */
	const FVector2D GetSize() const { return AdjustedSize.IsSet() ? AdjustedSize.GetValue() : GetWidget().Get().GetDesiredSize(); }

	void SetAdjustedSize(const FVector2D& Size) { AdjustedSize = Size; }

private:
	/** Size adjusted for alignment settings from DynamicHUDLayout. */
	TOptional<FVector2D> AdjustedSize;
};