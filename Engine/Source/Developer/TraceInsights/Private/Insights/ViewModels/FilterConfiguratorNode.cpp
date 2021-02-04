// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterConfiguratorNode.h"
#include "Insights/ViewModels/Filters.h"

// Insights

#define LOCTEXT_NAMESPACE "FilterConfiguratorNode"

namespace Insights
{

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

	AvailableFilterOperators = MakeShared<TArray<TSharedPtr<IFilterOperator>>>();
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
	AvailableFilterOperators = Other.AvailableFilterOperators;
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

bool FFilterConfiguratorNode::operator==(const FFilterConfiguratorNode& Other)
{
	bool bIsEqual = true;
	bIsEqual &= AvailableFilters.Get() == Other.AvailableFilters.Get();
	bIsEqual &= SelectedFilter.Get() == Other.SelectedFilter.Get();
	bIsEqual &= SelectedFilterOperator.Get() == Other.SelectedFilterOperator.Get();
	bIsEqual &= SelectedFilterGroupOperator.Get() == Other.SelectedFilterGroupOperator.Get();
	bIsEqual &= TextBoxValue == Other.TextBoxValue;
	bIsEqual &= GetChildren().Num() == Other.GetChildren().Num();

	if (bIsEqual)
	{
		for (int32 Index = 0; Index < GetChildren().Num(); ++Index)
		{
			bIsEqual &= *StaticCastSharedPtr<FFilterConfiguratorNode>(GetChildren()[Index]) == *StaticCastSharedPtr<FFilterConfiguratorNode>(Other.GetChildren()[Index]);
		}
	}

	return bIsEqual;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TArray<TSharedPtr<struct FFilterGroupOperator>>& FFilterConfiguratorNode::GetFilterGroupOperators()
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

		AvailableFilterOperators->Empty();
		Insights::FFilter::SupportedOperatorsArrayPtr AvailableOperators = InSelectedFilter->SupportedOperators;
		for (auto& FilterOperator : *AvailableOperators)
		{
			AvailableFilterOperators->Add(FilterOperator);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFilterConfiguratorNode::ProcessFilter()
{
	if (IsGroup())
	{
		TArray<Insights::FBaseTreeNodePtr> ChildArray = GetChildrenMutable();
		for (Insights::FBaseTreeNodePtr Child : ChildArray)
		{
			FFilterConfiguratorNodePtr CastedChild = StaticCastSharedPtr<FFilterConfiguratorNode>(Child);
			CastedChild->ProcessFilter();
		}
	}
	else
	{
		switch (SelectedFilter->DataType)
		{
		case EFilterDataType::Double:
		{
			FilterValue.Set<double>(FCString::Atod(*TextBoxValue));
			break;
		}
		case EFilterDataType::Int64:
		{
			FilterValue.Set<int64>(FCString::Atoi64(*TextBoxValue));
			break;
		}
		case EFilterDataType::String:
		{
			FilterValue.Set<FString>(TextBoxValue);
			break;
		}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFilterConfiguratorNode::ApplyFilters(const FFilterContext& Context) const
{
	bool Ret = true;
	if (IsGroup())
	{
		switch (SelectedFilterGroupOperator->Type)
		{
		case EFilterGroupOperator::And:
		{
			auto& ChildrenArr = GetChildren();
			for (int Index = 0; Index < ChildrenArr.Num() && Ret; ++Index)
			{
				Ret &= ((FFilterConfiguratorNode*)ChildrenArr[Index].Get())->ApplyFilters(Context);
			}
			break;
		}
		case EFilterGroupOperator::Or:
		{
			auto& ChildrenArr = GetChildren();
			if (ChildrenArr.Num() > 0)
			{
				Ret = false;
			}

			for (int Index = 0; Index < ChildrenArr.Num() && !Ret; ++Index)
			{
				Ret |= ((FFilterConfiguratorNode*)ChildrenArr[Index].Get())->ApplyFilters(Context);
			}
			break;
		}
		}
	}
	else
	{
		switch (SelectedFilter->DataType)
		{
		case EFilterDataType::Double:
		{
			FFilterOperator<double>* Operator = (FFilterOperator<double>*) SelectedFilterOperator.Get();
			double Value = 0.0;
			Context.GetFilterData<double>(SelectedFilter->Key, Value);

			Ret = Operator->Func(Value, FilterValue.Get<double>());
			break;
		}
		case EFilterDataType::Int64:
		{
			FFilterOperator<int64>* Operator = (FFilterOperator<int64>*) SelectedFilterOperator.Get();
			int64 Value = 0;
			Context.GetFilterData<int64>(SelectedFilter->Key, Value);

			Ret = Operator->Func(Value, FilterValue.Get<int64>());
			break;
		}
		case EFilterDataType::String:
		{
			FFilterOperator<FString>* Operator = (FFilterOperator<FString>*) SelectedFilterOperator.Get();
			FString Value;
			Context.GetFilterData<FString>(SelectedFilter->Key, Value);

			Ret = Operator->Func(Value, FilterValue.Get<FString>());
			break;
		}
		default:
			break;
		}
	}

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
