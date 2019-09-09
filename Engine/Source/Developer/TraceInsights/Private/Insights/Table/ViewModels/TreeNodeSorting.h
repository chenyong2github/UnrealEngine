// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Styling/SlateBrush.h"

// Insights
#include "Insights/Table/ViewModels/BaseTreeNode.h"

namespace Insights
{

class FTableColumn;
class FTableTreeNode;

////////////////////////////////////////////////////////////////////////////////////////////////////

typedef TFunction<bool(const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B)> FTreeNodeSortingCompareFn;

class ITreeNodeSorting
{
public:
	virtual FName GetName() const = 0;
	virtual FText GetShortName() const = 0;
	virtual FText GetTitleName() const = 0;
	virtual FText GetDescription() const = 0;
	virtual FName GetColumnId() const = 0;

	virtual FSlateBrush* GetAscendingIcon() const = 0;
	virtual FSlateBrush* GetDescendingIcon() const = 0;

	virtual FTreeNodeSortingCompareFn GetAscendingCompareDelegate() const = 0;
	virtual FTreeNodeSortingCompareFn GetDescendingCompareDelegate() const = 0;

	virtual void SortAscending(TArray<FBaseTreeNodePtr>& NodesToSort) const = 0;
	virtual void SortDescending(TArray<FBaseTreeNodePtr>& NodesToSort) const = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTreeNodeSorting : public ITreeNodeSorting
{
public:
	typedef bool FCompareFn(const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) const;

public:
	FTreeNodeSorting(FName InName, FText InShortName, FText InTitleName, FText InDescription, FName InColumnId);
	virtual ~FTreeNodeSorting() {}

	virtual FName GetName() const override { return Name; }
	virtual FText GetShortName() const override { return ShortName; }
	virtual FText GetTitleName() const override { return TitleName; }
	virtual FText GetDescription() const override { return Description; }
	virtual FName GetColumnId() const override { return ColumnId; }

	virtual FSlateBrush* GetAscendingIcon() const override { return AscendingIcon; }
	virtual FSlateBrush* GetDescendingIcon() const override { return DescendingIcon; }

	virtual FTreeNodeSortingCompareFn GetAscendingCompareDelegate() const override { return AscendingCompareDelegate; }
	virtual FTreeNodeSortingCompareFn GetDescendingCompareDelegate() const override { return DescendingCompareDelegate; }

	virtual void SortAscending(TArray<FBaseTreeNodePtr>& NodesToSort) const override;
	virtual void SortDescending(TArray<FBaseTreeNodePtr>& NodesToSort) const override;

protected:
	FName Name;
	FText ShortName;
	FText TitleName;
	FText Description;
	FName ColumnId;
	
	FSlateBrush* AscendingIcon;
	FSlateBrush* DescendingIcon;

	FTreeNodeSortingCompareFn AscendingCompareDelegate;
	FTreeNodeSortingCompareFn DescendingCompareDelegate;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTreeNodeSortingByName : public FTreeNodeSorting
{
public:
	FTreeNodeSortingByName();
	virtual ~FTreeNodeSortingByName() {}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTreeNodeSortingByType : public FTreeNodeSorting
{
public:
	FTreeNodeSortingByType();
	virtual ~FTreeNodeSortingByType() {}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTableTreeNodeSorting : public FTreeNodeSorting
{
public:
	FTableTreeNodeSorting(TSharedRef<FTableColumn> InColumnRef);
	virtual ~FTableTreeNodeSorting() {}

protected:
	TSharedRef<FTableColumn> ColumnRef;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTreeNodeSortingByBoolValue : public FTableTreeNodeSorting
{
public:
	FTreeNodeSortingByBoolValue(TSharedRef<FTableColumn> InColumnRef);
	virtual ~FTreeNodeSortingByBoolValue() {}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTreeNodeSortingByIntValue : public FTableTreeNodeSorting
{
public:
	FTreeNodeSortingByIntValue(TSharedRef<FTableColumn> InColumnRef);
	virtual ~FTreeNodeSortingByIntValue() {}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTreeNodeSortingByFloatValue : public FTableTreeNodeSorting
{
public:
	FTreeNodeSortingByFloatValue(TSharedRef<FTableColumn> InColumnRef);
	virtual ~FTreeNodeSortingByFloatValue() {}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTreeNodeSortingByDoubleValue : public FTableTreeNodeSorting
{
public:
	FTreeNodeSortingByDoubleValue(TSharedRef<FTableColumn> InColumnRef);
	virtual ~FTreeNodeSortingByDoubleValue() {}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTreeNodeSortingByCStringValue : public FTableTreeNodeSorting
{
public:
	FTreeNodeSortingByCStringValue(TSharedRef<FTableColumn> InColumnRef);
	virtual ~FTreeNodeSortingByCStringValue() {}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
