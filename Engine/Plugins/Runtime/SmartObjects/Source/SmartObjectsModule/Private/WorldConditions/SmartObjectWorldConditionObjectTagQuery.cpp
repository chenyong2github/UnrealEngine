// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldConditions/SmartObjectWorldConditionObjectTagQuery.h"
#include "GameplayTagContainer.h"
#include "WorldConditionSchema.h"
#include "WorldConditionContext.h"
#include "SmartObjectSubsystem.h"
#include "WorldConditions/SmartObjectWorldConditionSchema.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SmartObjectWorldConditionObjectTagQuery)

#define LOCTEXT_NAMESPACE "SmartObjects"

#if WITH_EDITOR
FText FSmartObjectWorldConditionObjectTagQuery::GetDescription() const
{
	return LOCTEXT("ObjectTagQueryDesc", "Match Runtime Object Tags");
}
#endif // WITH_EDITOR

bool FSmartObjectWorldConditionObjectTagQuery::Initialize(const UWorldConditionSchema& Schema)
{
	const USmartObjectWorldConditionSchema* SmartObjectSchema = Cast<USmartObjectWorldConditionSchema>(&Schema);
	if (!SmartObjectSchema)
	{
		UE_LOG(LogSmartObject, Error, TEXT("SmartObjectWorldConditionObjectTagQuery: Expecting schema based on SmartObjectWorldConditionSchema."));
		return false;
	}

	// @todo: update to event based once SO instance emits events.
	bCanCacheResult	= false;
	ObjectHandleRef = SmartObjectSchema->GetSmartObjectHandleRef();
	
	return false;
}

EWorldConditionResult FSmartObjectWorldConditionObjectTagQuery::IsTrue(const FWorldConditionContext& Context) const
{
	const USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(Context.GetWorld());
	check(SmartObjectSubsystem);

	if (const FSmartObjectHandle* ObjectHandle = Context.GetContextDataPtr<FSmartObjectHandle>(ObjectHandleRef))
	{
		const FGameplayTagContainer& ObjectTags = SmartObjectSubsystem->GetInstanceTags(*ObjectHandle);
		return TagQuery.Matches(ObjectTags) ? EWorldConditionResult::IsTrue : EWorldConditionResult::IsFalse;
	}

	return EWorldConditionResult::IsFalse;
}

#undef LOCTEXT_NAMESPACE
