// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldConditionTypes.h"
#include "WorldConditionBase.generated.h"

struct FWorldConditionContext;
struct FWorldConditionQueryState;
class UWorldConditionSchema;

/**
 * Base struct for all World Conditions.
 *
 * World Conditions are used together with World Condition Query to create expressions of conditions whose result can be checked.
 * The conditions can be based on globally accessible data (e.g. subsystems), or based on any of the context data accessible via FWorldConditionContext.
 * The data that is available is defined for each use case using a UWorldConditionSchema. It also defines which conditions are available when creating
 * a query in that use case.
 *
 * FWorldConditionContextDataRef allows to reference a specific context data. When added on a world condition as property, the UI allows to pick
 * context data of specified type, and in code, pointer to the actual data can be accessed via the context. 
 *
 *		UPROPERTY(EditAnywhere, Category="Default", meta=(BaseStruct="/Script/SmartObjectsModule.SmartObjectSlotHandle"))
 *		FWorldConditionContextDataRef Slot;
 *
 *	EWorldConditionResult FWorldConditionSlotTagQuery::IsTrue(const FWorldConditionContext& Context) const
 *	{
 *		if (FSmartObjectSlotHandle* SlotHandle = Context.GetMutableContextDataPtr<FSmartObjectSlotHandle>(Slot))
 *		{
 *		}
 *		...
 *	}
 *
 * Under the hood a reference is a name that needs to be turned into an index before use. This is done on Init().
 *	bool FWorldConditionSlotTagQuery::Init(const UWorldConditionSchema& Schema)
 *	{
 *		if (!Schema.ResolveContextDataRef<FSmartObjectSlotHandle>(Slot))
 *		{
 *			return false;
 *		}
 *		...
 *	}
 *
 * To speed up query evaluation, the result of a World Condition can be cached by World Condition Query. A condition can be cached, if it is based on
 * globally accessible data, or context data that is marked as Permanent. To indicate that a conditions result can be cached,
 * bCanCacheResult must be set to true. This is done on Init().
 *
 *	bool FWorldConditionSlotTagQuery::Init(const UWorldConditionSchema& Schema)
 *	{
 *		...
 *		bCanCacheResult = Schema.GetContextDataTypeByRef(Slot) == EWorldConditionContextDataType::Persistent)
 *		...
 *	}
 *
 * When the result is cached, it needs to be invalidated when new value arrives. This can be done using e.g. a delegate callback which
 * calls InvalidateResult() of the current condition. This call will invalidate the query state so that next time when IsTrue() is called
 * required condition will be re-evaluated. It is advised to do as little work as possible in the delegate callback.
 *
 *	bool FWorldConditionSlotTagQuery::Activate(const FWorldConditionContext& Context) const
 *	{
 *		if (Context.GetContextDataType(Slot) == EWorldConditionContextDataType::Persistent)
 *		{
 *			FOnSmartObjectEvent* SlotDelegate = ...;
 *			FStateType& State = Context.GetState(*this);
 *			State.DelegateHandle = SlotDelegate->AddLambda([this, &QueryState = Context.GetQueryState()]()
 *				{
 *					InvalidateResult(QueryState);
 *				});
 *			}
 *		}
 *		return false;
 *	}
 */
USTRUCT(meta=(Hidden))
struct WORLDCONDITIONS_API FWorldConditionBase
{
	GENERATED_BODY()

	FWorldConditionBase()
		: bIsStateObject(false)
		, bCanCacheResult(true)
	{
	}
	virtual ~FWorldConditionBase();

	/** @return The Instance data type of the condition. */
	virtual const UStruct* GetRuntimeStateType() const { return nullptr; }

	/** @retrun Description to be shown in the UI. */
	virtual FText GetDescription() const;

	/**
	 * Initializes the condition to be used with a specific schema.
	 * This is called on PostLoad(), or during editing to make sure the data stays in sync with the schema.
	 * This is the place to resolve all the Context Data References,
	 * and set bCanCacheResult based on if the context data type.
	 * @param Schema The schema to initialize the condition for.
	 * @return True if init succeeded. 
	 */
	virtual bool Initialize(const UWorldConditionSchema& Schema);

	/**
	 * Called to activate the condition.
	 * The state data for the conditions can be accessed via the Context.
	 * @param Context Context that stores the context data and state of the query.
	 * @return True if activation succeeded.
	 */
	virtual bool Activate(const FWorldConditionContext& Context) const;

	/**
	 * Called to check the condition state.
	 * The state data for the conditions can be accessed via the Context.
	 * @param Context Context that stores the context data and state of the query.
	 * @return The state of the condition.
	 */
	virtual EWorldConditionResult IsTrue(const FWorldConditionContext& Context) const;

	/**
	 * Called to deactivate the condition.
	 * The state data for the conditions can be accessed via the Context.
	 * @param Context Context that stores the context data and state of the query.
	 */
	virtual void Deactivate(const FWorldConditionContext& Context) const;

protected:
	/** Invalidates the query state so tha the required cached conditions are reevaluated on next call to IsTrue(). */
	void InvalidateResult(FWorldConditionQueryState& QueryState) const;

	/** Used internally, Offset of the data in the State storage. */
	uint16 StateDataOffset = 0;

	/** Used internally, Index of the condition in the definition and state storage. */
	uint8 ConditionIndex = 0;

	/** Used Internally, true if the condition has Object state. */
	uint8 bIsStateObject : 1;

	/** Set to true if the result of the IsTrue() can be cached. */
	uint8 bCanCacheResult : 1;

	/** Operator describing how the results of the condition is combined with other conditions. Not used directly, but to set up condition item in query state. */
	UPROPERTY()
	EWorldConditionOperator Operator = EWorldConditionOperator::And;

	/** Depth controlling the parenthesis of the expression. Not used directly, but to set up condition item in query state. */
	UPROPERTY()
	uint8 NextExpressionDepth = 0;

	friend struct FWorldConditionQueryDefinition;
	friend struct FWorldConditionQueryState;
	friend struct FWorldConditionContext;
};