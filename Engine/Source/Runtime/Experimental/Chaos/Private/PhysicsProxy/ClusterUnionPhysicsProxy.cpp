// Copyright Epic Games, Inc. All Rights Reserved.
#include "PhysicsProxy/ClusterUnionPhysicsProxy.h"

#include "Chaos/ClusterUnionManager.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PhysicsObjectInternal.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "Chaos/PullPhysicsDataImp.h"
#include "Math/UnrealMathUtility.h"
#include "PBDRigidsSolver.h"

namespace Chaos
{
	namespace
	{
		FPBDRigidsEvolutionGBF* GetEvolution(FClusterUnionPhysicsProxy* Proxy)
		{
			if (!Proxy)
			{
				return nullptr;
			}

			FPBDRigidsSolver* RigidsSolver = Proxy->GetSolver<FPBDRigidsSolver>();
			if (!RigidsSolver)
			{
				return nullptr;
			}

			return RigidsSolver->GetEvolution();
		}

		template<typename TParticle>
		void BufferPhysicsResultsImp(FClusterUnionPhysicsProxy* Proxy, TParticle* Particle, FDirtyClusterUnionData& BufferData)
		{
			if (!Particle || !Proxy)
			{
				return;
			}

			BufferData.SetProxy(*Proxy);
			BufferData.X = Particle->X();
			BufferData.R = Particle->R();
			BufferData.V = Particle->V();
			BufferData.W = Particle->W();
			BufferData.ObjectState = Particle->ObjectState();

			const FShapesArray& ShapeArray = Particle->ShapesArray();
			BufferData.CollisionData.Empty(ShapeArray.Num());
			BufferData.QueryData.Empty(ShapeArray.Num());
			BufferData.SimData.Empty(ShapeArray.Num());

			for (const TUniquePtr<Chaos::FPerShapeData>& ShapeData : ShapeArray)
			{
				BufferData.CollisionData.Add(ShapeData->GetCollisionData());
				BufferData.QueryData.Add(ShapeData->GetQueryData());
				BufferData.SimData.Add(ShapeData->GetSimData());
			}

			if constexpr (std::is_base_of_v<FClusterUnionPhysicsProxy::FInternalParticle, TParticle>)
			{
				BufferData.bIsAnchored = Particle->IsAnchored();
				BufferData.SharedGeometry = ConstCastSharedPtr<FImplicitObject, const FImplicitObject, ESPMode::ThreadSafe>(Particle->SharedGeometry());
			}
			else if constexpr (std::is_base_of_v<FClusterUnionPhysicsProxy::FExternalParticle, TParticle>)
			{
				BufferData.SharedGeometry = ConstCastSharedPtr<FImplicitObject, const FImplicitObject, ESPMode::ThreadSafe>(Particle->SharedGeometryLowLevel());
			}
		}
	}

	FClusterUnionPhysicsProxy::FClusterUnionPhysicsProxy(UObject* InOwner, const FClusterCreationParameters& InParameters, const FClusterUnionInitData& InInitData)
		: Base(InOwner)
		, ClusterParameters(InParameters)
		, InitData(InInitData)
	{
	}

	void FClusterUnionPhysicsProxy::Initialize_External()
	{
		// Create game thread particle as well as the physics object.
		Particle_External = FExternalParticle::CreateParticle();
		check(Particle_External != nullptr);

		Particle_External->SetProxy(this);
		Particle_External->SetUserData(InitData.UserData);

		// NO DIRTY FLAGS ALLOWED. We must strictly manage the dirty flags on the particle.
		// Setting the particle's XR on the particle will set the XR dirty flag but that isn't
		// used for the cluster union (there is no functionality in Chaos to let the particle
		// be easily managed by a proxy that isn't the single particle physics proxy). And if the
		// XR dirty flag is set, we'll try to access buffers that don't exist for cluster union proxies.
		Particle_External->ClearDirtyFlags();
		PhysicsObject = FPhysicsObjectFactory::CreatePhysicsObject(this);
	}

