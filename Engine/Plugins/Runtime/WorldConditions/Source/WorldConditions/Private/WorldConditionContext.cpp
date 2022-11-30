// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldConditionContext.h"


bool FWorldConditionContext::Activate() const
{
	if (!QueryState.IsInitialized())
	{
		return false;
	}

	const UWorldConditionQuerySharedDefinition* SharedDefinition = QueryState.SharedDefinition;
	if (SharedDefinition == nullptr)
	{
		// Initialized but no definition means empty query. Activating empty should succeed.
		return true;
	}

	for (int32 Index = 0; Index < SharedDefinition->Conditions.Num(); Index++)
	{
		const FWorldConditionBase& Condition = SharedDefinition->Conditions[Index].Get<FWorldConditionBase>();
		FWorldConditionItem& Item = QueryState.GetItem(Index);
		Item.Operator = Condition.Operator;
		Item.NextExpressionDepth = Condition.NextExpressionDepth;
	}

	bool bSuccess = true;
	for (int32 Index = 0; Index < SharedDefinition->Conditions.Num(); Index++)
	{
		const FWorldConditionBase& Condition = SharedDefinition->Conditions[Index].Get<FWorldConditionBase>();
		bSuccess &= Condition.Activate(*this);
	}

	if (!bSuccess)
	{
		Deactivate();
	}
	
	return bSuccess;
}

bool FWorldConditionContext::IsTrue() const
{
	if (!QueryState.IsInitialized())
	{
		return false;
	}

	if (QueryState.GetCachedResult() != EWorldConditionResult::Invalid)
	{
		return QueryState.GetCachedResult() == EWorldConditionResult::IsTrue;
	}

	const UWorldConditionQuerySharedDefinition* SharedDefinition = QueryState.SharedDefinition;
	if (SharedDefinition == nullptr)
	{
		// Empty query is true.
		return true;
	}

	static_assert(UE::WorldCondition::MaxExpressionDepth == 4);
	EWorldConditionResult Results[UE::WorldCondition::MaxExpressionDepth + 1] = { EWorldConditionResult::Invalid, EWorldConditionResult::Invalid, EWorldConditionResult::Invalid, EWorldConditionResult::Invalid, EWorldConditionResult::Invalid };
	EWorldConditionOperator Operators[UE::WorldCondition::MaxExpressionDepth + 1] = { EWorldConditionOperator::Copy, EWorldConditionOperator::Copy, EWorldConditionOperator::Copy, EWorldConditionOperator::Copy, EWorldConditionOperator::Copy };
	int32 Depth = 0;

	bool bAllConditionsCanBeCached = true;

	for (int32 Index = 0; Index < QueryState.GetNumConditions(); Index++)
	{
		FWorldConditionItem& Item = QueryState.GetItem(Index);
		const int32 NextExpressionDepth = Item.NextExpressionDepth;

		Operators[Depth] = Item.Operator;

		Depth = FMath::Max(Depth, NextExpressionDepth);
		
		EWorldConditionResult CurrResult = Item.CachedResult;
		if (CurrResult == EWorldConditionResult::Invalid)
		{
			check(SharedDefinition->Conditions.Num() == QueryState.GetNumConditions());
			const FWorldConditionBase& Condition = SharedDefinition->Conditions[Index].Get<FWorldConditionBase>();
			CurrResult = Condition.IsTrue(*this);
			CurrResult = UE::WorldCondition::Invert(CurrResult, Condition.bInvert); 
			
			if (Condition.bCanCacheResult)
			{
				Item.CachedResult = CurrResult;
			}
			
			bAllConditionsCanBeCached &= Condition.bCanCacheResult;
		}

		Depth++;
		Results[Depth] = CurrResult;

		while (Depth > NextExpressionDepth)
		{
			Depth--;
			Results[Depth] = UE::WorldCondition::MergeResults(Operators[Depth], Results[Depth], Results[Depth + 1]);
			Operators[Depth] = EWorldConditionOperator::Copy;
		}
	}

	const EWorldConditionResult FinalResult = Results[0];
	
	QueryState.SetCachedResult(bAllConditionsCanBeCached ? FinalResult : EWorldConditionResult::Invalid);
	
	return FinalResult == EWorldConditionResult::IsTrue;

}

void FWorldConditionContext::Deactivate() const
{
	if (!QueryState.IsInitialized())
	{
		return;
	}

	if (const UWorldConditionQuerySharedDefinition* SharedDefinition = QueryState.SharedDefinition)
	{
		for (int32 Index = 0; Index < SharedDefinition->Conditions.Num(); Index++)
		{
			const FWorldConditionBase& ConditionDef = SharedDefinition->Conditions[Index].Get<FWorldConditionBase>();
			ConditionDef.Deactivate(*this);
		}
	}

	QueryState.Free();
}
