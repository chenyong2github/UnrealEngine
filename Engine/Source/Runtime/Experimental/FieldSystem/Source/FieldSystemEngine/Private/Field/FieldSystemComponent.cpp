// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Field/FieldSystemComponent.h"

#include "Async/ParallelFor.h"
#include "ChaosSolversModule.h"
#include "Chaos/RigidParticles.h"
#include "Field/FieldSystemCoreAlgo.h"
#include "Field/FieldSystemSceneProxy.h"
#include "Field/FieldSystemNodes.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreMiscDefines.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsProxy/FieldSystemPhysicsProxy.h"
#include "PBDRigidsSolver.h"

DEFINE_LOG_CATEGORY_STATIC(FSC_Log, NoLogging, All);

UFieldSystemComponent::UFieldSystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FieldSystem(nullptr)
#if INCLUDE_CHAOS
	, PhysicsProxy(nullptr)
	, ChaosModule(nullptr)
#endif
	, bHasPhysicsState(false)
{
	UE_LOG(FSC_Log, Log, TEXT("FieldSystemComponent[%p]::UFieldSystemComponent()"),this);

	SetGenerateOverlapEvents(false);
}


FPrimitiveSceneProxy* UFieldSystemComponent::CreateSceneProxy()
{
	UE_LOG(FSC_Log, Log, TEXT("FieldSystemComponent[%p]::CreateSceneProxy()"), this);

	return new FFieldSystemSceneProxy(this);
}


void UFieldSystemComponent::OnCreatePhysicsState()
{
	UActorComponent::OnCreatePhysicsState();
	
	const bool bValidWorld = GetWorld() && GetWorld()->IsGameWorld();
	if(bValidWorld)
	{
#if INCLUDE_CHAOS
		// Check we can get a suitable dispatcher
		ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers");
		check(ChaosModule);

		PhysicsProxy = new FFieldSystemPhysicsProxy(this);
		TSharedPtr<FPhysScene_Chaos> Scene = GetOwner()->GetWorld()->PhysicsScene_Chaos;
		Scene->AddObject(this, PhysicsProxy);
#endif

		bHasPhysicsState = true;
	}
}

void UFieldSystemComponent::OnDestroyPhysicsState()
{
	UActorComponent::OnDestroyPhysicsState();
#if INCLUDE_CHAOS
	if (!PhysicsProxy)
	{
		check(!bHasPhysicsState);
		return;
	}

	TSharedPtr<FPhysScene_Chaos> Scene = GetOwner()->GetWorld()->PhysicsScene_Chaos;
	Scene->RemoveObject(PhysicsProxy);

	ChaosModule = nullptr;
	PhysicsProxy = nullptr;
#endif

	bHasPhysicsState = false;
	
}

bool UFieldSystemComponent::ShouldCreatePhysicsState() const
{
	return true;
}

bool UFieldSystemComponent::HasValidPhysicsState() const
{
	return bHasPhysicsState;
}

void UFieldSystemComponent::DispatchCommand(const FFieldSystemCommand& InCommand)
{
#if INCLUDE_CHAOS
	if (HasValidPhysicsState())
	{
		checkSlow(ChaosModule); // Should already be checked from OnCreatePhysicsState
		Chaos::IDispatcher* PhysicsDispatcher = ChaosModule->GetDispatcher();
		checkSlow(PhysicsDispatcher); // Should always have one of these

		// Assemble a list of compatible solvers
		TArray<Chaos::FPhysicsSolver*> SolverList;
		if(SupportedSolvers.Num() > 0)
		{
			for(TSoftObjectPtr<AChaosSolverActor>& SolverActorPtr : SupportedSolvers)
			{
				if(AChaosSolverActor* CurrActor = SolverActorPtr.Get())
				{
					SolverList.Add(CurrActor->GetSolver());
				}
			}
		}

		PhysicsDispatcher->EnqueueCommandImmediate([PhysicsProxy = this->PhysicsProxy, NewCommand = InCommand, ChaosModule = this->ChaosModule, SolverList]()
		{
			const int32 NumFilterSolvers = SolverList.Num();
			const TArray<Chaos::FPhysicsSolver*>& Solvers = ChaosModule->GetSolvers();

			for(Chaos::FPhysicsSolver* Solver : Solvers)
			{
				const bool bSolverValid = NumFilterSolvers == 0 || SolverList.Contains(Solver);

				if(Solver->Enabled() && Solver->HasActiveParticles() && bSolverValid)
				{
					PhysicsProxy->BufferCommand(Solver, NewCommand);
				}
			}
		});
	}
#endif
}

void UFieldSystemComponent::ApplyStayDynamicField(bool Enabled, FVector Position, float Radius)
{
	if (Enabled && HasValidPhysicsState())
	{
		DispatchCommand({"DynamicState",new FRadialIntMask(Radius, Position, (int32)Chaos::EObjectStateType::Dynamic, 
			(int32)Chaos::EObjectStateType::Kinematic, ESetMaskConditionType::Field_Set_IFF_NOT_Interior)});
	}
}