	void FClusterUnionPhysicsProxy::Initialize_Internal(FPBDRigidsSolver* RigidsSolver, FPBDRigidsSolver::FParticlesType& Particles)
	{
		if (!ensure(RigidsSolver) || !ensure(Particle_External != nullptr))
		{
			return;
		}

		bIsInitializedOnPhysicsThread = true;

		FPBDRigidsEvolutionGBF* Evolution = RigidsSolver->GetEvolution();
		if (!ensure(Evolution))
		{
			return;
		}

		FUniqueIdx UniqueIndex = Particle_External->UniqueIdx();
		FClusterUnionManager& ClusterUnionManager = Evolution->GetRigidClustering().GetClusterUnionManager();

		FClusterUnionCreationParameters ClusterUnionParameters;
		ClusterUnionParameters.UniqueIndex = &UniqueIndex;
		ClusterUnionParameters.ActorId = InitData.ActorId;
		ClusterUnionParameters.ComponentId = InitData.ComponentId;

		ClusterUnionIndex = ClusterUnionManager.CreateNewClusterUnion(ClusterParameters, ClusterUnionParameters);
		if (FClusterUnion* ClusterUnion = ClusterUnionManager.FindClusterUnion(ClusterUnionIndex); ensure(ClusterUnion != nullptr))
		{
			Particle_Internal = ClusterUnion->InternalCluster;
			Particle_Internal->SetPhysicsProxy(this);
			Particle_Internal->GTGeometryParticle() = Particle_External.Get();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			Particle_Internal->SetDebugName(MakeShared<FString, ESPMode::ThreadSafe>(FString::Printf(TEXT("%s"), *GetOwner()->GetName())));
#endif

			// On the client, we'd rather wait for the server to initialize the particle properly.
			ClusterUnion->bNeedsXRInitialization = InitData.bNeedsClusterXRInitialization;
		}
	}

	bool FClusterUnionPhysicsProxy::HasChildren_Internal() const
	{
		if (!Particle_Internal)
		{
			return false;
		}

		return Particle_Internal->ClusterIds().NumChildren > 0;
	}

	void FClusterUnionPhysicsProxy::AddPhysicsObjects_External(const TArray<FPhysicsObjectHandle>& Objects)
	{
		if (!Solver)
		{
			return;
		}
		
		Solver->EnqueueCommandImmediate(
			[this, Objects=Objects]() mutable
			{
				FReadPhysicsObjectInterface_Internal Interface = FPhysicsObjectInternalInterface::GetRead();
				if (FPBDRigidsEvolutionGBF* Evolution = GetEvolution(this))
				{
					Evolution->GetRigidClustering().GetClusterUnionManager().HandleAddOperation(ClusterUnionIndex, Interface.GetAllRigidParticles(Objects), false);
				}
			}
		);
	}

	void FClusterUnionPhysicsProxy::RemovePhysicsObjects_External(const TSet<FPhysicsObjectHandle>& Objects)
	{
		if (!Solver || Objects.IsEmpty())
		{
			return;
		}

		Solver->EnqueueCommandImmediate(
			[this, Objects = Objects]() mutable
			{
				FReadPhysicsObjectInterface_Internal Interface = FPhysicsObjectInternalInterface::GetRead();
				if (FPBDRigidsEvolutionGBF* Evolution = GetEvolution(this))
				{
					Evolution->GetRigidClustering().GetClusterUnionManager().HandleRemoveOperation(ClusterUnionIndex, Interface.GetAllRigidParticles(Objects.Array()), true);
				}
			}
		);
	}

	void FClusterUnionPhysicsProxy::SetIsAnchored_External(bool bIsAnchored)
	{
		if (!Solver || !ensure(Particle_External))
		{
			return;
		}

		Solver->EnqueueCommandImmediate(
			[this, bIsAnchored]() mutable
			{
				if (Particle_Internal->IsAnchored() != bIsAnchored)
				{
					if (FPBDRigidsEvolutionGBF* Evolution = GetEvolution(this))
					{
						Particle_Internal->SetIsAnchored(bIsAnchored);
						if (!bIsAnchored && !Particle_Internal->IsDynamic())
						{
							FKinematicTarget NoKinematicTarget;
							Evolution->SetParticleObjectState(Particle_Internal, EObjectStateType::Dynamic);
							Evolution->SetParticleKinematicTarget(Particle_Internal, NoKinematicTarget);
						}
						Evolution->GetParticles().MarkTransientDirtyParticle(Particle_Internal);
					}
				}
			}
		);
	}

	EObjectStateType FClusterUnionPhysicsProxy::GetObjectState_External() const
	{
		if (!ensure(Particle_External))
		{
			return EObjectStateType::Uninitialized;
		}

		return Particle_External->ObjectState();
	}

	void FClusterUnionPhysicsProxy::SetObjectState_External(EObjectStateType State)
	{
		if (!Solver || !ensure(Particle_External))
		{
			return;
		}

		Solver->EnqueueCommandImmediate(
			[this, State]() mutable
			{
				if (Particle_Internal->ObjectState() != State)
				{
					if (FPBDRigidsEvolutionGBF* Evolution = GetEvolution(this))
					{
						Evolution->SetParticleObjectState(Particle_Internal, State);
					}
				}
			}
		);
	}

