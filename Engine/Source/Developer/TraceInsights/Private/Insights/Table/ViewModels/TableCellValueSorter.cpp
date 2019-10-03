// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TableCellValueSorter.h"

#include "Insights/Table/ViewModels/BaseTreeNode.h"

#define LOCTEXT_NAMESPACE "TableCellValueSorter"

// Sort by name (ascending). -- too slow :(
//#define INSIGHTS_DEFAULT_SORTING_NODES(A, B) return A->GetName().LexicalLess(B->GetName());
//#define INSIGHTS_DEFAULT_SORTING_ELEMENTS(A, B) return A.NodePtr->GetName().LexicalLess(B.NodePtr->GetName());

// Sort by node id (ascending).
#define INSIGHTS_DEFAULT_SORTING_NODES(A, B) return A->GetId() < B->GetId();
#define INSIGHTS_DEFAULT_SORTING_ELEMENTS(A, B) return A.NodePtr->GetId() < B.NodePtr->GetId();

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTableCellValueSorter
////////////////////////////////////////////////////////////////////////////////////////////////////

FTableCellValueSorter::FTableCellValueSorter(const FName InName, const FText& InShortName, const FText& InTitleName, const FText& InDescription, TSharedRef<FTableColumn> InColumnRef)
	: Name(InName)
	, ShortName(InShortName)
	, TitleName(InTitleName)
	, Description(InDescription)
	, ColumnRef(InColumnRef)
	, AscendingIcon(nullptr)
	, DescendingIcon(nullptr)
	, AscendingCompareDelegate(nullptr)
	, DescendingCompareDelegate(nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTableCellValueSorter::Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const
{
	if (SortMode == ESortMode::Ascending)
	{
		if (AscendingCompareDelegate)
		{
			NodesToSort.Sort(AscendingCompareDelegate);
		}
	}
	else
	{
		if (DescendingCompareDelegate)
		{
			NodesToSort.Sort(DescendingCompareDelegate);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FBaseTableColumnSorter
////////////////////////////////////////////////////////////////////////////////////////////////////

FBaseTableColumnSorter::FBaseTableColumnSorter(TSharedRef<FTableColumn> InColumnRef)
	: FTableCellValueSorter(
		InColumnRef->GetId(),
		FText::Format(LOCTEXT("Sorter_ColumnValue_Name", "By {0}"), InColumnRef->GetShortName()),
		FText::Format(LOCTEXT("Sorter_ColumnValue_Title", "Sort By {0}"), InColumnRef->GetTitleName()),
		FText::Format(LOCTEXT("Sorter_ColumnValue_Desc", "Sort by {0} (ascending or descending), then by id (ascending)."), InColumnRef->GetShortName()),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FSorterByName
////////////////////////////////////////////////////////////////////////////////////////////////////

FSorterByName::FSorterByName(TSharedRef<FTableColumn> InColumnRef)
	: FTableCellValueSorter(
		FName(TEXT("ByName")),
		LOCTEXT("Sorter_ByName_Name", "By Name"),
		LOCTEXT("Sorter_ByName_Title", "Sort By Name"),
		LOCTEXT("Sorter_ByName_Desc", "Sort by name."),
		InColumnRef)
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
// FSorterByTypeName
////////////////////////////////////////////////////////////////////////////////////////////////////

FSorterByTypeName::FSorterByTypeName(TSharedRef<FTableColumn> InColumnRef)
	: FTableCellValueSorter(
		FName(TEXT("ByTypeName")),
		LOCTEXT("Sorter_ByTypeName_Name", "By Type Name"),
		LOCTEXT("Sorter_ByTypeName_Title", "Sort By Type Name"),
		LOCTEXT("Sorter_ByTypeName_Desc", "Sort by type name (ascending or descending), then by id (ascending)."),
		InColumnRef)
{
	AscendingCompareDelegate = [](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const FName& TypeA = A->GetTypeName();
		const FName& TypeB = B->GetTypeName();

		if (TypeA == TypeB)
		{
			INSIGHTS_DEFAULT_SORTING_NODES(A, B)
		}
		else
		{
			// Sort by type name (ascending).
			return TypeA.FastLess(TypeB); // TypeA < TypeB;
		}
	};

	DescendingCompareDelegate = [](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const FName& TypeA = A->GetTypeName();
		const FName& TypeB = B->GetTypeName();

		if (TypeA == TypeB)
		{
			INSIGHTS_DEFAULT_SORTING_NODES(A, B)
		}
		else
		{
			// Sort by type name (descending).
			return TypeB.FastLess(TypeA); // TypeA > TypeB;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FSorterByBoolValue
////////////////////////////////////////////////////////////////////////////////////////////////////

FSorterByBoolValue::FSorterByBoolValue(TSharedRef<FTableColumn> InColumnRef)
	: FBaseTableColumnSorter(InColumnRef)
{
	const FTableColumn& Column = *InColumnRef;

	ensure(Column.GetDataType() == ETableCellDataType::Bool);

	AscendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		const bool ValueA = OptionalValueA.IsSet() ? OptionalValueA.GetValue().Bool : false;
		const bool ValueB = OptionalValueB.IsSet() ? OptionalValueB.GetValue().Bool : false;

		if (ValueA == ValueB)
		{
			INSIGHTS_DEFAULT_SORTING_NODES(A, B)
		}
		else
		{
			// Sort by value (ascending).
			return ValueA < ValueB;
		}
	};

	DescendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		const bool ValueA = OptionalValueA.IsSet() ? OptionalValueA.GetValue().Bool : false;
		const bool ValueB = OptionalValueB.IsSet() ? OptionalValueB.GetValue().Bool : false;

		if (ValueA == ValueB)
		{
			INSIGHTS_DEFAULT_SORTING_NODES(A, B)
		}
		else
		{
			// Sort by value (descending).
			return ValueB < ValueA;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FSorterByInt64Value
////////////////////////////////////////////////////////////////////////////////////////////////////

FSorterByInt64Value::FSorterByInt64Value(TSharedRef<FTableColumn> InColumnRef)
	: FBaseTableColumnSorter(InColumnRef)
{
	const FTableColumn& Column = *InColumnRef;

	ensure(Column.GetDataType() == ETableCellDataType::Int64);

	AscendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		const int64 ValueA = OptionalValueA.IsSet() ? OptionalValueA.GetValue().Int64 : 0;
		const int64 ValueB = OptionalValueB.IsSet() ? OptionalValueB.GetValue().Int64 : 0;

		if (ValueA == ValueB)
		{
			INSIGHTS_DEFAULT_SORTING_NODES(A, B)
		}
		else
		{
			// Sort by value (ascending).
			return ValueA < ValueB;
		}
	};

	DescendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		const int64 ValueA = OptionalValueA.IsSet() ? OptionalValueA.GetValue().Int64 : 0;
		const int64 ValueB = OptionalValueB.IsSet() ? OptionalValueB.GetValue().Int64 : 0;

		if (ValueA == ValueB)
		{
			INSIGHTS_DEFAULT_SORTING_NODES(A, B)
		}
		else
		{
			// Sort by value (descending).
			return ValueB < ValueA;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FSorterByInt64Value::Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const
{
	// Note: This implementation is faster than using above delegates.
	//       It caches the values before sorting, in this way minimizes the calls to Column.GetValue().

	const FTableColumn& Column = *ColumnRef;

	const int32 NumElements = NodesToSort.Num();

	struct FSortElement
	{
		const FBaseTreeNodePtr NodePtr;
		int64 Value;
	};
	TArray<FSortElement> ElementsToSort;

	// Cache value for each node.
	ElementsToSort.Reset(NumElements);
	for (const FBaseTreeNodePtr& NodePtr : NodesToSort)
	{
		const TOptional<FTableCellValue> OptionalValue = Column.GetValue(*NodePtr);
		const int64 Value = OptionalValue.IsSet() ? OptionalValue.GetValue().Int64 : 0;
		ElementsToSort.Add({ NodePtr, Value });
	}
	ensure(ElementsToSort.Num() == NumElements);

	if (SortMode == ESortMode::Ascending)
	{
		ElementsToSort.Sort([](const FSortElement& A, const FSortElement& B) -> bool
		{
			if (A.Value == B.Value)
			{
				INSIGHTS_DEFAULT_SORTING_ELEMENTS(A, B)
			}
			else
			{
				// Sort by value (ascending).
				return A.Value < B.Value;
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		ElementsToSort.Sort([](const FSortElement& A, const FSortElement& B) -> bool
		{
			if (A.Value == B.Value)
			{
				INSIGHTS_DEFAULT_SORTING_ELEMENTS(A, B)
			}
			else
			{
				// Sort by value (descending).
				return B.Value < A.Value;
			}
		});
	}

	for (int32 Index = 0; Index < NumElements; ++Index)
	{
		NodesToSort[Index] = ElementsToSort[Index].NodePtr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FSorterByFloatValue
////////////////////////////////////////////////////////////////////////////////////////////////////

FSorterByFloatValue::FSorterByFloatValue(TSharedRef<FTableColumn> InColumnRef)
	: FBaseTableColumnSorter(InColumnRef)
{
	const FTableColumn& Column = *InColumnRef;

	ensure(Column.GetDataType() == ETableCellDataType::Float);

	AscendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		const float ValueA = OptionalValueA.IsSet() ? OptionalValueA.GetValue().Float : 0.0f;
		const float ValueB = OptionalValueB.IsSet() ? OptionalValueB.GetValue().Float : 0.0f;

		if (ValueA == ValueB)
		{
			INSIGHTS_DEFAULT_SORTING_NODES(A, B)
		}
		else
		{
			// Sort by value (ascending).
			return ValueA < ValueB;
		}
	};

	DescendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		const float ValueA = OptionalValueA.IsSet() ? OptionalValueA.GetValue().Float : 0.0f;
		const float ValueB = OptionalValueB.IsSet() ? OptionalValueB.GetValue().Float : 0.0f;

		if (ValueA == ValueB)
		{
			INSIGHTS_DEFAULT_SORTING_NODES(A, B)
		}
		else
		{
			// Sort by value (descending).
			return ValueB < ValueA;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FSorterByFloatValue::Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const
{
	// Note: This implementation is faster than using above delegates.
	//       It caches the values before sorting, in this way minimizes the calls to Column.GetValue().

	const FTableColumn& Column = *ColumnRef;

	const int32 NumElements = NodesToSort.Num();

	struct FSortElement
	{
		const FBaseTreeNodePtr NodePtr;
		float Value;
	};
	TArray<FSortElement> ElementsToSort;

	// Cache value for each node.
	ElementsToSort.Reset(NumElements);
	for (const FBaseTreeNodePtr& NodePtr : NodesToSort)
	{
		const TOptional<FTableCellValue> OptionalValue = Column.GetValue(*NodePtr);
		const float Value = OptionalValue.IsSet() ? OptionalValue.GetValue().Float : 0.0f;
		ElementsToSort.Add({ NodePtr, Value });
	}
	ensure(ElementsToSort.Num() == NumElements);

	if (SortMode == ESortMode::Ascending)
	{
		ElementsToSort.Sort([](const FSortElement& A, const FSortElement& B) -> bool
		{
			if (A.Value == B.Value)
			{
				INSIGHTS_DEFAULT_SORTING_ELEMENTS(A, B)
			}
			else
			{
				// Sort by value (ascending).
				return A.Value < B.Value;
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		ElementsToSort.Sort([](const FSortElement& A, const FSortElement& B) -> bool
		{
			if (A.Value == B.Value)
			{
				INSIGHTS_DEFAULT_SORTING_ELEMENTS(A, B)
			}
			else
			{
				// Sort by value (descending).
				return B.Value < A.Value;
			}
		});
	}

	for (int32 Index = 0; Index < NumElements; ++Index)
	{
		NodesToSort[Index] = ElementsToSort[Index].NodePtr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FSorterByDoubleValue
////////////////////////////////////////////////////////////////////////////////////////////////////

FSorterByDoubleValue::FSorterByDoubleValue(TSharedRef<FTableColumn> InColumnRef)
	: FBaseTableColumnSorter(InColumnRef)
{
	const FTableColumn& Column = *InColumnRef;

	ensure(Column.GetDataType() == ETableCellDataType::Double);

	AscendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		const double ValueA = OptionalValueA.IsSet() ? OptionalValueA.GetValue().Double : 0.0;
		const double ValueB = OptionalValueB.IsSet() ? OptionalValueB.GetValue().Double : 0.0;

		if (ValueA == ValueB)
		{
			INSIGHTS_DEFAULT_SORTING_NODES(A, B)
		}
		else
		{
			// Sort by value (ascending).
			return ValueA < ValueB;
		}
	};

	DescendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		const double ValueA = OptionalValueA.IsSet() ? OptionalValueA.GetValue().Double : 0.0;
		const double ValueB = OptionalValueB.IsSet() ? OptionalValueB.GetValue().Double : 0.0;

		if (ValueA == ValueB)
		{
			INSIGHTS_DEFAULT_SORTING_NODES(A, B)
		}
		else
		{
			// Sort by value (descending).
			return ValueB < ValueA;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FSorterByDoubleValue::Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const
{
	// Note: This implementation is faster than using above delegates.
	//       It caches the values before sorting, in this way minimizes the calls to Column.GetValue().

	const FTableColumn& Column = *ColumnRef;

	const int32 NumElements = NodesToSort.Num();

	struct FSortElement
	{
		const FBaseTreeNodePtr NodePtr;
		double Value;
	};
	TArray<FSortElement> ElementsToSort;

	// Cache value for each node.
	ElementsToSort.Reset(NumElements);
	for (const FBaseTreeNodePtr& NodePtr : NodesToSort)
	{
		const TOptional<FTableCellValue> OptionalValue = Column.GetValue(*NodePtr);
		const double Value = OptionalValue.IsSet() ? OptionalValue.GetValue().Double : 0.0;
		ElementsToSort.Add({ NodePtr, Value });
	}
	ensure(ElementsToSort.Num() == NumElements);

	if (SortMode == ESortMode::Ascending)
	{
		ElementsToSort.Sort([](const FSortElement& A, const FSortElement& B) -> bool
		{
			if (A.Value == B.Value)
			{
				INSIGHTS_DEFAULT_SORTING_ELEMENTS(A, B)
			}
			else
			{
				// Sort by value (ascending).
				return A.Value < B.Value;
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		ElementsToSort.Sort([](const FSortElement& A, const FSortElement& B) -> bool
		{
			if (A.Value == B.Value)
			{
				INSIGHTS_DEFAULT_SORTING_ELEMENTS(A, B)
			}
			else
			{
				// Sort by value (descending).
				return B.Value < A.Value;
			}
		});
	}

	for (int32 Index = 0; Index < NumElements; ++Index)
	{
		NodesToSort[Index] = ElementsToSort[Index].NodePtr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FSorterByCStringValue
////////////////////////////////////////////////////////////////////////////////////////////////////

FSorterByCStringValue::FSorterByCStringValue(TSharedRef<FTableColumn> InColumnRef)
	: FBaseTableColumnSorter(InColumnRef)
{
	const FTableColumn& Column = *InColumnRef;

	ensure(Column.GetDataType() == ETableCellDataType::CString);

	AscendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		const TCHAR* ValueA = OptionalValueA.IsSet() ? OptionalValueA.GetValue().CString : nullptr;
		const TCHAR* ValueB = OptionalValueB.IsSet() ? OptionalValueB.GetValue().CString : nullptr;

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			//return A->GetName().LexicalLess(B->GetName()); // so slow :(

			// Sort by node id (ascending).
			return A->GetId() < B->GetId();
		}
		else
		{
			// Sort by value (ascending).
			return FCString::Strcmp(ValueA, ValueB) < 0;
		}
	};

	DescendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		const TCHAR* ValueA = OptionalValueA.IsSet() ? OptionalValueA.GetValue().CString : nullptr;
		const TCHAR* ValueB = OptionalValueB.IsSet() ? OptionalValueB.GetValue().CString : nullptr;

		if (ValueA == ValueB)
		{
			// Sort by name (ascending).
			//return A->GetName().LexicalLess(B->GetName()); // so slow :(

			// Sort by node id (ascending).
			return A->GetId() < B->GetId();
		}
		else
		{
			// Sort by value (descending).
			return FCString::Strcmp(ValueB, ValueA) < 0;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FSorterByTextValue
////////////////////////////////////////////////////////////////////////////////////////////////////

FSorterByTextValue::FSorterByTextValue(TSharedRef<FTableColumn> InColumnRef)
	: FBaseTableColumnSorter(InColumnRef)
{
	const FTableColumn& Column = *InColumnRef;

	ensure(Column.GetDataType() == ETableCellDataType::Text);

	AscendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		if (!OptionalValueB.IsSet() || !OptionalValueB.GetValue().TextPtr.IsValid())
		{
			return true;
		}

		if (!OptionalValueA.IsSet() || !OptionalValueA.GetValue().TextPtr.IsValid())
		{
			return false;
		}

		const FText& ValueA = *OptionalValueA.GetValue().TextPtr;
		const FText& ValueB = *OptionalValueB.GetValue().TextPtr;

		if (ValueA.EqualTo(ValueB))
		{
			// Sort by name (ascending).
			//return A->GetName().LexicalLess(B->GetName()); // so slow :(

			// Sort by node id (ascending).
			return A->GetId() < B->GetId();
		}
		else
		{
			// Sort by value (ascending).
			return ValueA.CompareTo(ValueB) < 0;
		}
	};

	DescendingCompareDelegate = [&Column](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
	{
		const TOptional<FTableCellValue> OptionalValueA = Column.GetValue(*A);
		const TOptional<FTableCellValue> OptionalValueB = Column.GetValue(*B);

		if (!OptionalValueB.IsSet() || !OptionalValueB.GetValue().TextPtr.IsValid())
		{
			return true;
		}

		if (!OptionalValueA.IsSet() || !OptionalValueA.GetValue().TextPtr.IsValid())
		{
			return false;
		}

		const FText& ValueA = *OptionalValueA.GetValue().TextPtr;
		const FText& ValueB = *OptionalValueB.GetValue().TextPtr;

		if (ValueA.EqualTo(ValueB))
		{
			// Sort by name (ascending).
			//return A->GetName().LexicalLess(B->GetName()); // so slow :(

			// Sort by node id (ascending).
			return A->GetId() < B->GetId();
		}
		else
		{
			// Sort by value (descending).
			return ValueB.CompareTo(ValueA) < 0;
		}
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef INSIGHTS_DEFAULT_SORTING_NODES
#undef INSIGHTS_DEFAULT_SORTING_ELEMENTS
#undef LOCTEXT_NAMESPACE
