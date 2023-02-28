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

			if constexpr (std::is_base_of_v<FClusterUnionPhysicsProxy::FInternalParticle, TParticle>)
			{
				BufferData.bIsAnchored = Particle->IsAnchored();
			}
		}
	}

	FClusterUnionPhysicsProxy::FClusterUnionPhysicsProxy(UObject* InOwner, const FClusterCreationParameters& InParameters, void* InUserData, EThreadContext InAuthoritativeContext)
		: Base(InOwner)
		, ClusterParameters(InParameters)
		, UserData(InUserData)
		, AuthoritativeContext(InAuthoritativeContext)
	{
	}

	void FClusterUnionPhysicsProxy::Initialize_External()
	{
		// Create game thread particle as well as the physics object.
		Particle_External = FExternalParticle::CreateParticle();
		check(Particle_External != nullptr);

		Particle_External->SetProxy(this);
		Particle_External->SetUserData(UserData);

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
		ClusterUnionIndex = ClusterUnionManager.CreateNewClusterUnion(ClusterParameters, INDEX_NONE, &UniqueIndex);
		if (FClusterUnion* ClusterUnion = ClusterUnionManager.FindClusterUnion(ClusterUnionIndex); ensure(ClusterUnion != nullptr))
		{
			Particle_Internal = ClusterUnion->InternalCluster;
			Particle_Internal->SetPhysicsProxy(this);
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

		int32 OldNum = ChildPhysicsObjects.Num();
		ChildPhysicsObjects.Append(Objects);
		for (FPhysicsObjectHandle Handle : Objects)
		{
			ChildPhysicsObjectIndexMap.Add(Handle, OldNum++);
			SyncedData_External.ChildToParent.Add(FTransform::Identity);
		}
		
		Solver->EnqueueCommandImmediate(
			[this, Objects=Objects]() mutable
			{
				FReadPhysicsObjectInterface_Internal Interface = FPhysicsObjectInternalInterface::GetRead();
				if (FPBDRigidsEvolutionGBF* Evolution = GetEvolution(this))
				{
					Evolution->GetRigidClustering().GetClusterUnionManager().AddPendingClusterIndexOperation(ClusterUnionIndex, EClusterUnionOperation::Add, Interface.GetAllRigidParticles(Objects));
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

		SyncedData_External.bIsAnchored = bIsAnchored;
		Particle_External->ForceDirty(EChaosPropertyFlags::ClusterIsAnchored);
		Solver->AddDirtyProxy(this);

		FClusterUnionProxyTimestamp& SyncTS = GetSyncTimestampAs<FClusterUnionProxyTimestamp>();
		SyncTS.OverWriteAnchored.Set(GetSolverSyncTimestamp_External(), bIsAnchored);
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
		FClusterUnionManager& ClusterUnionManager = Evolution.GetRigidClustering().GetClusterUnionManager();

		if (const FParticlePositionRotation* NewXR = ParticleData.FindClusterXR(Manager, DataIdx))
		{
			Evolution.SetParticleTransform(Particle_Internal, NewXR->X(), NewXR->R(), true);
		}

		if (const FParticleVelocities* NewVelocities = ParticleData.FindClusterVelocities(Manager, DataIdx))
		{
			Particle_Internal->SetVelocities(*NewVelocities);
		}

		if (const bool* bNewIsAnchored = ParticleData.FindClusterIsAnchored(Manager, DataIdx); bNewIsAnchored && *bNewIsAnchored != Particle_Internal->IsAnchored())
		{
			Particle_Internal->SetIsAnchored(*bNewIsAnchored);
			if (!*bNewIsAnchored && !Particle_Internal->IsDynamic())
			{
				FKinematicTarget NoKinematicTarget;
				Evolution.SetParticleObjectState(Particle_Internal, EObjectStateType::Dynamic);
				Evolution.SetParticleKinematicTarget(Particle_Internal, NoKinematicTarget);
			}
		}

		if (AuthoritativeContext == EThreadContext::External)
		{
			if (const TArray<FTransform>* NewChildToParent = ParticleData.FindClusterChildToParent(Manager, DataIdx))
			{
				if (FClusterUnion* ClusterUnion = ClusterUnionManager.FindClusterUnion(ClusterUnionIndex))
				{
					if (NewChildToParent->Num() == ClusterUnion->ChildParticles.Num())
					{
						for (int32 Index = 0; Index < NewChildToParent->Num(); ++Index)
						{
							if (FPBDRigidClusteredParticleHandle* ChildHandle = ClusterUnion->ChildParticles[Index]->CastToClustered())
							{
								if (ChildHandle->ChildToParent().Equals((*NewChildToParent)[Index]))
								{
									continue;
								}
								ChildHandle->SetChildToParent((*NewChildToParent)[Index]);
								RigidsSolver.GetParticles().MarkTransientDirtyParticle(ChildHandle);
							}
						}
					}
				}
			}
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

		if (SolverSyncTimestamp >= ProxyTimestamp->OverWriteAnchored.Timestamp)
		{
			SyncedData_External.bIsAnchored = PullData.bIsAnchored;
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

		if (AuthoritativeContext == EThreadContext::Internal && SolverSyncTimestamp >= ProxyTimestamp->OverWriteChildToParent.Timestamp)
		{
			for (int32 Index = 0; Index < PullData.ChildToParent.Num() && Index < SyncedData_External.ChildToParent.Num(); ++Index)
			{
				SyncedData_External.ChildToParent[Index] = PullData.ChildToParent[Index];
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
			BufferData.ChildToParent.Empty(ClusterUnion->ChildParticles.Num());
			for (FPBDRigidParticleHandle* Particle : ClusterUnion->ChildParticles)
			{
				if (FPBDRigidClusteredParticleHandle* ClusteredParticle = Particle->CastToClustered())
				{
					BufferData.ChildToParent.Add(ClusteredParticle->ChildToParent());
				}
				else
				{
					BufferData.ChildToParent.Add(FTransform::Identity);
				}
			}
		}
	}

	void FClusterUnionPhysicsProxy::BufferPhysicsResults_External(FDirtyClusterUnionData& BufferData)
	{
		BufferPhysicsResultsImp(this, Particle_External.Get(), BufferData);
		BufferData.bIsAnchored = SyncedData_External.bIsAnchored;
		BufferData.ChildToParent = SyncedData_External.ChildToParent;
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
#define CHAOS_PROPERTY(PropName, Type, ProxyType) if constexpr (ProxyType != EPhysicsProxyType::ClusterUnionProxy) { DirtyFlags.MarkClean(EChaosPropertyFlags::PropName); } //-V501
#include "Chaos/ParticleProperties.inl"
#undef CHAOS_PROPERTY

		RemoteData.SetFlags(DirtyFlags);

		// SyncRemote will check the dirty flags and will skip the change in value if the dirty flag is not actually set.
		RemoteData.SyncRemote<FParticlePositionRotation, EChaosProperty::ClusterXR>(Manager, DataIdx, Particle_External->XR());
		RemoteData.SyncRemote<FParticleVelocities, EChaosProperty::ClusterVelocities>(Manager, DataIdx, Particle_External->Velocities());
		RemoteData.SyncRemote<bool, EChaosProperty::ClusterIsAnchored>(Manager, DataIdx, SyncedData_External.bIsAnchored);
		RemoteData.SyncRemote<TArray<FTransform>, EChaosProperty::ClusterChildToParent>(Manager, DataIdx, SyncedData_External.ChildToParent);
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
		if (!ensure(Particle_External))
		{
			return;
		}

		Particle_External->SetX(X, false);
		Particle_External->SetR(R, false);
		Particle_External->ForceDirty(EChaosPropertyFlags::ClusterXR);

		FClusterUnionProxyTimestamp& SyncTS = GetSyncTimestampAs<FClusterUnionProxyTimestamp>();
		SyncTS.OverWriteX.Set(GetSolverSyncTimestamp_External(), X);
		SyncTS.OverWriteR.Set(GetSolverSyncTimestamp_External(), R);
	}

	void FClusterUnionPhysicsProxy::SetLinearVelocity_External(const FVector& V)
	{
		if (!ensure(Particle_External))
		{
			return;
		}

		Particle_External->SetV(V, false);
		Particle_External->ForceDirty(EChaosPropertyFlags::ClusterVelocities);

		FClusterUnionProxyTimestamp& SyncTS = GetSyncTimestampAs<FClusterUnionProxyTimestamp>();
		SyncTS.OverWriteV.Set(GetSolverSyncTimestamp_External(), V);
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
	}

	void FClusterUnionPhysicsProxy::SetChildToParent_External(FPhysicsObjectHandle Child, const FTransform& RelativeTransform)
	{
		if (!Solver || !ensure(Particle_External))
		{
			return;
		}

		if (int32* Index = ChildPhysicsObjectIndexMap.Find(Child); Index && *Index >=0 && *Index < ChildPhysicsObjects.Num() && ensure(ChildPhysicsObjects[*Index] == Child))
		{
			SyncedData_External.ChildToParent[*Index] = RelativeTransform;
			Particle_External->ForceDirty(EChaosPropertyFlags::ClusterChildToParent);
			Solver->AddDirtyProxy(this);

			FClusterUnionProxyTimestamp& SyncTS = GetSyncTimestampAs<FClusterUnionProxyTimestamp>();
			SyncTS.OverWriteChildToParent.Set(GetSolverSyncTimestamp_External(), SyncedData_External.ChildToParent);
		}
	}
}