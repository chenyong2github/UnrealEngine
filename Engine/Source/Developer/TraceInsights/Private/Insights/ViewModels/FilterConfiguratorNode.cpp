// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterConfiguratorNode.h"
#include "Insights/ViewModels/Filters.h"

// Insights

#define LOCTEXT_NAMESPACE "FilterConfiguratorNode"

////////////////////////////////////////////////////////////////////////////////////////////////////

const FName FFilterConfiguratorNode::TypeName(TEXT("FFilterConfiguratorNode"));

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterConfiguratorNode::FFilterConfiguratorNode(const FName InName, bool bInIsGroup)
	: FBaseTreeNode(InName, bInIsGroup)
{
	if (bInIsGroup)
	{
		SelectedFilterGroupOperator = GetFilterGroupOperators()[0];
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterConfiguratorNode::FFilterConfiguratorNode(const FFilterConfiguratorNode& Other)
	: FBaseTreeNode(Other.GetName(), Other.IsGroup())
{
	*this = Other;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterConfiguratorNode& FFilterConfiguratorNode::operator=(const FFilterConfiguratorNode& Other)
{
	GetChildrenMutable().Empty();

	AvailableFilters = Other.AvailableFilters;
	SelectedFilter = Other.SelectedFilter;
	SelectedFilterOperator = Other.SelectedFilterOperator;
	SelectedFilterGroupOperator = Other.SelectedFilterGroupOperator;
	TextBoxValue = Other.TextBoxValue;
	SetExpansion(Other.IsExpanded());

	for (Insights::FBaseTreeNodePtr Child : Other.GetChildren())
	{
		GetChildrenMutable().Add(MakeShared<FFilterConfiguratorNode>(*StaticCastSharedPtr<FFilterConfiguratorNode>(Child)));
	}

	return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TArray<TSharedPtr<struct FFilterGroupOperator>>& FFilterConfiguratorNode::GetFilterGroupOperators()
{
	return FFilterService::Get()->GetFilterGroupOperators();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFilterConfiguratorNode::DeleteChildNode(FFilterConfiguratorNodePtr InNode)
{
	Insights::FBaseTreeNodePtr Node = StaticCastSharedPtr<Insights::FBaseTreeNode>(InNode);
	GetChildrenMutable().Remove(Node);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFilterConfiguratorNode::SetGroupPtrForChildren()
{
	for (Insights::FBaseTreeNodePtr Child : GetChildrenMutable())
	{
		FFilterConfiguratorNodePtr CastedChild = StaticCastSharedPtr<FFilterConfiguratorNode>(Child);
		CastedChild->SetGroupPtrForChildren();
		CastedChild->SetGroupPtr(SharedThis(this));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFilterConfiguratorNode::SetAvailableFilters(TSharedPtr<TArray<TSharedPtr<struct FFilter>>> InAvailableFilters)
{
	AvailableFilters = InAvailableFilters;

	if (AvailableFilters.IsValid() && AvailableFilters->Num() > 0)
	{
		SetSelectedFilter(AvailableFilters->GetData()[0]);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFilterConfiguratorNode::SetSelectedFilter(TSharedPtr<struct FFilter> InSelectedFilter)
{
	SelectedFilter = InSelectedFilter;
	if (SelectedFilter.IsValid() && SelectedFilter->GetSupportedOperators()->Num() > 0)
	{
		SetSelectedFilterOperator(SelectedFilter->GetSupportedOperators()->GetData()[0]);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFilterConfiguratorNode::ApplyFilters(const FFilterContext& Context) const
{
	bool ret = true;
	if (IsGroup())
	{
		if (SelectedFilterGroupOperator->Type == EFilterGroupOperator::And)
		{
			auto& ChildrenArr = GetChildren();
			for (int Index = 0; Index < ChildrenArr.Num(); ++Index)
			{
				ret &= ((FFilterConfiguratorNode*)&*ChildrenArr[Index])->ApplyFilters(Context);
			}
		}

		if (SelectedFilterGroupOperator->Type == EFilterGroupOperator::Or)
		{
			ret = false;

			auto& ChildrenArr = GetChildren();
			for (int Index = 0; Index < ChildrenArr.Num(); ++Index)
			{
				ret |= ((FFilterConfiguratorNode*)&*ChildrenArr[Index])->ApplyFilters(Context);
			}
		}
	}
	else
	{
		switch (SelectedFilter->DataType)
		{
		case EFilterDataType::Double:
			{
				FFilterOperator<double>* Operator = (FFilterOperator<double>*)&*SelectedFilterOperator;
				double Value = 0.0;
				Context.GetFilterData<double>(SelectedFilter->Key, Value);

				ret = Operator->Func(Value, FCString::Atod(*TextBoxValue));
				break;
			}

		case EFilterDataType::Int64:
		{
			FFilterOperator<int64>* Operator = (FFilterOperator<int64>*) & *SelectedFilterOperator;
			int64 Value = 0;
			Context.GetFilterData<int64>(SelectedFilter->Key, Value);

			ret = Operator->Func(Value, FCString::Atoi64(*TextBoxValue));
			break;
		}
		default:
			break;
		}
	}

	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
