// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SModifierListview.h"

/** Listview row widget representing a single modifier instance */
class SModifierItemRow : public STableRow<ModifierListviewItem>
{
public:
	SLATE_BEGIN_ARGS(SModifierItemRow) {}
	SLATE_ARGUMENT(FOnSingleModifier, OnOpenModifier);
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const ModifierListviewItem& Item);
	FReply OnDoubleClicked(const FGeometry& MyGeometry, const FPointerEvent& PointerEvent);

protected:
	FText GetInstanceText() const;
protected:
	ModifierListviewItem InternalItem;
	FOnSingleModifier OnOpenModifier;
};
