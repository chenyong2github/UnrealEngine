// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "InputCoreTypes.h"
#include "BehaviorTree/BTTaskNode.h"
#include "AI/AITask_UseSmartObject.h"
#include "BTTask_FindAndUseSmartObject.generated.h"


class AITask_UseSmartObject;

struct FBTUseSOTaskMemory
{
	TWeakObjectPtr<UAITask_UseSmartObject> TaskInstance;
};

/**
*
*/
UCLASS()
class SMARTOBJECTSMODULE_API UBTTask_FindAndUseSmartObject : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_FindAndUseSmartObject(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTUseSOTaskMemory); }

	//virtual void DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const override;
	virtual FString GetStaticDescription() const override;

	//#if WITH_EDITOR
	//	virtual FName GetNodeIconName() const override;
	//	virtual void OnNodeCreated() override;
	//#endif // WITH_EDITOR

protected:
	/** Additional tag query to filter available smart objects. We'll query for smart
	 *	objects that support activities tagged in a way matching the filter.
	 *	Note that regular tag-base filtering is going to take place as well */
	UPROPERTY(EditAnywhere, Category = SmartObjects)
	FGameplayTagQuery ActivityRequirements;

	UPROPERTY(EditAnywhere, Category = SmartObjects)
	float Radius;

	//EBTNodeResult::Type PerformMoveTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);
	//
	///** prepares move task for activation */
	//virtual UAITask_MoveTo* PrepareMoveTask(UBehaviorTreeComponent& OwnerComp, UAITask_MoveTo* ExistingTask, FAIMoveRequest& MoveRequest);
};
