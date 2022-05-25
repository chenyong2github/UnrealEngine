// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/AsyncPhysicsInputComponent.h"

#include "Chaos/SimCallbackObject.h"
#include "ChaosSolversModule.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Experimental/Chaos/Public/RewindData.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Net/UnrealNetwork.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "RewindData.h"
#include "UObject/ObjectKey.h"
#include "UObject/UObjectIterator.h"

namespace InputCmdCVars
{
	static int32 ForceFault = 0;
	static FAutoConsoleVariableRef CVarForceFault(TEXT("p.net.ForceFault"), ForceFault, TEXT("Forces server side input fault"));

	static int32 ForceInputDrop = 0;
	static FAutoConsoleVariableRef CVarForceInputDrop(TEXT("p.net.ForceInputDrop"), ForceInputDrop, TEXT("Forces client to drop inputs. Useful for simulating desync"));

	static int32 MaxBufferedCmds = 16;
	static FAutoConsoleVariableRef CVarMaxBufferedCmds(TEXT("p.net.MaxBufferedCmds"), MaxBufferedCmds, TEXT("MaxNumber of buffered server side commands"));

	static int32 TimeDilationEnabled = 1;
	static FAutoConsoleVariableRef CVarTimeDilationEnabled(TEXT("p.net.TimeDilationEnabled"), TimeDilationEnabled, TEXT("Enable clientside TimeDilation"));

	static float MaxTargetNumBufferedCmds = 5.0;
	static FAutoConsoleVariableRef CVarMaxTargetNumBufferedCmds(TEXT("p.net.MaxTargetNumBufferedCmds"), MaxTargetNumBufferedCmds, TEXT("Maximum number of buffered inputs the server will target per client."));

	static float MaxTimeDilationMag = 0.01f;
	static FAutoConsoleVariableRef CVarMaxTimeDilationMag(TEXT("p.net.MaxTimeDilationMag"), MaxTimeDilationMag, TEXT("Maximum time dilation that client will use to slow down / catch up with server"));

	static float TimeDilationAlpha = 0.1f;
	static FAutoConsoleVariableRef CVarTimeDilationAlpha(TEXT("p.net.TimeDilationAlpha"), TimeDilationAlpha, TEXT("Lerp strength for sliding client time dilation"));

	static float TargetNumBufferedCmdsDeltaOnFault = 1.0f;
	static FAutoConsoleVariableRef CVarTargetNumBufferedCmdsDeltaOnFault(TEXT("p.net.TargetNumBufferedCmdsDeltaOnFault"), TargetNumBufferedCmdsDeltaOnFault, TEXT("How much to increase TargetNumBufferedCmds when an input fault occurs"));

	static float TargetNumBufferedCmds = 1.9f;
	static FAutoConsoleVariableRef CVarTargetNumBufferedCmds(TEXT("p.net.TargetNumBufferedCmds"), TargetNumBufferedCmds, TEXT("How much to increase TargetNumBufferedCmds when an input fault occurs"));

	static float TargetNumBufferedCmdsAlpha = 0.005f;
	static FAutoConsoleVariableRef CVarTargetNumBufferedCmdsAlpha(TEXT("p.net.TargetNumBufferedCmdsAlpha"), TargetNumBufferedCmdsAlpha, TEXT("Lerp strength for TargetNumBufferedCmds"));

	static int32 LerpTargetNumBufferedCmdsAggresively = 0;
	static FAutoConsoleVariableRef CVarLerpTargetNumBufferedCmdsAggresively(TEXT("p.net.LerpTargetNumBufferedCmdsAggresively"), LerpTargetNumBufferedCmdsAggresively, TEXT("Aggresively lerp towards TargetNumBufferedCmds. Reduces server side buffering but can cause more artifacts."));
}

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDispatchPhysicsTick, int32 PhysicsStep, int32 NumSteps);

struct FAsyncPhysicsInputRewindCallback : public Chaos::IRewindCallback
{
	FAsyncPhysicsInputRewindCallback(UWorld* InWorld) : World(InWorld) { }
	UWorld* World = nullptr;

	FOnDispatchPhysicsTick DispatchPhysicsTick;

	int32 CachedServerFrame = 0;

	virtual void InjectInputs_External(int32 PhysicsStep, int32 NumSteps) override
	{
		DispatchPhysicsTick.Broadcast(PhysicsStep, NumSteps);
	}

