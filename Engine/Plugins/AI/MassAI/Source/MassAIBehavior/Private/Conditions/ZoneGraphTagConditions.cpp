// Copyright Epic Games, Inc. All Rights Reserved.

#include "Conditions/ZoneGraphTagConditions.h"
#include "StateTreeExecutionContext.h"
#if WITH_EDITOR
#include "ZoneGraphSettings.h"
#include "StateTreePropertyBindings.h"
#endif// WITH_EDITOR

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE::MassBehavior::ZoneGraph
{
	FText GetTagName(const FZoneGraphTag Tag)
	{
		const UZoneGraphSettings* ZoneGraphSettings = GetDefault<UZoneGraphSettings>();
		check(ZoneGraphSettings);
		TConstArrayView<FZoneGraphTagInfo> TagInfos = ZoneGraphSettings->GetTagInfos();
		
		for (const FZoneGraphTagInfo& TagInfo : TagInfos)
		{
			if (TagInfo.Tag == Tag)
			{
				return FText::FromName(TagInfo.Name);
			}
		}
		return FText::GetEmpty();
	}

	FText GetTagMaskName(const FZoneGraphTagMask TagMask)
	{
		const UZoneGraphSettings* ZoneGraphSettings = GetDefault<UZoneGraphSettings>();
		check(ZoneGraphSettings);
		TConstArrayView<FZoneGraphTagInfo> TagInfos = ZoneGraphSettings->GetTagInfos();

		TArray<FText> Names;
		for (const FZoneGraphTagInfo& Info : TagInfos)
		{
			if (TagMask.Contains(Info.Tag))
			{
				if (Info.IsValid())
				{
					Names.Add(FText::FromName(Info.Name));
				}
			}
		}
		if (Names.Num() == 0)
		{
			return LOCTEXT("EmptyMask", "(Empty)");
		}
		
		if (Names.Num() > 2)
		{
			Names.SetNum(2);
			Names.Add(FText::FromString(TEXT("...")));
		}
		
		return FText::Join(FText::FromString(TEXT(", ")), Names);
	}

	FText GetMaskOperatorText(const EZoneLaneTagMaskComparison Operator)
	{
		switch (Operator)
		{
		case EZoneLaneTagMaskComparison::Any:
			return LOCTEXT("ContainsAny", "Any");
		case EZoneLaneTagMaskComparison::All:
			return LOCTEXT("ContainsAll", "All");
		case EZoneLaneTagMaskComparison::Not:
			return LOCTEXT("ContainsNot", "Not");
		default:
			return FText::FromString(TEXT("??"));
		}
		return FText::GetEmpty();
	}

}

#endif// WITH_EDITOR


//----------------------------------------------------------------------//
//  FZoneGraphTagFilterCondition
//----------------------------------------------------------------------//

bool FZoneGraphTagFilterCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(TagsHandle, STATETREE_INSTANCEDATA_PROPERTY(FZoneGraphTagFilterConditionInstanceData, Tags));

	return true;
}

bool FZoneGraphTagFilterCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FZoneGraphTagMask Tags = Context.GetInstanceData(TagsHandle);
	return Filter.Pass(Tags) ^ bInvert;
}

#if WITH_EDITOR
FText FZoneGraphTagFilterCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const
{
	const FStateTreeEditorPropertyPath LeftPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FZoneGraphTagFilterConditionInstanceData, Tags));

	FText InvertText;
	if (bInvert)
	{
		InvertText = LOCTEXT("Not", "Not");
	}

	FText LeftText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(LeftPath))
	{
		LeftText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		LeftText = LOCTEXT("NotBound", "Not Bound");
	}

	FText FilterParts[6] = { FText::GetEmpty(), FText::GetEmpty(), FText::GetEmpty(), FText::GetEmpty(), FText::GetEmpty(), FText::GetEmpty() };
	int32 PartIndex = 0;
	if (Filter.AnyTags != FZoneGraphTagMask::None)
	{
		FilterParts[PartIndex++] = UE::MassBehavior::ZoneGraph::GetMaskOperatorText(EZoneLaneTagMaskComparison::Any);
		FilterParts[PartIndex++] = UE::MassBehavior::ZoneGraph::GetTagMaskName(Filter.AnyTags);
	}

	if (Filter.AllTags != FZoneGraphTagMask::None)
	{
		FilterParts[PartIndex++] = UE::MassBehavior::ZoneGraph::GetMaskOperatorText(EZoneLaneTagMaskComparison::All);
		FilterParts[PartIndex++] = UE::MassBehavior::ZoneGraph::GetTagMaskName(Filter.AllTags);
	}

	if (Filter.NotTags != FZoneGraphTagMask::None)
	{
		FilterParts[PartIndex++] = UE::MassBehavior::ZoneGraph::GetMaskOperatorText(EZoneLaneTagMaskComparison::Not);
		FilterParts[PartIndex++] = UE::MassBehavior::ZoneGraph::GetTagMaskName(Filter.NotTags);
	}

	return FText::Format(LOCTEXT("CompareZoneGraphTagFilterDesc", "{0} <Details.Bold>{1}</> contains {2} <Details.Bold>{3}</> {4} <Details.Bold>{5}</> {6} <Details.Bold>{7}</>"),
		InvertText, LeftText, FilterParts[0], FilterParts[1], FilterParts[2], FilterParts[3], FilterParts[4], FilterParts[5]);
}
#endif// WITH_EDITOR


