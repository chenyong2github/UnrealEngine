// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BTDecorator_GameplayTagQuery.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "UObject/Object.h"

UBTDecorator_GameplayTagQuery::UBTDecorator_GameplayTagQuery(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	NodeName = "Gameplay Tag Query";

	INIT_DECORATOR_NODE_NOTIFY_FLAGS();

	// Accept only actors
	ActorForGameplayTagQuery.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_GameplayTagQuery, ActorForGameplayTagQuery), AActor::StaticClass());

	// Default to using Self Actor
	ActorForGameplayTagQuery.SelectedKeyName = FBlackboard::KeySelf;
}

bool UBTDecorator_GameplayTagQuery::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if (!BlackboardComp)
	{
		// Not calling super here since it returns true
		return false;
	}

	const IGameplayTagAssetInterface* GameplayTagAssetInterface = Cast<IGameplayTagAssetInterface>(BlackboardComp->GetValue<UBlackboardKeyType_Object>(ActorForGameplayTagQuery.GetSelectedKeyID()));
	if (!GameplayTagAssetInterface)
	{
		// Not calling super here since it returns true
		return false;
	}

	FGameplayTagContainer SelectedActorTags;
	GameplayTagAssetInterface->GetOwnedGameplayTags(SelectedActorTags);

	return GameplayTagQuery.Matches(SelectedActorTags);
}

void UBTDecorator_GameplayTagQuery::OnGameplayTagInQueryChanged(const FGameplayTag InTag, int32 NewCount, TWeakObjectPtr<UBehaviorTreeComponent> BehaviorTreeComponent, uint8* NodeMemory)
{
	if (!BehaviorTreeComponent.IsValid())
	{
		return;
	}

	ConditionalFlowAbort(*BehaviorTreeComponent, EBTDecoratorAbortRequest::ConditionResultChanged);
}

FString UBTDecorator_GameplayTagQuery::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s: %s"), *Super::GetStaticDescription(), *GameplayTagQuery.GetDescription());
}

void UBTDecorator_GameplayTagQuery::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	ensureMsgf(GameplayTagEventHandles.Num() == 0, TEXT("Dangling gameplay tag event handles for decorator %s"), *GetStaticDescription());
}

void UBTDecorator_GameplayTagQuery::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if (!BlackboardComp)
	{
		// Not calling super here since it does nothing
		return;
	}

	const AActor* SelectedActor = Cast<AActor>(BlackboardComp->GetValue<UBlackboardKeyType_Object>(ActorForGameplayTagQuery.GetSelectedKeyID()));
	if (!SelectedActor)
	{
		// Not calling super here since it does nothing
		return;
	}

	CachedAbilitySystemComponent = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(SelectedActor);
	if (CachedAbilitySystemComponent)
	{
		for (const FGameplayTag& CurrentTag : QueryTags)
		{
			FDelegateHandle GameplayTagEventCallbackDelegate = CachedAbilitySystemComponent->RegisterGameplayTagEvent(CurrentTag, EGameplayTagEventType::Type::AnyCountChange).AddUObject(this, &UBTDecorator_GameplayTagQuery::OnGameplayTagInQueryChanged, TWeakObjectPtr<UBehaviorTreeComponent>(&OwnerComp), NodeMemory);
			GameplayTagEventHandles.Emplace(CurrentTag, GameplayTagEventCallbackDelegate);
		}
	}
}

void UBTDecorator_GameplayTagQuery::OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	const UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if (!BlackboardComp)
	{
		// Not calling super here since it does nothing
		return;
	}

	if (CachedAbilitySystemComponent)
	{
		for (const TTuple<FGameplayTag, FDelegateHandle>& GameplayTagEvent : GameplayTagEventHandles)
		{
			CachedAbilitySystemComponent->RegisterGameplayTagEvent(GameplayTagEvent.Key, EGameplayTagEventType::Type::AnyCountChange).Remove(GameplayTagEvent.Value);
		}
	}

	GameplayTagEventHandles.Reset();
	CachedAbilitySystemComponent = nullptr;
}

#if WITH_EDITOR
void UBTDecorator_GameplayTagQuery::CacheGameplayTagsInsideQuery()
{
	QueryTags.Reset();
	GameplayTagQuery.GetGameplayTagArray(QueryTags);
}

void UBTDecorator_GameplayTagQuery::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property == NULL)
	{
		return;
	}

	CacheGameplayTagsInsideQuery();
}
#endif	// WITH_EDITOR

void UBTDecorator_GameplayTagQuery::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	UBlackboardData* BBAsset = GetBlackboardAsset();
	if (ensure(BBAsset))
	{
		ActorForGameplayTagQuery.ResolveSelectedKey(*BBAsset);
	}
}
