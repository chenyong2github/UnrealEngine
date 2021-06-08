// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "SkeletonTreeItem.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

class FSkeletonTreePhysicsConstraintItem : public FSkeletonTreeItem
{
public:
	SKELETON_TREE_ITEM_TYPE(FSkeletonTreePhysicsConstraintItem, FSkeletonTreeItem)

	FSkeletonTreePhysicsConstraintItem(UPhysicsConstraintTemplate* InConstraint, int32 InConstraintIndex, const FName& InBoneName, bool bInIsConstraintOnParentBody, const TSharedRef<class ISkeletonTree>& InSkeletonTree);

	/** ISkeletonTreeItem interface */
	virtual void GenerateWidgetForNameColumn(TSharedPtr< SHorizontalBox > Box, const TAttribute<FText>& FilterText, FIsSelected InIsSelected) override;
	virtual TSharedRef< SWidget > GenerateWidgetForDataColumn(const FName& DataColumnName, FIsSelected InIsSelected) override;	
	virtual FName GetRowItemName() const override { return DisplayName; }
	virtual UObject* GetObject() const override { return Constraint; }

	/** Get the index of the constraint in the physics asset */
	int32 GetConstraintIndex() const { return ConstraintIndex; }

	/** since constraint are showing on both parent and child, gets  if this tree item is the one on the parent body */
	bool IsConstraintOnParentBody() const { return bIsConstraintOnParentBody; }

private:
	FSlateColor GetConstraintTextColor() const;

private:
	/** The constraint we are representing */
	UPhysicsConstraintTemplate* Constraint;

	/** The index of the body setup in the physics asset */
	int32 ConstraintIndex;

	/** since constraint are showing on both parent and child, indicates if this tree item is the one on the parent body */
	bool bIsConstraintOnParentBody;

	/** The display name of the item */
	FName DisplayName;
};
