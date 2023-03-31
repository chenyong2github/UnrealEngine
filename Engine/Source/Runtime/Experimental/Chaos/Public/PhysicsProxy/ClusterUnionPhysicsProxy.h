// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ClusterCreationParameters.h"
#include "Chaos/ClusterUnionManager.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PhysicsObject.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Framework/Threading.h"
#include "PBDRigidsSolver.h"
#include "Templates/UniquePtr.h"

namespace Chaos
{
	struct FDirtyClusterUnionData;

	struct FClusterUnionChildData
	{
		FUniqueIdx ParticleIdx;
		FTransform ChildToParent;
	};

	struct FClusterUnionInitData
	{
		void* UserData;
		uint32 ActorId = INDEX_NONE;
		uint32 ComponentId = INDEX_NONE;
		bool bNeedsClusterXRInitialization = true;
	};

	/**
	 * Extra data that needs to be synced between the PT and GT for cluster unions.
	 */
	struct FClusterUnionSyncedData
	{
		// Whether the cluster is anchored or not.
		bool bIsAnchored;

		// Data on every child particle in the cluster union.
		TArray<FClusterUnionChildData> ChildParticles;
	};
	

	class CHAOS_API FClusterUnionPhysicsProxy : public TPhysicsProxy<FClusterUnionPhysicsProxy, void, FClusterUnionProxyTimestamp>
	{
		using Base = TPhysicsProxy<FClusterUnionPhysicsProxy, void, FClusterUnionProxyTimestamp>;
	public:
		// The mismatch here is fine since we really only need to sync a few properties between the PT and the GT.
		using FExternalParticle = TPBDRigidParticle<FReal, 3>;
		using FInternalParticle = TPBDRigidClusteredParticleHandle<Chaos::FReal, 3>;

		FClusterUnionPhysicsProxy() = delete;
		FClusterUnionPhysicsProxy(UObject* InOwner, const FClusterCreationParameters& InParameters, const FClusterUnionInitData& InInitData);

		// Add physics objects to the cluster union. Should only be called from the game thread.
		void AddPhysicsObjects_External(const TArray<FPhysicsObjectHandle>& Objects);
		const FClusterUnionSyncedData& GetSyncedData_External() const { return SyncedData_External; }

		// Remove physics objects from the cluster union.
		void RemovePhysicsObjects_External(const TSet<FPhysicsObjectHandle>& Objects);

		// Set/Remove any anchors on the cluster particle.
		void SetIsAnchored_External(bool bIsAnchored);

		// Set Object State.
		EObjectStateType GetObjectState_External() const;
		void SetObjectState_External(EObjectStateType State);

		// Cluster union proxy initialization happens in two first on the game thread (external) then on the
		// physics thread (internal). Cluster unions are a primarily physics concept so the things exposed to
		// an external context is just there to be able to safely query the state on the physics thread and to
		// be able to add/remove things to the proxy itself (rather than to move it, for example).
		void Initialize_External();

		bool IsInitializedOnPhysicsThread() const { return bIsInitializedOnPhysicsThread; }
		void Initialize_Internal(FPBDRigidsSolver* RigidsSolver, FPBDRigidsSolver::FParticlesType& Particles);

		FExternalParticle* GetParticle_External() const { return Particle_External.Get(); }
		FInternalParticle* GetParticle_Internal() const { return Particle_Internal; }
		virtual void* GetHandleUnsafe() const override { return Particle_Internal; }
		FPhysicsObjectHandle GetPhysicsObjectHandle() const { return PhysicsObject.Get(); }

		bool HasChildren_External() const { return !SyncedData_External.ChildParticles.IsEmpty(); }
		bool HasChildren_Internal() const;

		bool IsAnchored_External() const { return SyncedData_External.bIsAnchored; }

		void SetXR_External(const FVector& X, const FQuat& R);
		void SetLinearVelocity_External(const FVector& V);
		void SetAngularVelocity_External(const FVector& W);
		void SetChildToParent_External(FPhysicsObjectHandle Child, const FTransform& RelativeTransform);
		void BulkSetChildToParent_External(const TArray<FPhysicsObjectHandle>& Objects, const TArray<FTransform>& Transforms);

		//
		// These functions take care of marshaling data back and forth between the game thread
		// and the physics thread.
		//
		void PushToPhysicsState(const FDirtyPropertiesManager& Manager, int32 DataIdx, const FDirtyProxy& Dirty);
		bool PullFromPhysicsState(const FDirtyClusterUnionData& PullData, int32 SolverSyncTimestamp, const FDirtyClusterUnionData* NextPullData = nullptr, const FRealSingle* Alpha = nullptr);

		void BufferPhysicsResults_Internal(FDirtyClusterUnionData& BufferData);
		void BufferPhysicsResults_External(FDirtyClusterUnionData& BufferData);

		void SyncRemoteData(FDirtyPropertiesManager& Manager, int32 DataIdx, FDirtyChaosProperties& RemoteData) const;
		void ClearAccumulatedData();

		FProxyInterpolationData& GetInterpolationData() { return InterpolationData; }
		const FProxyInterpolationData& GetInterpolationData() const { return InterpolationData; }

		FClusterUnionIndex GetClusterUnionIndex() const { return ClusterUnionIndex; }

	private:
		bool bIsInitializedOnPhysicsThread = false;
		FClusterCreationParameters ClusterParameters;
		const FClusterUnionInitData InitData;

		FPhysicsObjectUniquePtr PhysicsObject;
		TUniquePtr<FExternalParticle> Particle_External;
		FClusterUnionSyncedData SyncedData_External;

		FInternalParticle* Particle_Internal = nullptr;
		FClusterUnionIndex ClusterUnionIndex = INDEX_NONE;

		FProxyInterpolationData InterpolationData;

		//~ Begin TPhysicsProxy Interface
	public:
		bool IsSimulating() const { return true; }
		void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy) {}
		void StartFrameCallback(const float InDt, const float InTime) {}
		void EndFrameCallback(const float InDt) {}
		void CreateRigidBodyCallback(FParticlesType& InOutParticles) {}
		void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs) {}
		void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex) {}
		void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<PhysicsProxyWrapper>& PhysicsProxyReverseMap, Chaos::TArrayCollectionArray<int32>& ParticleIDReverseMap) {}
		EPhysicsProxyType ConcreteType() { return EPhysicsProxyType::ClusterUnionProxy; }
		void SyncBeforeDestroy() {}
		void OnRemoveFromScene() {}
		bool IsDirty() { return false; }

		//~ End TPhysicsProxy Interface
	};

}