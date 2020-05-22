// Copyright Epic Games, Inc. All Rights Reserved.

#include "AITestsCommon.h"
#include "4MLManager.h"
#include "4MLSession.h"
#include "Agents/4MLAgent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Engine/Engine.h"


#define LOCTEXT_NAMESPACE "AITestSuite_UE4MLTest"

PRAGMA_DISABLE_OPTIMIZATION

/**
 *	this fixture creates a game instance and can react to changes done to pawn.controller
 *	also, the session instance is created via the U4MLManager so all other notifies should get through as well, most 
 *	notably world-related ones
 */
struct F4MLTest_WithSession : public FAITestBase
{
	U4MLAgent* Agent = nullptr;
	AActor* Actor = nullptr;
	APawn* Pawn = nullptr;
	AAIController* Controller = nullptr;
	UGameInstance* GameInstance = nullptr;
	F4ML::FAgentID AgentID = F4ML::InvalidAgentID;
	U4MLSession* Session = nullptr;

	virtual UWorld& GetWorld() const override
	{
		return GameInstance && GameInstance->GetWorld()
			? *GameInstance->GetWorld()
			: FAITestBase::GetWorld();
	}

	virtual bool SetUp() override
	{
		Session = &U4MLManager::Get().GetSession();

		GameInstance = NewObject<UGameInstance>(GEngine);
		AITEST_NOT_NULL("GameInstance", GameInstance);
		GameInstance->InitializeStandalone();
		
		F4MLAgentConfig EmptyConfig;
		AgentID = Session->AddAgent(EmptyConfig);
		Agent = Session->GetAgent(AgentID);

		UWorld& World = GetWorld();
		Actor = World.SpawnActor<AActor>();
		Pawn = World.SpawnActor<APawn>();
		Controller = World.SpawnActor<AAIController>();

		return Agent && Actor && Pawn && Controller;
	}

	virtual void TearDown() override
	{
		if (Session)
		{
			U4MLManager::Get().CloseSession(*Session);
		}
		FAITestBase::TearDown();
	}
};

IMPLEMENT_INSTANT_TEST_WITH_FIXTURE(F4MLTest_WithSession, "System.AI.4ML.Agent", PossessingWhilePawnAvatar)
{
	Agent->SetAvatar(Pawn);
	AITEST_NULL("Setting unpossessed pawn as avatar results in no controller", Agent->GetController());
	Controller->Possess(Pawn);
	AITEST_EQUAL("After possessing the pawn the controller should be known to the agent", Agent->GetController(), Controller);
	return true;
}

IMPLEMENT_INSTANT_TEST_WITH_FIXTURE(F4MLTest_WithSession, "System.AI.4ML.Agent", SessionAssigningAvatar)
{
	F4MLAgentConfig NewConfig;
	NewConfig.AvatarClassName = APawn::StaticClass()->GetFName();
	Agent->Configure(NewConfig);
	AITEST_EQUAL("Calling configure should make the session instance pick a pawn avatar for the agent", Agent->GetPawn(), Pawn);
	return true;
}

IMPLEMENT_INSTANT_TEST_WITH_FIXTURE(F4MLTest_WithSession, "System.AI.4ML.Agent", ChangingAvatarClassOnTheFly)
{
	F4MLAgentConfig NewConfig;
	NewConfig.AvatarClassName = APawn::StaticClass()->GetFName();
	Agent->Configure(NewConfig);
	AITEST_EQUAL("Calling configure should make the session instance pick a pawn avatar for the agent", Agent->GetPawn(), Pawn);
	return true;
}

IMPLEMENT_INSTANT_TEST_WITH_FIXTURE(F4MLTest_WithSession, "System.AI.4ML.Agent", FindingNewPawnAfterDeath)
{
	F4MLAgentConfig NewConfig;
	NewConfig.AvatarClassName = APawn::StaticClass()->GetFName();
	Agent->Configure(NewConfig);

	AITEST_NOT_NULL("Session", Session);

	APawn* Pawn2 = Session->GetWorld()->SpawnActor<APawn>(FVector::ZeroVector, FRotator::ZeroRotator);
	Pawn->Destroy();
	// Avatar auto-selection should pick Pawn2 after Pawn is destroyed
	AITEST_EQUAL("Auto-picked avatar and the other pawn", Agent->GetAvatar(), Pawn2);

	return true;
}

IMPLEMENT_INSTANT_TEST_WITH_FIXTURE(F4MLTest_WithSession, "System.AI.4ML.Agent", UnPossesingWhileControllerAvatar)
{
	Agent->SetAvatar(Controller);
	Controller->Possess(Pawn);
	Controller->UnPossess();
	AITEST_NULL("After the controller unpossessing its pawn the agent should automatically update", Agent->GetPawn());
	return true;
}

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE
