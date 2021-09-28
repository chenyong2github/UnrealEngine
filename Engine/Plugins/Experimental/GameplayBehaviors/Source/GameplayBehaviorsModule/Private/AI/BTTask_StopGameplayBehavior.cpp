// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BTTask_StopGameplayBehavior.h"
#include "GameplayBehavior.h"
#include "GameplayBehaviorManager.h"
#include "AIController.h"

//----------------------------------------------------------------------//
//  UBTTask_StopGameplayBehavior
//----------------------------------------------------------------------//
UBTTask_StopGameplayBehavior::UBTTask_StopGameplayBehavior(const FObjectInitializer& ObjectInitializer)
{

}

EBTNodeResult::Type UBTTask_StopGameplayBehavior::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UWorld* World = GetWorld();
	UGameplayBehaviorManager* GBMgr = UGameplayBehaviorManager::GetCurrent(World);
	AAIController* MyController = OwnerComp.GetAIOwner();
	if (GBMgr == nullptr || MyController == nullptr
		|| MyController->GetPawn() == nullptr)
	{
		return EBTNodeResult::Failed;
	}

	AActor& Avatar = *MyController->GetPawn();

	return GBMgr->StopBehavior(Avatar, BehaviorToStop)
		? EBTNodeResult::Succeeded
		: EBTNodeResult::Failed;
}

FString UBTTask_StopGameplayBehavior::GetStaticDescription() const
{
	FString Result;
	if (BehaviorToStop)
	{
		Result += FString::Printf(TEXT("Stop current gameplay behavior of type %s")
			, *BehaviorToStop->GetName());
	}
	else
	{
		Result += FString::Printf(TEXT("Stop any current gameplay behavior"));
	}

	return Result;
}

