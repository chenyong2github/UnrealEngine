// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Actions/PawnAction.h"
#include "PawnAction_Sequence.generated.h"

UCLASS()
class AIMODULE_API UDEPRECATED_UPawnAction_Sequence : public UDEPRECATED_UPawnAction
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	TArray<TObjectPtr<UDEPRECATED_UPawnAction>> ActionSequence_DEPRECATED;

	UPROPERTY(Category = PawnAction, EditAnywhere, BlueprintReadOnly)
	TEnumAsByte<EPawnActionFailHandling::Type> ChildFailureHandlingMode;

	UPROPERTY(Transient)
	TObjectPtr<UDEPRECATED_UPawnAction> RecentActionCopy_DEPRECATED;

	uint32 CurrentActionIndex;

	EPawnSubActionTriggeringPolicy::Type SubActionTriggeringPolicy;

	static UDEPRECATED_UPawnAction_Sequence* CreateAction(UWorld& World, TArray<UDEPRECATED_UPawnAction*>& ActionSequence, EPawnSubActionTriggeringPolicy::Type InSubActionTriggeringPolicy = EPawnSubActionTriggeringPolicy::CopyBeforeTriggering);

protected:
	virtual bool Start() override;
	virtual bool Resume() override;
	virtual void OnChildFinished(UDEPRECATED_UPawnAction& Action, EPawnActionResult::Type WithResult) override;

	bool PushNextActionCopy();
};
