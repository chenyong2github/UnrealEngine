// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonInputActionDomain.h"

bool UCommonInputActionDomain::ShouldBreakInnerEventFlow(bool bInputEventHandled) const
{
	switch (InnerBehavior)
	{
		case ECommonInputEventFlowBehavior::BlockIfActive:
		{
			return true;
		}
		case ECommonInputEventFlowBehavior::BlockIfHandled:
		{
			return bInputEventHandled;
		}
		case ECommonInputEventFlowBehavior::NeverBlock:
		{
			return false;
		}
	}

	return false;
}

bool UCommonInputActionDomain::ShouldBreakEventFlow(bool bDomainHadActiveRoots, bool bInputEventHandledAtLeastOnce) const
{
	switch (Behavior)
	{
		case ECommonInputEventFlowBehavior::BlockIfActive:
		{
			return bDomainHadActiveRoots;
		}
		case ECommonInputEventFlowBehavior::BlockIfHandled:
		{
			return bInputEventHandledAtLeastOnce;
		}
		case ECommonInputEventFlowBehavior::NeverBlock:
		{
			return false;
		}
	}

	return false;
}

void UCommonInputActionDomainTable::PostLoad()
{
	for (UCommonInputActionDomain* ActionDomain : ActionDomains)
	{
		if (ActionDomain && ActionDomain->bIsDefaultActionDomain)
		{
			DefaultActionDomainCache = ActionDomain;
			break;
		}
	}

	Super::PostLoad();
}