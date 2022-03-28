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
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
