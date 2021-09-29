// Copyright Epic Games, Inc. All Rights Reserved.

#include "Conditions/ZoneGraphTagConditions.h"
#if WITH_EDITOR
#include "ZoneGraphSettings.h"
#include "StateTreePropertyBindings.h"

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

FText FZoneGraphTagFilterCondition::GetDescription(const IStateTreeBindingLookup& BindingLookup) const
{
	const FStateTreeEditorPropertyPath LeftPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FZoneGraphTagFilterCondition, Tags));

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
		LeftText = UE::MassBehavior::ZoneGraph::GetTagMaskName(Tags);
	}

	FText FilterParts[6] = { FText::GetEmpty(), FText::GetEmpty(), FText::GetEmpty(), FText::GetEmpty(), FText::GetEmpty(), FText::GetEmpty() };
	int32 PartIndex = 0;
	if (Filter.AnyTags != FZoneGraphTagMask::None)
	{
		FilterParts[PartIndex++] = LOCTEXT("ContainsAny", "any");
		FilterParts[PartIndex++] = UE::MassBehavior::ZoneGraph::GetTagMaskName(Filter.AnyTags);
	}

	if (Filter.AllTags != FZoneGraphTagMask::None)
	{
		FilterParts[PartIndex++] = LOCTEXT("ContainsAll", "all");
		FilterParts[PartIndex++] = UE::MassBehavior::ZoneGraph::GetTagMaskName(Filter.AllTags);
	}

	if (Filter.NotTags != FZoneGraphTagMask::None)
	{
		FilterParts[PartIndex++] = LOCTEXT("ContainsNot", "not");
		FilterParts[PartIndex++] = UE::MassBehavior::ZoneGraph::GetTagMaskName(Filter.NotTags);
	}

	return FText::Format(LOCTEXT("CompareZoneGraphTagFilterDesc", "{0} <Details.Bold>{1}</> contains {2} <Details.Bold>{3}</> {4} <Details.Bold>{5}</> {6} <Details.Bold>{7}</>"), InvertText, LeftText, FilterParts[0], FilterParts[1], FilterParts[2], FilterParts[3], FilterParts[4], FilterParts[5]);
}


FText FZoneGraphTagMaskCondition::GetDescription(const IStateTreeBindingLookup& BindingLookup) const
{
	const FStateTreeEditorPropertyPath LeftPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FZoneGraphTagMaskCondition, Left));
	const FStateTreeEditorPropertyPath RightPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FZoneGraphTagMaskCondition, Right));

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
		LeftText = UE::MassBehavior::ZoneGraph::GetTagMaskName(Left);
	}

	FText OperatorText = UE::MassBehavior::ZoneGraph::GetMaskOperatorText(Operator);

	FText RightText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(RightPath))
	{
		RightText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		RightText = UE::MassBehavior::ZoneGraph::GetTagMaskName(Right);
	}

	return FText::Format(LOCTEXT("CompareZoneGraphTagMaskDesc", "{0} <Details.Bold>{1}</> contains {2} <Details.Bold>{3}</>"), InvertText, LeftText, OperatorText, RightText);
}


FText FZoneGraphTagCondition::GetDescription(const IStateTreeBindingLookup& BindingLookup) const
{
	const FStateTreeEditorPropertyPath LeftPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FZoneGraphTagCondition, Left));
	const FStateTreeEditorPropertyPath RightPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FZoneGraphTagCondition, Right));

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
		LeftText = UE::MassBehavior::ZoneGraph::GetTagName(Left);
	}

	FText RightText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(RightPath))
	{
		RightText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		RightText = UE::MassBehavior::ZoneGraph::GetTagName(Right);
	}

	return FText::Format(LOCTEXT("CompareZoneGraphTagDesc", "{0} <Details.Bold>{1}</> is <Details.Bold>{3}</>"), InvertText, LeftText, RightText);
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