	// Updates the TMap on PhysScene that stores (non interpolated) physics data for replication.
	// 
	// Needs to be called from PT context to access fixed tick handle
	// but also needs to be able to access GT data (actor iterator, actor state)
	int32 LastUpdatedStep = 0;
	void UpdateReplicationMap_Internal(int32 PhysicsStep)
	{
		if (LastUpdatedStep == PhysicsStep)
		{
			return;
		}

		LastUpdatedStep = PhysicsStep;

		// Go through all "managed" primitive components and update Tmap to have latest physics state
		// This is a temp hack and we will eventually replace this with something internal to the physics system that can be updated as things move
		if (FPhysScene_Chaos* Scene = static_cast<FPhysScene_Chaos*>(World->GetPhysicsScene()))
		{
			Scene->ReplicationCache.ServerFrame = PhysicsStep;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (Actor && Actor->GetIsReplicated() && Actor->IsReplicatingMovement())
				{
					UPrimitiveComponent* RootComponent = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
					if (RootComponent && RootComponent->IsSimulatingPhysics())
					{
						if (FBodyInstanceAsyncPhysicsTickHandle Handle = RootComponent->GetBodyInstanceAsyncPhysicsTickHandle())
						{
							FObjectKey Key(RootComponent);
							FRigidBodyState& LatestState = Scene->ReplicationCache.Map.FindOrAdd(Key);

							// This might be wrong... see FBodyInstance::GetRigidBodyState (converts to unreal units?) and FRepMovement::FillFrom
							LatestState.Position = Handle->X();
							LatestState.Quaternion = Handle->R();
							LatestState.LinVel = Handle->V();
							LatestState.AngVel = Handle->W();
							LatestState.Flags = Handle->ObjectState() == Chaos::EObjectStateType::Sleeping ? ERigidBodyFlags::Sleeping : 0;
						}
					}
				}
			}
		}
	}
};

UAsyncPhysicsInputComponent::UAsyncPhysicsInputComponent()
{
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);
}

void UAsyncPhysicsInputComponent::SetDataClass(TSubclassOf<UAsyncPhysicsData> InDataClass)
{
	ensureMsgf(DataClass == nullptr, TEXT("You can only set the data class once"));
	DataClass = InDataClass;

	DataToWrite = DuplicateObject((UAsyncPhysicsData*)DataClass->GetDefaultObject(), nullptr);
	
	//now that we have a class we're ready to async tick
	SetAsyncPhysicsTickEnabled(true);
	UWorld* World = GetWorld();
	if (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game)
	{
		FPhysScene* PhysScene = World->GetPhysicsScene();
		if (ensureAlways(PhysScene))
		{
			Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();
			if (Solver->GetRewindCallback() == nullptr)
			{
				// -------------------------------------------------------------------------------------
				// Hack: enable Rewind capture here. This needs to be moved somewhere else
				// -------------------------------------------------------------------------------------

				int32 NumFrames = 64;
				const IConsoleVariable* NumFramesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.RewindCaptureNumFrames"));
				if (ensure(NumFramesCVar))
				{
					// 1 frame is required to enable rewind capture. 
					NumFrames = FMath::Max<int32>(1, NumFramesCVar->GetInt());
				}
				Solver->EnableRewindCapture(NumFrames, false, MakeUnique<FAsyncPhysicsInputRewindCallback>(World));
			}

			FAsyncPhysicsInputRewindCallback* Callback = static_cast<FAsyncPhysicsInputRewindCallback*>(Solver->GetRewindCallback());
			Callback->DispatchPhysicsTick.AddUObject(this, &UAsyncPhysicsInputComponent::OnDispatchPhysicsTick);
		}
	}
}