//----------------------------------------------------------------------//
//  FZoneGraphTagMaskCondition
//----------------------------------------------------------------------//

bool FZoneGraphTagMaskCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(LeftHandle, STATETREE_INSTANCEDATA_PROPERTY(FZoneGraphTagMaskConditionInstanceData, Left));
	Linker.LinkInstanceDataProperty(RightHandle, STATETREE_INSTANCEDATA_PROPERTY(FZoneGraphTagMaskConditionInstanceData, Right));

	return true;
}

bool FZoneGraphTagMaskCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FZoneGraphTagMask Left = Context.GetInstanceData(LeftHandle);
	const FZoneGraphTagMask Right = Context.GetInstanceData(RightHandle);
	return Left.CompareMasks(Right, Operator) ^ bInvert;
}

#if WITH_EDITOR
FText FZoneGraphTagMaskCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const
{
	const FZoneGraphTagMaskConditionInstanceData& Instance = InstanceData.Get<FZoneGraphTagMaskConditionInstanceData>();
	const FStateTreeEditorPropertyPath LeftPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FZoneGraphTagMaskConditionInstanceData, Left));
	const FStateTreeEditorPropertyPath RightPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FZoneGraphTagMaskConditionInstanceData, Right));

	FText InvertText;
	if (bInvert)
	{
		InvertText = LOCTEXT("Not", "Not");
	}

	FText LeftText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(LeftPath))
	{
		LeftText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		LeftText = LOCTEXT("NotBound", "Not Bound");
	}

	FText OperatorText = UE::MassBehavior::ZoneGraph::GetMaskOperatorText(Operator);

	FText RightText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(RightPath))
	{
		RightText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		RightText = UE::MassBehavior::ZoneGraph::GetTagMaskName(Instance.Right);
	}

	return FText::Format(LOCTEXT("CompareZoneGraphTagMaskDesc", "{0} <Details.Bold>{1}</> contains {2} <Details.Bold>{3}</>"),
		InvertText, LeftText, OperatorText, RightText);
}
#endif// WITH_EDITOR


//----------------------------------------------------------------------//
//  FZoneGraphTagCondition
//----------------------------------------------------------------------//

bool FZoneGraphTagCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(LeftHandle, STATETREE_INSTANCEDATA_PROPERTY(FZoneGraphTagConditionInstanceData, Left));
	Linker.LinkInstanceDataProperty(RightHandle, STATETREE_INSTANCEDATA_PROPERTY(FZoneGraphTagConditionInstanceData, Right));

	return true;
}

bool FZoneGraphTagCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FZoneGraphTag Left = Context.GetInstanceData(LeftHandle);
	const FZoneGraphTag Right = Context.GetInstanceData(RightHandle);
	return (Left == Right) ^ bInvert;
}

#if WITH_EDITOR
FText FZoneGraphTagCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const
{
	const FZoneGraphTagConditionInstanceData& Instance = InstanceData.Get<FZoneGraphTagConditionInstanceData>();
	const FStateTreeEditorPropertyPath LeftPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FZoneGraphTagConditionInstanceData, Left));
	const FStateTreeEditorPropertyPath RightPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FZoneGraphTagConditionInstanceData, Right));

	FText InvertText;
	if (bInvert)
	{
		InvertText = LOCTEXT("Not", "Not");
	}

	FText LeftText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(LeftPath))
	{
		LeftText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		LeftText = LOCTEXT("NotBound", "Not Bound");
	}

	FText RightText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(RightPath))
	{
		RightText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		RightText = UE::MassBehavior::ZoneGraph::GetTagName(Instance.Right);
	}

	return FText::Format(LOCTEXT("CompareZoneGraphTagDesc", "{0} <Details.Bold>{1}</> is <Details.Bold>{2}</>"),
		InvertText, LeftText, RightText);
}
#endif// WITH_EDITOR

#if WITH_EDITOR
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