	void FClusterUnionPhysicsProxy::PushToPhysicsState(const FDirtyPropertiesManager& Manager, int32 DataIdx, const FDirtyProxy& Dirty)
	{
		if (!ensure(Solver) || !ensure(Particle_Internal))
		{
			return;
		}

		const FDirtyChaosProperties& ParticleData = Dirty.PropertyData;
		FPBDRigidsSolver& RigidsSolver = *static_cast<FPBDRigidsSolver*>(Solver);
		if (!ensure(RigidsSolver.GetEvolution()))
		{
			return;
		}

		FPBDRigidsEvolutionGBF& Evolution = *RigidsSolver.GetEvolution();

		if (const FParticlePositionRotation* NewXR = ParticleData.FindClusterXR(Manager, DataIdx))
		{
			Evolution.SetParticleTransform(Particle_Internal, NewXR->X(), NewXR->R(), true);

			// Particle needs to actually be marked dirty! Less so to pull state from the PT back to the GT for the cluster union
			// but this is primarily for proxies within the cluster union. They (i.e. GCs) need to see this change to the particle's transform.
			Evolution.GetParticles().MarkTransientDirtyParticle(Particle_Internal);

			const FRigidTransform3 WorldTransform{ Particle_Internal->X(), Particle_Internal->R() };
			Particle_Internal->UpdateWorldSpaceState(WorldTransform, FVec3(0));

			Evolution.DirtyParticle(*Particle_Internal);
		}

		if (const FParticleVelocities* NewVelocities = ParticleData.FindClusterVelocities(Manager, DataIdx))
		{
			Particle_Internal->SetVelocities(*NewVelocities);
		}
	}

	bool FClusterUnionPhysicsProxy::PullFromPhysicsState(const FDirtyClusterUnionData& PullData, int32 SolverSyncTimestamp, const FDirtyClusterUnionData* NextPullData, const FRealSingle* Alpha)
	{
		if (!ensure(Particle_External))
		{
			return false;
		}

		const FClusterUnionProxyTimestamp* ProxyTimestamp = PullData.GetTimestamp();
		if (!ProxyTimestamp)
		{
			return false;
		}

		
		SyncedData_External.bIsAnchored = PullData.bIsAnchored;
		SyncedData_External.ChildParticles.Empty(PullData.ChildParticles.Num());
		for (const FDirtyClusterUnionParticleData& InData : PullData.ChildParticles)
		{
			FClusterUnionChildData ConvertedData;
			ConvertedData.Particle = InData.Particle;
			ConvertedData.ChildToParent = InData.ChildToParent;
			SyncedData_External.ChildParticles.Add(ConvertedData);
		}

		Particle_External->SetGeometry(PullData.SharedGeometry);
		Particle_External->SetObjectState(PullData.ObjectState, true, /*bInvalidate=*/false);

		const FShapesArray& ShapeArray = Particle_External->ShapesArray();
		for (int32 ShapeIndex = 0; ShapeIndex < ShapeArray.Num(); ++ShapeIndex)
		{
			if (const TUniquePtr<Chaos::FPerShapeData>& ShapeData = ShapeArray[ShapeIndex])
			{
				if (PullData.CollisionData.IsValidIndex(ShapeIndex))
				{
					ShapeData->SetCollisionData(PullData.CollisionData[ShapeIndex]);
				}

				if (PullData.QueryData.IsValidIndex(ShapeIndex))
				{
					ShapeData->SetQueryData(PullData.QueryData[ShapeIndex]);
				}

				if (PullData.SimData.IsValidIndex(ShapeIndex))
				{
					ShapeData->SetSimData(PullData.SimData[ShapeIndex]);
				}
			}
		}

		if (NextPullData)
		{
			// This is the same as in the SingleParticlePhysicsProxy.
			auto LerpHelper = [SolverSyncTimestamp](const auto& Prev, const auto& OverwriteProperty) -> const auto*
			{
				//if overwrite is in the future, do nothing
				//if overwrite is on this step, we want to interpolate from overwrite to the result of the frame that consumed the overwrite
				//if overwrite is in the past, just do normal interpolation

				//this is nested because otherwise compiler can't figure out the type of nullptr with an auto return type
				return OverwriteProperty.Timestamp <= SolverSyncTimestamp ? (OverwriteProperty.Timestamp < SolverSyncTimestamp ? &Prev : &OverwriteProperty.Value) : nullptr;
			};

			if (const FVec3* Prev = LerpHelper(PullData.X, ProxyTimestamp->OverWriteX))
			{
				const FVec3 NewX = FMath::Lerp(*Prev, NextPullData->X, *Alpha);
				Particle_External->SetX(NewX, false);
			}

			if (const FQuat* Prev = LerpHelper(PullData.R, ProxyTimestamp->OverWriteR))
			{
				const FQuat NewR = FMath::Lerp(*Prev, NextPullData->R, *Alpha);
				Particle_External->SetR(NewR, false);
			}

			if (const FVec3* Prev = LerpHelper(PullData.V, ProxyTimestamp->OverWriteV))
			{
				const FVec3 NewV = FMath::Lerp(*Prev, NextPullData->V, *Alpha);
				Particle_External->SetV(NewV, false);
			}

			if (const FVec3* Prev = LerpHelper(PullData.W, ProxyTimestamp->OverWriteW))
			{
				const FVec3 NewW = FMath::Lerp(*Prev, NextPullData->W, *Alpha);
				Particle_External->SetW(NewW, false);
			}
		}
		else
		{
			if (SolverSyncTimestamp >= ProxyTimestamp->OverWriteX.Timestamp)
			{
				Particle_External->SetX(PullData.X, false);
			}

			if (SolverSyncTimestamp >= ProxyTimestamp->OverWriteR.Timestamp)
			{
				Particle_External->SetR(PullData.R, false);
			}

			if (SolverSyncTimestamp >= ProxyTimestamp->OverWriteV.Timestamp)
			{
				Particle_External->SetV(PullData.V, false);
			}

			if (SolverSyncTimestamp >= ProxyTimestamp->OverWriteW.Timestamp)
			{
				Particle_External->SetW(PullData.W, false);
			}
		}
		
		Particle_External->UpdateShapeBounds();
		return true;
	}

