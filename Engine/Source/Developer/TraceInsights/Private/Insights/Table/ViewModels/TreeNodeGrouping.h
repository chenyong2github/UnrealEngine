// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Delegates/DelegateCombinations.h"

// Insights
#include "Insights/Table/ViewModels/BaseTreeNode.h"
#include "Insights/Table/ViewModels/TableColumn.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class ITreeNodeGrouping
{
public:
	virtual FText GetShortName() const = 0;
	virtual FText GetTitleName() const = 0;
	virtual FText GetDescription() const = 0;

	virtual FName GetBrushName() const = 0;
	virtual const FSlateBrush* GetIcon() const = 0;

	virtual FName GetColumnId() const = 0;

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
	FTreeNodeGrouping(FText InShortName, FText InTitleName, FText InDescription, FName InBrushName,  FSlateBrush* InIcon);
	virtual ~FTreeNodeGrouping() {}

	virtual FText GetShortName() const override { return ShortName; }
	virtual FText GetTitleName() const override { return TitleName; }
	virtual FText GetDescription() const override { return Description; }

	virtual FName GetBrushName() const override { return BrushName; }
	virtual const FSlateBrush* GetIcon() const override { return Icon; }

	virtual FName GetColumnId() const override { return NAME_None; }

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const { return { FName(), false }; }

protected:
	FText ShortName;
	FText TitleName;
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

/** Creates a group for unique column value of node. */
class FTreeNodeGroupingByUniqueValue : public FTreeNodeGrouping
{
public:
	FTreeNodeGroupingByUniqueValue(TSharedRef<FTableColumn> InColumnRef);
	virtual ~FTreeNodeGroupingByUniqueValue() {}

	virtual FTreeNodeGroupInfo GetGroupForNode(const FBaseTreeNodePtr InNode) const override;

	virtual FName GetColumnId() const override { return ColumnRef->GetId(); }
	TSharedRef<FTableColumn> GetColumn() const { return ColumnRef; }

private:
	TSharedRef<FTableColumn> ColumnRef;
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
