// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldConditionContext.h"


bool FWorldConditionContext::Activate() const
{
	check(QueryState.IsValid());

	for (int32 Index = 0; Index < QueryDefinition.Conditions.Num(); Index++)
	{
		const FWorldConditionBase& Condition = QueryDefinition.Conditions[Index].Get<FWorldConditionBase>();
		FWorldConditionItem& Item = QueryState.GetItem(Index);
		Item.Operator = Condition.Operator;
		Item.NextExpressionDepth = Condition.NextExpressionDepth;
	}

	bool bSuccess = true;
	for (int32 Index = 0; Index < QueryDefinition.Conditions.Num(); Index++)
	{
		const FWorldConditionBase& Condition = QueryDefinition.Conditions[Index].Get<FWorldConditionBase>();
		bSuccess &= Condition.Activate(*this);
	}

	if (!bSuccess)
	{
		Deactivate();
		QueryState.Free(QueryDefinition);
	}
	
	return bSuccess;
}

bool FWorldConditionContext::IsTrue() const
{
	if (!QueryState.IsValid())
	{
		return false;
	}

	if (QueryState.CachedResult != EWorldConditionResult::Invalid)
	{
		return QueryState.CachedResult == EWorldConditionResult::IsTrue;
	}
	
	static_assert(UE::WorldCondition::MaxExpressionDepth == 4);
	EWorldConditionResult Results[UE::WorldCondition::MaxExpressionDepth + 1] = { EWorldConditionResult::Invalid, EWorldConditionResult::Invalid, EWorldConditionResult::Invalid, EWorldConditionResult::Invalid, EWorldConditionResult::Invalid };
	EWorldConditionOperator Operators[UE::WorldCondition::MaxExpressionDepth + 1] = { EWorldConditionOperator::Copy, EWorldConditionOperator::Copy, EWorldConditionOperator::Copy, EWorldConditionOperator::Copy, EWorldConditionOperator::Copy };
	int32 Depth = 0;

	for (int32 Index = 0; Index < QueryState.GetNumConditions(); Index++)
	{
		FWorldConditionItem& Item = QueryState.GetItem(Index);
		const int32 NextExpressionDepth = Item.NextExpressionDepth;

		Operators[Depth] = Item.Operator;

		Depth = FMath::Max(Depth, NextExpressionDepth);
		
		EWorldConditionResult CurrResult = Item.CachedResult;
		if (CurrResult == EWorldConditionResult::Invalid)
		{
			check(QueryDefinition.Conditions.Num() == QueryState.GetNumConditions());
			const FWorldConditionBase& Condition = QueryDefinition.Conditions[Index].Get<FWorldConditionBase>();
			CurrResult = Condition.IsTrue(*this);
			if (Condition.bCanCacheResult)
			{
				Item.CachedResult = CurrResult;
			}
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

	QueryState.CachedResult = Results[0]; 
	
	return QueryState.CachedResult == EWorldConditionResult::IsTrue;

}

void FWorldConditionContext::Deactivate() const
{
	check(QueryState.IsValid());
	
	for (int32 Index = 0; Index < QueryDefinition.Conditions.Num(); Index++)
	{
		const FWorldConditionBase& ConditionDef = QueryDefinition.Conditions[Index].Get<FWorldConditionBase>();
		ConditionDef.Deactivate(*this);
	}

	QueryState.Free(QueryDefinition);

}
