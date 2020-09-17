// Copyright Epic Games, Inc. All Rights Reserved.

#include "Field/FieldSystemComponent.h"

#include "Async/ParallelFor.h"
#include "ChaosSolversModule.h"
#include "Field/FieldSystemCoreAlgo.h"
#include "Field/FieldSystemSceneProxy.h"
#include "Field/FieldSystemNodes.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreMiscDefines.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsProxy/PerSolverFieldSystem.h"
#include "PBDRigidsSolver.h"

DEFINE_LOG_CATEGORY_STATIC(FSC_Log, NoLogging, All);

UFieldSystemComponent::UFieldSystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FieldSystem(nullptr)
	, ChaosModule(nullptr)
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

TSet<FPhysScene_Chaos*> UFieldSystemComponent::GetPhysicsScenes() const
{
	TSet<FPhysScene_Chaos*> Scenes;
	if (SupportedSolvers.Num())
	{
		for (const TSoftObjectPtr<AChaosSolverActor>& Actor : SupportedSolvers)
		{
			if (!Actor.IsValid())
				continue;
			Scenes.Add(Actor->GetPhysicsScene().Get());
		}
	}
	else
	{
#if INCLUDE_CHAOS
		if (ensure(GetOwner()) && ensure(GetOwner()->GetWorld()))
		{
			Scenes.Add(GetOwner()->GetWorld()->GetPhysicsScene());
		}
		else
		{
			check(GWorld);
			Scenes.Add(GWorld->GetPhysicsScene());
		}
#endif
	}
	return Scenes;
}

void UFieldSystemComponent::OnCreatePhysicsState()
{
	UActorComponent::OnCreatePhysicsState();
	
	const bool bValidWorld = GetWorld() && GetWorld()->IsGameWorld();
	if(bValidWorld)
	{
		// Check we can get a suitable dispatcher
		ChaosModule = FChaosSolversModule::GetModule();
		check(ChaosModule);

		bHasPhysicsState = true;

		if(FieldSystem)
		{
			for(FFieldSystemCommand& Cmd : FieldSystem->Commands)
			{
				DispatchCommand(Cmd);
			}
		}
	}
}

void UFieldSystemComponent::OnDestroyPhysicsState()
{
	UActorComponent::OnDestroyPhysicsState();

	ChaosModule = nullptr;


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
	using namespace Chaos;
	if (HasValidPhysicsState())
	{
		checkSlow(ChaosModule); // Should already be checked from OnCreatePhysicsState

		// Assemble a list of compatible solvers
		TArray<FPhysicsSolverBase*> SupportedSolverList;
		if(SupportedSolvers.Num() > 0)
		{
			for(TSoftObjectPtr<AChaosSolverActor>& SolverActorPtr : SupportedSolvers)
			{
				if(AChaosSolverActor* CurrActor = SolverActorPtr.Get())
				{
					SupportedSolverList.Add(CurrActor->GetSolver());
				}
			}
		}

		TArray<FPhysicsSolverBase*> WorldSolverList = ChaosModule->GetAllSolvers();
		const int32 NumFilterSolvers = SupportedSolverList.Num();

		for(FPhysicsSolverBase* Solver : WorldSolverList)
		{
			const bool bSolverValid = NumFilterSolvers == 0 || SupportedSolverList.Contains(Solver);
			if(bSolverValid)
			{
				Solver->CastHelper([&InCommand](auto& Concrete)
				{
					Concrete.EnqueueCommandImmediate([ConcreteSolver = &Concrete, NewCommand = InCommand]()
					{
						if(ConcreteSolver->HasActiveParticles())
						{
							ConcreteSolver->GetPerSolverField().BufferCommand(NewCommand);
						}
					});
				});
			}
		}
	}
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




