// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BTTask_FindAndUseSmartObject.h"
#include "AI/AITask_UseSmartObject.h"
#include "AIController.h"
#include "GameplayTagAssetInterface.h"
#include "SmartObjectSubsystem.h"
#include "SmartObjectDefinition.h"
#include "VisualLogger/VisualLogger.h"


UBTTask_FindAndUseSmartObject::UBTTask_FindAndUseSmartObject(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	//TagMatchingPolicy = ESmartObjectGameplayTagMatching::Any;
	Radius = 500.f;
}

EBTNodeResult::Type UBTTask_FindAndUseSmartObject::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	EBTNodeResult::Type NodeResult = EBTNodeResult::Failed;

	UWorld* World = GetWorld();
	USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(World);
	AAIController* MyController = OwnerComp.GetAIOwner();
	if (Subsystem == nullptr || MyController == nullptr
		|| MyController->GetPawn() == nullptr)
	{
		return EBTNodeResult::Failed;
	}

	FBTUseSOTaskMemory* MyMemory = reinterpret_cast<FBTUseSOTaskMemory*>(NodeMemory);
	MyMemory->TaskInstance.Reset();

	AActor& Avatar = *MyController->GetPawn();
	const FVector UserLocation = Avatar.GetActorLocation();

	// Create filter
	FSmartObjectRequestFilter Filter(ActivityRequirements);
	Filter.BehaviorDefinitionClass = USmartObjectGameplayBehaviorDefinition::StaticClass();
	const IGameplayTagAssetInterface* TagsSource = Cast<const IGameplayTagAssetInterface>(&Avatar);
	if (TagsSource != nullptr)
	{
		TagsSource->GetOwnedGameplayTags(Filter.UserTags);
	}

	// Create request
	FSmartObjectRequest Request(FBox(UserLocation, UserLocation).ExpandBy(FVector(Radius), FVector(Radius)), Filter);
	TArray<FSmartObjectRequestResult> Results; 
	
	if (Subsystem->FindSmartObjects(Request, Results))
	{
		for (const FSmartObjectRequestResult& Result : Results)
		{
			FSmartObjectClaimHandle ClaimHandle = Subsystem->Claim(Result);
			if (ClaimHandle.IsValid())
			{
				UAITask_UseSmartObject* UseSOTask = NewBTAITask<UAITask_UseSmartObject>(OwnerComp);
				UseSOTask->SetClaimHandle(ClaimHandle);
				UseSOTask->ReadyForActivation();

				NodeResult = EBTNodeResult::InProgress;
				UE_VLOG_UELOG(MyController, LogSmartObject, Verbose, TEXT("%s claimed smart object: %s"), *GetNodeName(), *LexToString(ClaimHandle));
				break;
			}
		}

		UE_CVLOG_UELOG(NodeResult == EBTNodeResult::Failed, MyController, LogSmartObject, Warning, TEXT("%s failed to claim smart object"), *GetNodeName());
	}
	else
	{
		UE_VLOG_UELOG(MyController, LogSmartObject
			, Verbose, TEXT("%s failed to find smart objects for request: %s")
			, *GetNodeName(), *Avatar.GetName());
	}

	return NodeResult;
}

EBTNodeResult::Type UBTTask_FindAndUseSmartObject::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::Aborted;
}

void UBTTask_FindAndUseSmartObject::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{

}

FString UBTTask_FindAndUseSmartObject::GetStaticDescription() const
{
	FString Result;
	if (ActivityRequirements.IsEmpty() == false)
	{
		Result += FString::Printf(TEXT("Object requirements: %s")
			, *ActivityRequirements.GetDescription());
	}

	return Result.Len() > 0
		? Result
		: Super::GetStaticDescription();
}