	void FClusterUnionPhysicsProxy::BufferPhysicsResults_Internal(FDirtyClusterUnionData& BufferData)
	{
		BufferPhysicsResultsImp(this, Particle_Internal, BufferData);
	
		FPBDRigidsEvolutionGBF& Evolution = *static_cast<FPBDRigidsSolver*>(Solver)->GetEvolution();
		FClusterUnionManager& ClusterUnionManager = Evolution.GetRigidClustering().GetClusterUnionManager();

		if (FClusterUnion* ClusterUnion = ClusterUnionManager.FindClusterUnion(ClusterUnionIndex))
		{
			BufferData.ChildParticles.Empty(ClusterUnion->ChildParticles.Num());

			for (FPBDRigidParticleHandle* Particle : ClusterUnion->ChildParticles)
			{
				if (!ensure(Particle->GTGeometryParticle()))
				{
					continue;
				}

				FDirtyClusterUnionParticleData Data;
				Data.Particle = Particle->GTGeometryParticle()->CastToRigidParticle();
				if (FPBDRigidClusteredParticleHandle* ClusteredParticle = Particle->CastToClustered())
				{
					Data.ChildToParent = ClusteredParticle->ChildToParent();
				}
				else
				{
					Data.ChildToParent = FRigidTransform3::Identity;
				}
				BufferData.ChildParticles.Add(Data);
			}

			BufferData.SharedGeometry = ClusterUnion->SharedGeometry;
		}
	}

	void FClusterUnionPhysicsProxy::BufferPhysicsResults_External(FDirtyClusterUnionData& BufferData)
	{
		BufferPhysicsResultsImp(this, Particle_External.Get(), BufferData);
		BufferData.bIsAnchored = SyncedData_External.bIsAnchored;

		BufferData.ChildParticles.Empty(SyncedData_External.ChildParticles.Num());
		for (const FClusterUnionChildData& Data : SyncedData_External.ChildParticles)
		{
			FDirtyClusterUnionParticleData ConvertedData;
			ConvertedData.Particle = Data.Particle;
			ConvertedData.ChildToParent = Data.ChildToParent;
			BufferData.ChildParticles.Add(ConvertedData);
		}
	}