void UAsyncPhysicsInputComponent::AsyncPhysicsTickComponent(float DeltaTime, float SimTime)
{
	Super::AsyncPhysicsTickComponent(DeltaTime, SimTime);
	ensureMsgf(DataClass != nullptr, TEXT("You must call SetDataClass after creating the component"));

	UWorld* World = GetWorld();
	FPhysScene* PhysScene = World->GetPhysicsScene();
	Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();

	// Pull server frame out in this unsafe way. Better if we can get piped into AsyncPhysicsTickComponent
	FAsyncPhysicsInputRewindCallback* Callback = static_cast<FAsyncPhysicsInputRewindCallback*>(Solver->GetRewindCallback());
	const int32 ServerFrame = Callback->CachedServerFrame;

	if(World->IsNetMode(ENetMode::NM_DedicatedServer) || World->IsNetMode(ENetMode::NM_ListenServer))
	{
		//TODO: move this somewhere else - here because we need to run this when GT and PT are both on same core. Function guards against multiple calls per ServerFrame so calling from each instance is ok
		Callback->UpdateReplicationMap_Internal(ServerFrame);
	}

	DataToConsume = nullptr; //set current input to none in case we don't find it

	for (int32 Idx = BufferedData.Num() - 1; Idx >= 0; --Idx)
	{
		UAsyncPhysicsData* Data = BufferedData[Idx];
		if (Data->ServerFrame == ServerFrame)
		{
			DataToConsume = Data;
		}

		bool bFreeData = Data->ServerFrame < ServerFrame;	//for most cases once we're passed this frame we can free
		if (APlayerController* PC = GetPlayerController())
		{
			if (PC->IsLocalController() && !InputCmdCVars::ForceInputDrop)
			{
				//TODO: fix rpc version
				//ServerRPCBufferInput(Data);

				//If we are the local player then we need to keep this data around to send redundant RPCs to deal with potential packet loss
				bFreeData = --Data->ReplicationRedundancy <= 0;
			}
		}

		if (bFreeData)
		{
			BufferedData.RemoveAtSwap(Idx);
		}
	}
}

void UAsyncPhysicsInputComponent::ServerRPCBufferInput_Implementation(UAsyncPhysicsData* NewData)
{
	for (UAsyncPhysicsData* Data : BufferedData)
	{
		if (Data->ServerFrame == NewData->ServerFrame)
		{
			//already buffered this is just a redundant send to deal with potential packet loss
			//question: what if client's offset changes so we get a new instruction for what we think is an existing frame? The case where offset changes is input fault (hopefully rare) so maybe we just accept it
			return;
		}
	}

	//TODO: fix rpc version
	//BufferedData.Add(NewData);
}

void UAsyncPhysicsInputComponent::OnDispatchPhysicsTick(int32 PhysicsStep, int32 NumSteps)
{
	ensureMsgf(DataClass != nullptr, TEXT("You must call SetDataClass after creating the component"));
	if (!ensure(DataToWrite != nullptr))
	{
		return;
	}

	// It would be better if we only registered while we had a local controller,
	// but this is simpler to implement

	if (APlayerController* PC = GetPlayerController())
	{
		if (PC->IsLocalController())
		{
			const int32 LocalToServerOffset = PC->GetLocalToServerAsyncPhysicsTickOffset();
			const int32 ServerFrame = PhysicsStep + LocalToServerOffset;
			UAsyncPhysicsData* DataToSend = DataToWrite;
			DataToWrite = DuplicateObject((UAsyncPhysicsData*)DataClass->GetDefaultObject(), nullptr);
			for (int32 Step = 0; Step < NumSteps; ++Step)
			{
				if(Step > 0)
				{
					//Make sure each sub-step gets its own unique data so we don't have to worry about ref count
					//TODO: should probably given user opportunity to modify this per sub-step. For example a jump instruction should only happen on first sub-step
					DataToSend = DuplicateObject(DataToSend, nullptr);
				}
				DataToSend->ServerFrame = ServerFrame + 1 + Step;	//why do we need + 1?
				BufferedData.Add(DataToSend);
			}
		}
	}
}

APlayerController* UAsyncPhysicsInputComponent::GetPlayerController()
{
	if (APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		return PC;
	}

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->GetController<APlayerController>();
	}

	return nullptr;
}


UAsyncPhysicsData* UAsyncPhysicsInputComponent::GetDataToWrite() const
{
	//TODO: ensure not inside async physics tick
	return DataToWrite;
}

const UAsyncPhysicsData* UAsyncPhysicsInputComponent::GetDataToConsume() const
{
	//TODO: ensure inside async physics tick
	ensureMsgf(DataClass != nullptr, TEXT("You must call SetDataClass after creating the component"));
	if (DataToConsume)
	{
		return DataToConsume;
	}
	
	return (UAsyncPhysicsData*)DataClass->GetDefaultObject();
}