void UFieldSystemComponent::ApplyLinearForce(bool Enabled, FVector Direction, float Magnitude)
{
	if (Enabled && HasValidPhysicsState())
	{
		DispatchCommand({ "LinearForce", new FUniformVector(Magnitude, Direction) });
	}
}

void UFieldSystemComponent::ApplyRadialForce(bool Enabled, FVector Position, float Magnitude)
{
	if (Enabled && HasValidPhysicsState())
	{
		DispatchCommand({ "LinearForce", new FRadialVector(Magnitude, Position) });
	}
}

void UFieldSystemComponent::ApplyRadialVectorFalloffForce(bool Enabled, FVector Position, float Radius, float Magnitude)
{
	if (Enabled && HasValidPhysicsState())
	{
		FRadialFalloff * FalloffField = new FRadialFalloff(Magnitude,0.f, 1.f, 0.f, Radius, Position);
		FRadialVector* VectorField = new FRadialVector(Magnitude, Position);
		DispatchCommand({"LinearForce", new FSumVector(1.0, FalloffField, VectorField, nullptr, Field_Multiply)});
	}
}

void UFieldSystemComponent::ApplyUniformVectorFalloffForce(bool Enabled, FVector Position, FVector Direction, float Radius, float Magnitude)
{
	if (Enabled && HasValidPhysicsState())
	{
		FRadialFalloff * FalloffField = new FRadialFalloff(Magnitude, 0.f, 1.f, 0.f, Radius, Position);
		FUniformVector* VectorField = new FUniformVector(Magnitude, Direction);
		DispatchCommand({ "LinearForce", new FSumVector(1.0, FalloffField, VectorField, nullptr, Field_Multiply) });
	}
}

void UFieldSystemComponent::ApplyStrainField(bool Enabled, FVector Position, float Radius, float Magnitude, int32 Iterations)
{
	if (Enabled && HasValidPhysicsState())
	{
		FFieldSystemCommand Command = { "ExternalClusterStrain", new FRadialFalloff(Magnitude,0.f, 1.f, 0.f, Radius, Position) };
		DispatchCommand(Command);
	}
}

UFUNCTION(BlueprintCallable, Category = "Field")
void UFieldSystemComponent::ApplyPhysicsField(bool Enabled, EFieldPhysicsType Target, UFieldSystemMetaData* MetaData, UFieldNodeBase* Field)
{
	if (Enabled && Field && HasValidPhysicsState())
	{
		TArray<const UFieldNodeBase*> Nodes;
		FFieldSystemCommand Command = { GetFieldPhysicsName(Target), Field->NewEvaluationGraph(Nodes) };
		if (ensureMsgf(Command.RootNode, 
			TEXT("Failed to generate physics field command for target attribute.")))
		{
			if (MetaData)
			{
				switch (MetaData->Type())
				{
				case FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution:
					Command.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution).Reset(new FFieldSystemMetaDataProcessingResolution(static_cast<UFieldSystemMetaDataProcessingResolution*>(MetaData)->ResolutionType));
					break;
				case FFieldSystemMetaData::EMetaType::ECommandData_Iteration:
					Command.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_Iteration).Reset(new FFieldSystemMetaDataIteration(static_cast<UFieldSystemMetaDataIteration*>(MetaData)->Iterations));
					break;
				}
			}
			ensure(!Command.TargetAttribute.IsEqual("None"));
			DispatchCommand(Command);
		}
	}
}

void UFieldSystemComponent::ResetFieldSystem()
{
	if (FieldSystem)
	{
		BlueprintBufferedCommands.Reset();
	}
}

void UFieldSystemComponent::AddFieldCommand(bool Enabled, EFieldPhysicsType Target, UFieldSystemMetaData* MetaData, UFieldNodeBase* Field)
{
	if (Field && FieldSystem)
	{
		TArray<const UFieldNodeBase*> Nodes;
		FFieldSystemCommand Command = { GetFieldPhysicsName(Target), Field->NewEvaluationGraph(Nodes) };
		if (ensureMsgf(Command.RootNode,
			TEXT("Failed to generate physics field command for target attribute.")))
		{
			if (MetaData)
			{
				switch (MetaData->Type())
				{
				case FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution:
					Command.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution).Reset(new FFieldSystemMetaDataProcessingResolution(static_cast<UFieldSystemMetaDataProcessingResolution*>(MetaData)->ResolutionType));
					break;
				case FFieldSystemMetaData::EMetaType::ECommandData_Iteration:
					Command.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_Iteration).Reset(new FFieldSystemMetaDataIteration(static_cast<UFieldSystemMetaDataIteration*>(MetaData)->Iterations));
					break;
				}
			}
			ensure(!Command.TargetAttribute.IsEqual("None"));
			BlueprintBufferedCommands.Add(Command);
		}
	}
}




