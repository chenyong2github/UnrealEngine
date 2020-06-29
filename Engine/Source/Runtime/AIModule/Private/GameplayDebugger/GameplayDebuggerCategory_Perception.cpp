// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayDebugger/GameplayDebuggerCategory_Perception.h"

#if WITH_GAMEPLAY_DEBUGGER

#include "GameFramework/Pawn.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"

FGameplayDebuggerCategory_Perception::FGameplayDebuggerCategory_Perception()
{
}

TSharedRef<FGameplayDebuggerCategory> FGameplayDebuggerCategory_Perception::MakeInstance()
{
	return MakeShareable(new FGameplayDebuggerCategory_Perception());
}

void FGameplayDebuggerCategory_Perception::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
	UAIPerceptionComponent* PerceptionComponent = nullptr;
	APawn* MyPawn = Cast<APawn>(DebugActor);
	if (MyPawn)
	{
		AAIController* AIC = Cast<AAIController>(MyPawn->GetController());
		if (AIC)
		{
			PerceptionComponent = AIC->GetPerceptionComponent();
		}
	}

	if (PerceptionComponent == nullptr && DebugActor != nullptr)
	{
		PerceptionComponent = DebugActor->FindComponentByClass<UAIPerceptionComponent>();
	}

	if (PerceptionComponent)
	{
		PerceptionComponent->DescribeSelfToGameplayDebugger(this);
	}
}

#endif // WITH_GAMEPLAY_DEBUGGER
