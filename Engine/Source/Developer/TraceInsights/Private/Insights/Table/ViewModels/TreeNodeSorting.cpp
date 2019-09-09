// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TreeNodeSorting.h"

// Insights
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/Table/ViewModels/TableTreeNode.h"

#define LOCTEXT_NAMESPACE "Insights_TreeNode"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeSorting
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeSorting::FTreeNodeSorting(FName InName, FText InShortName, FText InTitleName, FText InDescription, FName InColumnId)
	: Name(InName)
	, ShortName(InShortName)
	, TitleName(InTitleName)
	, Description(InDescription)
	, ColumnId(InColumnId)
	, AscendingIcon(nullptr)
	, DescendingIcon(nullptr)
	, AscendingCompareDelegate(nullptr)
	, DescendingCompareDelegate(nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTreeNodeSorting::SortAscending(TArray<FBaseTreeNodePtr>& NodesToSort) const
{
	if (AscendingCompareDelegate)
	{
		NodesToSort.Sort(AscendingCompareDelegate);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTreeNodeSorting::SortDescending(TArray<FBaseTreeNodePtr>& NodesToSort) const
{
	if (DescendingCompareDelegate)
	{
		NodesToSort.Sort(DescendingCompareDelegate);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeSortingByName
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeSortingByName::FTreeNodeSortingByName()
	: FTreeNodeSorting(
		FName(TEXT("ByName")),
		LOCTEXT("Sorting_ByName_Name", "By Name"),
		LOCTEXT("Sorting_ByName_Title", "Sort By Name"),
		LOCTEXT("Sorting_ByName_Desc", "Sort by name."),
		FName(TEXT("Name")))
{
	AscendingCompareDelegate = [](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		// Sort by name (ascending).
		return A->GetName().LexicalLess(B->GetName());
	};

	DescendingCompareDelegate = [](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		// Sort by name (descending).
		return B->GetName().LexicalLess(A->GetName());
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeSortingByType
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeSortingByType::FTreeNodeSortingByType()
	: FTreeNodeSorting(
		FName(TEXT("ByType")),
		LOCTEXT("Sorting_ByName_Name", "By Type"),
		LOCTEXT("Sorting_ByName_Title", "Sort By Type"),
		LOCTEXT("Sorting_ByName_Desc", "Sort by type (ascending or descending), then by name (ascending)."),
		FName(TEXT("Type")))
{
	AscendingCompareDelegate = [](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const FName& TypeA = A->GetTypeId();
		const FName& TypeB = B->GetTypeId();

		if (TypeA == TypeB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by type (ascending).
			return TypeA.FastLess(TypeB); // TypeA < TypeB;
		}
	};

	DescendingCompareDelegate = [](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const FName& TypeA = A->GetTypeId();
		const FName& TypeB = B->GetTypeId();

		if (TypeA == TypeB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by type (descending).
			return TypeB.FastLess(TypeA); // TypeA > TypeB;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTableTreeNodeSorting
////////////////////////////////////////////////////////////////////////////////////////////////////

FTableTreeNodeSorting::FTableTreeNodeSorting(TSharedRef<FTableColumn> InColumnRef)
	: FTreeNodeSorting(
		InColumnRef->GetId(),
		FText::Format(LOCTEXT("Sorting_TableValue_Name", "By {0}"), InColumnRef->GetShortName()),
		FText::Format(LOCTEXT("Sorting_TableValue_Title", "Sort By {0}"), InColumnRef->GetTitleName()),
		FText::Format(LOCTEXT("Sorting_TableValue_Desc", "Sort by {0} (ascending or descending), then by name (ascending)."), InColumnRef->GetShortName()),
		InColumnRef->GetId())
	, ColumnRef(InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeSortingByBoolValue
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeSortingByBoolValue::FTreeNodeSortingByBoolValue(TSharedRef<FTableColumn> InColumnRef)
	: FTableTreeNodeSorting(InColumnRef)
{
	const FTableColumn& Column = *InColumnRef;

	AscendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const bool ValueA = StaticCastSharedPtr<FTableTreeNode>(A)->GetValueBool(Column);
		const bool ValueB = StaticCastSharedPtr<FTableTreeNode>(B)->GetValueBool(Column);

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by value (ascending).
			return ValueA < ValueB;
		}
	};

	DescendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const bool ValueA = StaticCastSharedPtr<FTableTreeNode>(A)->GetValueBool(Column);
		const bool ValueB = StaticCastSharedPtr<FTableTreeNode>(B)->GetValueBool(Column);

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by value (descending).
			return ValueB < ValueA;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeSortingByIntValue
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeSortingByIntValue::FTreeNodeSortingByIntValue(TSharedRef<FTableColumn> InColumnRef)
	: FTableTreeNodeSorting(InColumnRef)
{
	const FTableColumn& Column = *InColumnRef;

	AscendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const int64 ValueA = StaticCastSharedPtr<FTableTreeNode>(A)->GetValueInt64(Column);
		const int64 ValueB = StaticCastSharedPtr<FTableTreeNode>(B)->GetValueInt64(Column);

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by value (ascending).
			return ValueA < ValueB;
		}
	};

	DescendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const int64 ValueA = StaticCastSharedPtr<FTableTreeNode>(A)->GetValueInt64(Column);
		const int64 ValueB = StaticCastSharedPtr<FTableTreeNode>(B)->GetValueInt64(Column);

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by value (descending).
			return ValueB < ValueA;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeSortingByFloatValue
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeSortingByFloatValue::FTreeNodeSortingByFloatValue(TSharedRef<FTableColumn> InColumnRef)
	: FTableTreeNodeSorting(InColumnRef)
{
	const FTableColumn& Column = *InColumnRef;

	AscendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const float ValueA = StaticCastSharedPtr<FTableTreeNode>(A)->GetValueFloat(Column);
		const float ValueB = StaticCastSharedPtr<FTableTreeNode>(B)->GetValueFloat(Column);

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by value (ascending).
			return ValueA < ValueB;
		}
	};

	DescendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const float ValueA = StaticCastSharedPtr<FTableTreeNode>(A)->GetValueFloat(Column);
		const float ValueB = StaticCastSharedPtr<FTableTreeNode>(B)->GetValueFloat(Column);

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by value (descending).
			return ValueB < ValueA;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeSortingByDoubleValue
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeSortingByDoubleValue::FTreeNodeSortingByDoubleValue(TSharedRef<FTableColumn> InColumnRef)
	: FTableTreeNodeSorting(InColumnRef)
{
	const FTableColumn& Column = *InColumnRef;

	AscendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const double ValueA = StaticCastSharedPtr<FTableTreeNode>(A)->GetValueDouble(Column);
		const double ValueB = StaticCastSharedPtr<FTableTreeNode>(B)->GetValueDouble(Column);

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by value (ascending).
			return ValueA < ValueB;
		}
	};

	DescendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const double ValueA = StaticCastSharedPtr<FTableTreeNode>(A)->GetValueDouble(Column);
		const double ValueB = StaticCastSharedPtr<FTableTreeNode>(B)->GetValueDouble(Column);

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by value (descending).
			return ValueB < ValueA;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTreeNodeSortingByCStringValue
////////////////////////////////////////////////////////////////////////////////////////////////////

FTreeNodeSortingByCStringValue::FTreeNodeSortingByCStringValue(TSharedRef<FTableColumn> InColumnRef)
	: FTableTreeNodeSorting(InColumnRef)
{
	const FTableColumn& Column = *InColumnRef;

	AscendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TCHAR* ValueA = StaticCastSharedPtr<FTableTreeNode>(A)->GetValueCString(Column);
		const TCHAR* ValueB = StaticCastSharedPtr<FTableTreeNode>(B)->GetValueCString(Column);

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by value (ascending).
			return FCString::Strcmp(ValueA, ValueB) < 0;
		}
	};

	DescendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TCHAR* ValueA = StaticCastSharedPtr<FTableTreeNode>(A)->GetValueCString(Column);
		const TCHAR* ValueB = StaticCastSharedPtr<FTableTreeNode>(B)->GetValueCString(Column);

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			return A->GetName().LexicalLess(B->GetName());
		}
		else
		{
			// Sort by value (descending).
			return FCString::Strcmp(ValueB, ValueA) < 0;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