	void FClusterUnionPhysicsProxy::SyncRemoteData(FDirtyPropertiesManager& Manager, int32 DataIdx, FDirtyChaosProperties& RemoteData) const
	{
		if (!ensure(Particle_External))
		{
			return;
		}

		// This is similar to TGeometryParticle::SyncRemoteData except it puts it into the cluster properties.
		RemoteData.SetParticleBufferType(Particle_External->Type);

		// We need to modify the dirty flags to remove the non cluster properties to be 100% safe.
		FDirtyChaosPropertyFlags DirtyFlags = Particle_External->DirtyFlags();

		// We need to suppress V501 in PVS Studio since it's a false positive warning in this case. This is actually necessary for the codegen here.
//-V:CHAOS_PROPERTY:501
#define CHAOS_PROPERTY(PropName, Type, ProxyType) if constexpr (ProxyType != EPhysicsProxyType::ClusterUnionProxy) { DirtyFlags.MarkClean(EChaosPropertyFlags::PropName); }
#include "Chaos/ParticleProperties.inl" //-V:CHAOS_PROPERTY:501
#undef CHAOS_PROPERTY

		RemoteData.SetFlags(DirtyFlags);

		// SyncRemote will check the dirty flags and will skip the change in value if the dirty flag is not actually set.
		RemoteData.SyncRemote<FParticlePositionRotation, EChaosProperty::ClusterXR>(Manager, DataIdx, Particle_External->XR());
		RemoteData.SyncRemote<FParticleVelocities, EChaosProperty::ClusterVelocities>(Manager, DataIdx, Particle_External->Velocities());
	}

	void FClusterUnionPhysicsProxy::ClearAccumulatedData()
	{
		if (!ensure(Particle_External))
		{
			return;
		}

		Particle_External->ClearDirtyFlags();
	}

	void FClusterUnionPhysicsProxy::SetXR_External(const FVector& X, const FQuat& R)
	{
		if (!Solver || !ensure(Particle_External))
		{
			return;
		}

		Particle_External->SetX(X, false);
		Particle_External->SetR(R, false);
		Particle_External->ForceDirty(EChaosPropertyFlags::ClusterXR);

		FClusterUnionProxyTimestamp& SyncTS = GetSyncTimestampAs<FClusterUnionProxyTimestamp>();
		SyncTS.OverWriteX.Set(GetSolverSyncTimestamp_External(), X);
		SyncTS.OverWriteR.Set(GetSolverSyncTimestamp_External(), R);

		Solver->AddDirtyProxy(this);
	}

	void FClusterUnionPhysicsProxy::SetLinearVelocity_External(const FVector& V)
	{
		if (!Solver || !ensure(Particle_External))
		{
			return;
		}

		Particle_External->SetV(V, false);
		Particle_External->ForceDirty(EChaosPropertyFlags::ClusterVelocities);

		FClusterUnionProxyTimestamp& SyncTS = GetSyncTimestampAs<FClusterUnionProxyTimestamp>();
		SyncTS.OverWriteV.Set(GetSolverSyncTimestamp_External(), V);

		Solver->AddDirtyProxy(this);
	}

	void FClusterUnionPhysicsProxy::SetAngularVelocity_External(const FVector& W)
	{
		if (!ensure(Particle_External))
		{
			return;
		}

		Particle_External->SetW(W, false);
		Particle_External->ForceDirty(EChaosPropertyFlags::ClusterVelocities);

		FClusterUnionProxyTimestamp& SyncTS = GetSyncTimestampAs<FClusterUnionProxyTimestamp>();
		SyncTS.OverWriteW.Set(GetSolverSyncTimestamp_External(), W);

		Solver->AddDirtyProxy(this);
	}

	void FClusterUnionPhysicsProxy::SetChildToParent_External(FPhysicsObjectHandle Child, const FTransform& RelativeTransform)
	{
		BulkSetChildToParent_External({ Child }, { RelativeTransform });
	}

	void FClusterUnionPhysicsProxy::BulkSetChildToParent_External(const TArray<FPhysicsObjectHandle>& Objects, const TArray<FTransform>& Transforms)
	{
		if (!Solver || !ensure(Particle_External) || !ensure(Objects.Num() == Transforms.Num()))
		{
			return;
		}

		Solver->EnqueueCommandImmediate(
			[this, Objects, Transforms]() mutable
			{
				FReadPhysicsObjectInterface_Internal Interface = FPhysicsObjectInternalInterface::GetRead();
				TArray<FPBDRigidParticleHandle*> Particles = Interface.GetAllRigidParticles(Objects);
				if (ensure(Particles.Num() == Objects.Num()))
				{
					FPBDRigidsEvolutionGBF& Evolution = *static_cast<FPBDRigidsSolver*>(Solver)->GetEvolution();
					FClusterUnionManager& ClusterUnionManager = Evolution.GetRigidClustering().GetClusterUnionManager();
					ClusterUnionManager.UpdateClusterUnionParticlesChildToParent(ClusterUnionIndex, Particles, Transforms);
				}
			}
		);
	}
}