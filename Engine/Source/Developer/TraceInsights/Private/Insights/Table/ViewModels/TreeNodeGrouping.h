// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Delegates/DelegateCombinations.h"

// Insights
#include "Insights/Table/ViewModels/BaseTreeNode.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class ITreeNodeGrouping
{
public:
	virtual FText GetName() const = 0;
	virtual FText GetDescription() const = 0;
	virtual FName GetBrushName() const = 0;
	virtual const FSlateBrush* GetIcon() const = 0;

	//virtual void CreateGrouping() = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTreeNodeGroupInfo
{
	FName Name;
	bool IsExpanded;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTreeNodeGrouping : public ITreeNodeGrouping
{
public:
	FTreeNodeGrouping(FText InName, FText InDescription, FName InBrushName,  FSlateBrush* InIcon);
	virtual ~FTreeNodeGrouping() {}

	virtual FText GetName() const override { return Name; }
	virtual FText GetDescription() const override { return Description; }
	virtual FName GetBrushName() const override { return BrushName; }
	virtual const FSlateBrush* GetIcon() const override { return Icon; }

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const { return { FName(), false }; }

protected:
	FText Name;
	FText Description;
	FName BrushName;
	const FSlateBrush* Icon;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a single group for all nodes. */
class FTreeNodeGroupingFlat : public FTreeNodeGrouping
{
public:
	FTreeNodeGroupingFlat();
	virtual ~FTreeNodeGroupingFlat() {}

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each first letter of node names. */
class FTreeNodeGroupingByNameFirstLetter : public FTreeNodeGrouping
{
public:
	FTreeNodeGroupingByNameFirstLetter();
	virtual ~FTreeNodeGroupingByNameFirstLetter() {}

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each type. */
class FTreeNodeGroupingByType : public FTreeNodeGrouping
{
public:
	FTreeNodeGroupingByType();
	virtual ~FTreeNodeGroupingByType() {}

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
