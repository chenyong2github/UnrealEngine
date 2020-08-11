// Copyright Epic Games, Inc. All Rights Reserved.

#include "Agents/4MLAgentElement.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Agents/4MLAgent.h"


U4MLAgentElement::U4MLAgentElement(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SpaceDef(MakeShareable(new F4ML::FSpace_Dummy()))
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		Description = FString::Printf(TEXT("%s, detailed description pending"), *GetClass()->GetName());
	}

	Nickname = GetName();
}

void U4MLAgentElement::PostInitProperties()
{
	// U4MLAgent instance is the only valid outer type
	check(HasAnyFlags(RF_ClassDefaultObject) || Cast<U4MLAgent>(GetOuter()));

	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		UpdateSpaceDef();
	}
}

const U4MLAgent& U4MLAgentElement::GetAgent() const
{
	// U4MLAgent instance is the only valid outer type
	return *CastChecked<U4MLAgent>(GetOuter());
}

AActor* U4MLAgentElement::GetAvatar() const
{
	return GetAgent().GetAvatar();
}

APawn* U4MLAgentElement::GetPawnAvatar() const
{
	AActor* Avatar = GetAvatar();
	APawn* Pawn = Cast<APawn>(Avatar);
	AController* Controller = Cast<AController>(Avatar);
	return Pawn ? Pawn : (Controller ? Controller->GetPawn() : nullptr);
}

AController* U4MLAgentElement::GetControllerAvatar() const
{
	AActor* Avatar = GetAvatar();
	APawn* Pawn = Cast<APawn>(Avatar);
	AController* Controller = Cast<AController>(Avatar);
	return Controller ? Controller : (Pawn ? Pawn->GetController() : nullptr);
}

bool U4MLAgentElement::GetPawnAndControllerAvatar(APawn*& OutPawn, AController*& OutController) const
{
	AActor* Avatar = GetAvatar();
	APawn* Pawn = Cast<APawn>(Avatar);
	AController* Controller = Cast<AController>(Avatar);
	if (Pawn)
	{
		Controller = Pawn->GetController();
	}
	else if (Controller)
	{
		Pawn = Controller->GetPawn();
	}
	OutPawn = Pawn;
	OutController = Controller;
	
	return Pawn || Controller;
}

void U4MLAgentElement::Configure(const TMap<FName, FString>& Params)
{
	const FName NAME_Nickname = TEXT("nickname");

	for (auto KeyValue : Params)
	{
		if (KeyValue.Key == NAME_Nickname)
		{
			Nickname = KeyValue.Value;
		}
	}
}

void U4MLAgentElement::UpdateSpaceDef()
{
	SpaceDef = ConstructSpaceDef().ToSharedRef();
}

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategory.h"

void U4MLAgentElement::DescribeSelfToGameplayDebugger(FGameplayDebuggerCategory& DebuggerCategory) const
{
	DebuggerCategory.AddTextLine(FString::Printf(TEXT("\t{yellow}[%d] %s {white}%s"), ElementID
		, *GetName(), *DebugRuntimeString));
}
#endif // WITH_GAMEPLAY_DEBUGGER
