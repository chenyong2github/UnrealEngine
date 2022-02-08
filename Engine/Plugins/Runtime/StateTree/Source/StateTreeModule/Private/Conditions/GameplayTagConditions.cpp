// Copyright Epic Games, Inc. All Rights Reserved.

#include "Conditions/GameplayTagConditions.h"
#include "GameplayTagContainer.h"
#include "StateTreeExecutionContext.h"
#if WITH_EDITOR
#include "StateTreePropertyBindings.h"
#endif// WITH_EDITOR

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE::StateTree::Conditions
{
	FText GetContainerAsText(const FGameplayTagContainer& TagContainer, const int ApproxMaxLength = 60)
	{
		FString Combined;
		for (const FGameplayTag& Tag : TagContainer)
		{
			FString TagString = Tag.ToString();

			if (Combined.Len() > 0)
			{
				Combined += TEXT(", ");
			}
			
			if (Combined.Len() + TagString.Len() > ApproxMaxLength)
			{
				// Overflow
				if (Combined.Len() == 0)
				{
					Combined += TagString.Left(ApproxMaxLength);
				}
				Combined += TEXT("...");
				break;
			}

			Combined += TagString;
		}

		return FText::FromString(Combined);
	}
	
}

#endif// WITH_EDITOR


//----------------------------------------------------------------------//
//  FGameplayTagMatchCondition
//----------------------------------------------------------------------//

bool FGameplayTagMatchCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(TagContainerHandle, STATETREE_INSTANCEDATA_PROPERTY(FGameplayTagMatchConditionInstanceData, TagContainer));
	Linker.LinkInstanceDataProperty(TagHandle, STATETREE_INSTANCEDATA_PROPERTY(FGameplayTagMatchConditionInstanceData, Tag));

	return true;
}

bool FGameplayTagMatchCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FGameplayTagContainer& TagContainer = Context.GetInstanceData(TagContainerHandle);
	const FGameplayTag& Tag = Context.GetInstanceData(TagHandle);

	return (bExactMatch ? TagContainer.HasTagExact(Tag) : TagContainer.HasTag(Tag)) ^ bInvert;
}

#if WITH_EDITOR
FText FGameplayTagMatchCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const
{
	const FGameplayTagMatchConditionInstanceData& Instance = InstanceData.Get<FGameplayTagMatchConditionInstanceData>();
	const FStateTreeEditorPropertyPath TagContainerPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FGameplayTagMatchConditionInstanceData, TagContainer));
	const FStateTreeEditorPropertyPath TagPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FGameplayTagMatchConditionInstanceData, Tag));

	FText InvertText;
	if (bInvert)
	{
		InvertText = LOCTEXT("Not", "Not");
	}

	FText TagContainerText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(TagContainerPath))
	{
		TagContainerText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		TagContainerText = UE::StateTree::Conditions::GetContainerAsText(Instance.TagContainer);
	}

	FText TagText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(TagContainerPath))
	{
		TagText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		TagText = FText::FromString(Instance.Tag.ToString());
	}

	return FText::Format(LOCTEXT("GameplayTagMatchDesc", "{0} <Details.Bold>{1}</> has <Details.Bold>{2}</>"),
		InvertText, TagContainerText, TagText);
}
#endif// WITH_EDITOR

//----------------------------------------------------------------------//
//  FGameplayTagContainerMatchCondition
//----------------------------------------------------------------------//

bool FGameplayTagContainerMatchCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(TagContainerHandle, STATETREE_INSTANCEDATA_PROPERTY(FGameplayTagContainerMatchConditionInstanceData, TagContainer));
	Linker.LinkInstanceDataProperty(OtherContainerHandle, STATETREE_INSTANCEDATA_PROPERTY(FGameplayTagContainerMatchConditionInstanceData, OtherContainer));

	return true;
}

bool FGameplayTagContainerMatchCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FGameplayTagContainer& TagContainer = Context.GetInstanceData(TagContainerHandle);
	const FGameplayTagContainer& OtherContainer = Context.GetInstanceData(OtherContainerHandle);

	bool bResult = false;
	switch (MatchType)
	{
	case EGameplayContainerMatchType::Any:
		bResult = bExactMatch ? TagContainer.HasAnyExact(OtherContainer) : TagContainer.HasAny(OtherContainer);
		break;
	case EGameplayContainerMatchType::All:
		bResult = bExactMatch ? TagContainer.HasAllExact(OtherContainer) : TagContainer.HasAll(OtherContainer);
		break;
	default:
		ensureMsgf(false, TEXT("Unhandled match type %s."), *UEnum::GetValueAsString(MatchType));
	}
	
	return bResult ^ bInvert;
}

#if WITH_EDITOR
FText FGameplayTagContainerMatchCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const
{
	const FGameplayTagContainerMatchConditionInstanceData& Instance = InstanceData.Get<FGameplayTagContainerMatchConditionInstanceData>();
	const FStateTreeEditorPropertyPath TagContainerPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FGameplayTagContainerMatchConditionInstanceData, TagContainer));
	const FStateTreeEditorPropertyPath OtherContainerPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FGameplayTagContainerMatchConditionInstanceData, OtherContainer));

	FText InvertText;
	if (bInvert)
	{
		InvertText = LOCTEXT("Not", "Not");
	}

	FText TagContainerText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(TagContainerPath))
	{
		TagContainerText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		TagContainerText = LOCTEXT("NotBound", "Not Bound");
	}

	FText MatchTypeText = UEnum::GetDisplayValueAsText(MatchType);

	FText OtherContainerText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(TagContainerPath))
	{
		OtherContainerText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		OtherContainerText = UE::StateTree::Conditions::GetContainerAsText(Instance.TagContainer);
	}

	return FText::Format(LOCTEXT("GameplayTagContainerMatchDesc", "{0} <Details.Bold>{1}</> {2} <Details.Bold>{3}</>"),
		InvertText, TagContainerText, MatchTypeText, OtherContainerText);
}
#endif// WITH_EDITOR

//----------------------------------------------------------------------//
//  FGameplayTagQueryCondition
//----------------------------------------------------------------------//

bool FGameplayTagQueryCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(TagContainerHandle, STATETREE_INSTANCEDATA_PROPERTY(FGameplayTagQueryConditionInstanceData, TagContainer));

	return true;
}

bool FGameplayTagQueryCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FGameplayTagContainer& TagContainer = Context.GetInstanceData(TagContainerHandle);
	return TagQuery.Matches(TagContainer) ^ bInvert;
}

#if WITH_EDITOR
FText FGameplayTagQueryCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const
{
	const FGameplayTagQueryConditionInstanceData& Instance = InstanceData.Get<FGameplayTagQueryConditionInstanceData>();
	const FStateTreeEditorPropertyPath TagContainerPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FGameplayTagQueryConditionInstanceData, TagContainer));

	FText InvertText;
	if (bInvert)
	{
		InvertText = LOCTEXT("Not", "Not");
	}

	FText TagContainerText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(TagContainerPath))
	{
		TagContainerText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		TagContainerText = LOCTEXT("NotBound", "Not Bound");
	}

	return FText::Format(LOCTEXT("GameplayTagQueryMatchDesc", "{0} <Details.Bold>{1}</> matches query</>"),
		InvertText, TagContainerText);
}
#endif// WITH_EDITOR


#if WITH_EDITOR
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
