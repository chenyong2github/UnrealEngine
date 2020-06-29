// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debug/GameplayDebuggerCategory_4ML.h"
#include "Engine/Engine.h"
#include "Misc/CoreDelegates.h"
#include "4MLManager.h"
#include "4MLSession.h"
#include "Agents/4MLAgent.h"
#include "Sensors/4MLSensor.h"
#include "Actuators/4MLActuator.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategoryReplicator.h"

//----------------------------------------------------------------------//
//  FGameplayDebuggerCategory_4ML
//----------------------------------------------------------------------//
FGameplayDebuggerCategory_4ML::FGameplayDebuggerCategory_4ML()
{
	CachedAgentID = F4ML::InvalidAgentID;
	CachedDebugActor = nullptr;
	bShowOnlyWithDebugActor = false;
	
	BindKeyPress(EKeys::RightBracket.GetFName(), FGameplayDebuggerInputModifier::Shift, this, &FGameplayDebuggerCategory_4ML::OnShowNextAgent, EGameplayDebuggerInputMode::Replicated);
	BindKeyPress(EKeys::LeftBracket.GetFName(), FGameplayDebuggerInputModifier::Shift, this, &FGameplayDebuggerCategory_4ML::OnRequestAvatarUpdate, EGameplayDebuggerInputMode::Replicated);
	BindKeyPress(EKeys::P.GetFName(), FGameplayDebuggerInputModifier::Shift, this, &FGameplayDebuggerCategory_4ML::OnSetAvatarAsDebugAgent, EGameplayDebuggerInputMode::Replicated);
}

void FGameplayDebuggerCategory_4ML::Init()
{
	U4MLManager::Get().GetOnCurrentSessionChanged().AddSP(this, &FGameplayDebuggerCategory_4ML::OnCurrentSessionChanged);
	if (U4MLManager::Get().HasSession())
	{
		OnCurrentSessionChanged();
	}
}

void FGameplayDebuggerCategory_4ML::ResetProps()
{
	CachedAgentID = F4ML::InvalidAgentID;
	CachedDebugActor = nullptr;
}

TSharedRef<FGameplayDebuggerCategory> FGameplayDebuggerCategory_4ML::MakeInstance()
{
	FGameplayDebuggerCategory_4ML* Instance = new FGameplayDebuggerCategory_4ML();
	TSharedRef<FGameplayDebuggerCategory> SharedInstanceRef = MakeShareable(Instance);

	if (U4MLManager::IsReady())
	{
		Instance->Init();
	}
	else
	{
		U4MLManager::OnPostInit.AddSP(Instance, &FGameplayDebuggerCategory_4ML::Init);
	}

	return SharedInstanceRef;
}

void FGameplayDebuggerCategory_4ML::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
	if (U4MLManager::Get().HasSession() == false)
	{
		AddTextLine(TEXT("{red}No session"));
		return;
	}

	U4MLSession& Session = U4MLManager::Get().GetSession();

	const U4MLAgent* Agent = nullptr;
	if (CachedDebugActor != DebugActor)
	{
		CachedDebugActor = DebugActor;
		if (DebugActor)
		{
			Agent = Session.FindAgentByAvatar(*DebugActor);
			
		}
		CachedAgentID = Agent ? Agent->GetAgentID() : F4ML::InvalidAgentID;
	}

	if (CachedAgentID != F4ML::InvalidAgentID && Agent == nullptr)
	{
		Agent = Session.GetAgent(CachedAgentID);
		ensureMsgf(Agent, TEXT("Null-agent retrieved while AgentID used was valid"));
	}

	Session.DescribeSelfToGameplayDebugger(*this);
	AddTextLine(FString::Printf(TEXT("{DimGrey}---------------------")));
	if (Agent)
	{
		Agent->DescribeSelfToGameplayDebugger(*this);
	}
	else if (CachedAgentID != F4ML::InvalidAgentID)
	{
		AddTextLine(FString::Printf(TEXT("{orange}Agent %d has no avatar"), CachedAgentID));
	}
	else
	{
		AddTextLine(FString::Printf(TEXT("{orange}No agent selected"), CachedAgentID));
	}
}

void FGameplayDebuggerCategory_4ML::DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext)
{
	CanvasContext.Printf(TEXT("\n[{yellow}%s{white}] Next agent"), *GetInputHandlerDescription(0));
	CanvasContext.Printf(TEXT("[{yellow}%s{white}] Request avatar"), *GetInputHandlerDescription(1));
	CanvasContext.Printf(TEXT("[{yellow}%s{white}] Debug current avatar"), *GetInputHandlerDescription(2));

	FGameplayDebuggerCategory::DrawData(OwnerPC, CanvasContext);
}

void FGameplayDebuggerCategory_4ML::OnShowNextAgent()
{
	CachedDebugActor = nullptr;
	// should get called on Authority
	if (U4MLManager::Get().HasSession())
	{
		CachedAgentID = U4MLManager::Get().GetSession().GetNextAgentID(CachedAgentID);
		if (CachedAgentID != F4ML::InvalidAgentID)
		{
			const U4MLAgent* Agent = U4MLManager::Get().GetSession().GetAgent(CachedAgentID);
			if (ensure(Agent) && Agent->GetAvatar())
			{
				AGameplayDebuggerCategoryReplicator* Replicator = GetReplicator();
				if (ensure(Replicator))
				{
					Replicator->SetDebugActor(Agent->GetAvatar());
				}
			}
		}
	}
}

void FGameplayDebuggerCategory_4ML::OnRequestAvatarUpdate()
{
	if (U4MLManager::Get().HasSession() && CachedAgentID != F4ML::InvalidAgentID)
	{
		U4MLManager::Get().GetSession().RequestAvatarForAgent(CachedAgentID);
	}
}

void FGameplayDebuggerCategory_4ML::OnSetAvatarAsDebugAgent()
{
	if (CachedAgentID != F4ML::InvalidAgentID && U4MLManager::Get().HasSession())
	{
		const U4MLAgent* Agent = U4MLManager::Get().GetSession().GetAgent(CachedAgentID);
		if (ensure(Agent) && Agent->GetAvatar())
		{
			AGameplayDebuggerCategoryReplicator* Replicator = GetReplicator();
			if (ensure(Replicator))
			{
				Replicator->SetDebugActor(Agent->GetAvatar());
			}
		}
	}
}

void FGameplayDebuggerCategory_4ML::OnCurrentSessionChanged()
{
	if (U4MLManager::Get().HasSession())
	{
		U4MLManager::Get().GetSession().GetOnAgentAvatarChanged().AddSP(this, &FGameplayDebuggerCategory_4ML::OnAgentAvatarChanged);
		U4MLManager::Get().GetSession().GetOnBeginAgentRemove().AddSP(this, &FGameplayDebuggerCategory_4ML::OnBeginAgentRemove);
	}
	else
	{
		ResetProps();
	}
}

void FGameplayDebuggerCategory_4ML::OnAgentAvatarChanged(U4MLAgent& Agent, AActor* OldAvatar)
{
	if (Agent.GetAgentID() == CachedAgentID)
	{
		// pass
	}
}

void FGameplayDebuggerCategory_4ML::OnBeginAgentRemove(U4MLAgent& Agent)
{
	if (Agent.GetAgentID() == CachedAgentID)
	{
		ResetProps();
	}
}

#endif // WITH_GAMEPLAY_DEBUGGER

