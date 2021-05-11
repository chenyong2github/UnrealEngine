// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ParticleHandle.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"

namespace Chaos
{
class FParticleUniqueIndices
{
public:
	FParticleUniqueIndices()
		: Block(0)
	{
		//Note: tune this so that all allocation is done at initialization
		AddPageAndAcquireNextId(/*bAcquireNextId = */ false);
	}

	FUniqueIdx GenerateUniqueIdx()
	{
		while(true)
		{
			if(FUniqueIdx* Idx = FreeIndices.Pop())
			{
				return *Idx;
			}

			//nothing available so try to add some

			if(FPlatformAtomics::InterlockedCompareExchange(&Block,1,0) == 0)
			{
				//we got here first so add a new page
				FUniqueIdx RetIdx = FUniqueIdx(AddPageAndAcquireNextId(/*bAcquireNextId =*/ true));

				//release blocker. Note: I don't think this can ever fail, but no real harm in the while loop
				while(FPlatformAtomics::InterlockedCompareExchange(&Block,0,1) != 1)
				{
				}

				return RetIdx;
			}
		}
	}

	void ReleaseIdx(FUniqueIdx Unique)
	{
		ensure(Unique.IsValid());
		int32 PageIdx = Unique.Idx / IndicesPerPage;
		int32 Entry = Unique.Idx % IndicesPerPage;
		FUniqueIdxPage& Page = *Pages[PageIdx];
		FreeIndices.Push(&Page.Indices[Entry]);
	}

	~FParticleUniqueIndices()
	{
		//Make sure queue is empty, memory management of actual pages is handled automatically by TUniquePtr
		while(FreeIndices.Pop())
		{
		}
	}

private:

	int32 AddPageAndAcquireNextId(bool bAcquireNextIdx)
	{
		//Note: this should never really be called post initialization
		TUniquePtr<FUniqueIdxPage> Page = MakeUnique<FUniqueIdxPage>();
		const int32 PageIdx = Pages.Num();
		int32 FirstIdxInPage = PageIdx * IndicesPerPage;
		Page->Indices[0] = FUniqueIdx(FirstIdxInPage);

		//If we acquire next id we avoid pushing it into the queue
		for(int32 Count = bAcquireNextIdx ? 1 : 0; Count < IndicesPerPage; Count++)
		{
			Page->Indices[Count] = FUniqueIdx(FirstIdxInPage + Count);
			FreeIndices.Push(&Page->Indices[Count]);
		}

		Pages.Emplace(MoveTemp(Page));
		return bAcquireNextIdx ? FirstIdxInPage : INDEX_NONE;
	}

	static constexpr int32 IndicesPerPage = 1024;
	struct FUniqueIdxPage
	{
		FUniqueIdx Indices[IndicesPerPage];
	};
	TArray<TUniquePtr<FUniqueIdxPage>> Pages;
	TLockFreePointerListFIFO<FUniqueIdx,0> FreeIndices;
	volatile int8 Block;
};

template <typename TParticleType>
class TParticleMapArray
{
public:
	void Reset()
	{
		ParticleToIndex.Reset();
		ParticleArray.Reset();
	}

	template <typename TParticle1>
	void Insert(const TArray<TParticle1*>& ParticlesToInsert)
	{
		TArray<bool> Contains;
		Contains.AddZeroed(ParticlesToInsert.Num());

		// TODO: Compile time check ensuring TParticle2 is derived from TParticle1?
		int32 NextIdx = ParticleArray.Num();
		for (int32 Idx = 0; Idx < ParticlesToInsert.Num(); ++Idx)
		{
			auto Particle = ParticlesToInsert[Idx];
			Contains[Idx] = ParticleToIndex.Contains(Particle);
			if (!Contains[Idx])
			{
				ParticleToIndex.Add(Particle, NextIdx++);
			}
		}
		ParticleArray.Reserve(ParticleArray.Num() + NextIdx - ParticleArray.Num());
		for (int32 Idx = 0; Idx < ParticlesToInsert.Num(); ++Idx)
		{
			if (!Contains[Idx])
			{
				auto Particle = ParticlesToInsert[Idx];
				ParticleArray.Add(Particle);
			}
		}
	}

	void Insert(TParticleType* Particle)
	{
		if (ParticleToIndex.Contains(Particle) == false)
		{
			ParticleToIndex.Add(Particle, ParticleArray.Num());
			ParticleArray.Add(Particle);
		}
	}

	void Remove(TParticleType* Particle)
	{
		if (int32* IdxPtr = ParticleToIndex.Find(Particle))
		{
			int32 Idx = *IdxPtr;
			ParticleArray.RemoveAtSwap(Idx);
			if (Idx < ParticleArray.Num())
			{
				//update swapped element with new index
				ParticleToIndex[ParticleArray[Idx]] = Idx;
			}
			ParticleToIndex.Remove(Particle);
		}
	}

	void Serialize(FChaosArchive& Ar)
	{
		TArray<TSerializablePtr<TParticleType>>& SerializableArray = AsAlwaysSerializableArray(ParticleArray);
		Ar << SerializableArray;

		int32 Idx = 0;
		for (auto Particle : ParticleArray)
		{
			ParticleToIndex.Add(Particle, Idx++);
		}
	}

	const TArray<TParticleType*>& GetArray() const { return ParticleArray;	}
	TArray<TParticleType*>& GetArray() { return ParticleArray; }

private:
	TMap<TParticleType*, int32> ParticleToIndex;
	TArray<TParticleType*> ParticleArray;
};

class FPBDRigidsSOAs
{
public:
	FPBDRigidsSOAs()
		: bDisableParticleDeletion(false)
	{
#if CHAOS_DETERMINISTIC
		BiggestParticleID = 0;
#endif

		StaticParticles = MakeUnique<FGeometryParticles>();
		StaticDisabledParticles = MakeUnique <FGeometryParticles>();

		KinematicParticles = MakeUnique < FKinematicGeometryParticles>();
		KinematicDisabledParticles = MakeUnique < FKinematicGeometryParticles>();

		DynamicDisabledParticles = MakeUnique<FPBDRigidParticles>();
		DynamicParticles = MakeUnique<FPBDRigidParticles>();
		DynamicKinematicParticles = MakeUnique<FPBDRigidParticles>();

		ClusteredParticles = MakeUnique< FPBDRigidClusteredParticles>();
		ClusteredParticles->RemoveParticleBehavior() = ERemoveParticleBehavior::Remove;	//clustered particles maintain relative ordering

		GeometryCollectionParticles = MakeUnique<TPBDGeometryCollectionParticles<FReal, 3>>();
		GeometryCollectionParticles->RemoveParticleBehavior() = ERemoveParticleBehavior::Remove;	//clustered particles maintain relative ordering
		bGeometryCollectionDirty = false;

		UpdateViews();
	}

	FPBDRigidsSOAs(const FPBDRigidsSOAs&) = delete;
	FPBDRigidsSOAs(FPBDRigidsSOAs&& Other) = delete;

	~FPBDRigidsSOAs()
	{
		// Abandonning the particles, don't worry about ordering anymore.
		ClusteredParticles->RemoveParticleBehavior() = ERemoveParticleBehavior::RemoveAtSwap;
		GeometryCollectionParticles->RemoveParticleBehavior() = ERemoveParticleBehavior::RemoveAtSwap;
	}

	void Reset()
	{
		check(0);
	}
	
	TArray<FGeometryParticleHandle*> CreateStaticParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr, const FGeometryParticleParameters& Params = FGeometryParticleParameters())
	{
		auto Results = CreateParticlesHelper<FGeometryParticleHandle>(NumParticles, ExistingIndices, Params.bDisabled ? StaticDisabledParticles : StaticParticles, Params);
		UpdateViews();
		return Results;
	}
	TArray<FKinematicGeometryParticleHandle*> CreateKinematicParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr,  const FKinematicGeometryParticleParameters& Params = FKinematicGeometryParticleParameters())
	{
		auto Results = CreateParticlesHelper<FKinematicGeometryParticleHandle>(NumParticles, ExistingIndices, Params.bDisabled ? KinematicDisabledParticles : KinematicParticles, Params);
		UpdateViews();
		return Results;
	}
	TArray<FPBDRigidParticleHandle*> CreateDynamicParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr,  const FPBDRigidParticleParameters& Params = FPBDRigidParticleParameters())
	{
		auto Results = CreateParticlesHelper<FPBDRigidParticleHandle>(NumParticles, ExistingIndices, Params.bDisabled ? DynamicDisabledParticles : DynamicParticles, Params);;

		if (!Params.bStartSleeping)
		{
			InsertToMapAndArray(Results, ActiveParticlesToIndex, ActiveParticlesArray);
		}
		UpdateViews();
		return Results;
	}
	TArray<TPBDGeometryCollectionParticleHandle<FReal, 3>*> CreateGeometryCollectionParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr, const FPBDRigidParticleParameters& Params = FPBDRigidParticleParameters())
	{
		TArray<TPBDGeometryCollectionParticleHandle<FReal, 3>*> Results = CreateParticlesHelper<TPBDGeometryCollectionParticleHandle<FReal, 3>>(
			NumParticles, ExistingIndices, GeometryCollectionParticles, Params);
		for (auto* Handle : Results)
		{
			if (Params.bStartSleeping)
			{
				Handle->SetObjectStateLowLevel(Chaos::EObjectStateType::Sleeping);
				Handle->SetSleeping(true);
			}
			else
			{
				Handle->SetObjectStateLowLevel(Chaos::EObjectStateType::Dynamic);
				Handle->SetSleeping(false);
			}
		}
		bGeometryCollectionDirty = true;
		return Results;
	}

	/** Used specifically by PBDRigidClustering. These have special properties for maintaining relative order, efficiently switching from kinematic to dynamic, disable to enable, etc... */
	TArray<FPBDRigidClusteredParticleHandle*> CreateClusteredParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr,  const FPBDRigidParticleParameters& Params = FPBDRigidParticleParameters())
	{
		auto NewClustered = CreateParticlesHelper<FPBDRigidClusteredParticleHandle>(NumParticles, ExistingIndices, ClusteredParticles, Params);
		
		if (!Params.bDisabled)
		{
			InsertClusteredParticles(NewClustered);
		}

		if (!Params.bStartSleeping)
		{
			AddToActiveArray(reinterpret_cast<TArray<FPBDRigidParticleHandle*>&>(NewClustered));
		}

		UpdateViews();
		
		return NewClustered;
	}
	
	void ClearTransientDirty()
	{
		TransientDirtyMapArray.Reset();
	}

	void MarkTransientDirtyParticle(FGeometryParticleHandle* Particle)
	{
		FPBDRigidParticleHandle* Rigid =  Particle->CastToRigidParticle();
		if(Rigid)
		{
			TransientDirtyMapArray.Insert(Rigid);
		}

		UpdateViews();
	}

	// WARNING
	// Only ever use DisableParticleDeletion when debugging the particle clean up. 
	// This introduces a massive memory leak.
	bool bDisableParticleDeletion;
	void SetDisableParticleDeletion(bool bIn) { bDisableParticleDeletion = bIn; }

	void DestroyParticle(FGeometryParticleHandle* Particle)
	{
		auto PBDRigid = Particle->CastToRigidParticle();
		if(PBDRigid)
		{
			RemoveFromActiveArray(PBDRigid, /*bStillDirty=*/ false);

			if (auto PBDRigidClustered = Particle->CastToClustered())
			{
				if (Particle->GetParticleType() == Chaos::EParticleType::GeometryCollection)
				{
					bGeometryCollectionDirty = true;
				}
				else // clustered
				{
					DynamicClusteredMapArray.Remove(PBDRigidClustered);
				}
			}
			else
			{
				Particle->MoveToSOA(*DynamicDisabledParticles);
			}

			// Check for sleep events referencing this particle
			// TODO think about this case more
			GetDynamicParticles().GetSleepDataLock().WriteLock();
			TArray<TSleepData<FReal, 3>>& SleepData = GetDynamicParticles().GetSleepData();

			SleepData.RemoveAllSwap([Particle](TSleepData<FReal, 3>& Entry) 
			{
				return Entry.Particle == Particle;
			});

			GetDynamicParticles().GetSleepDataLock().WriteUnlock();
		}

		// WARNING
		// Only ever use DisableParticleDeletion when debugging the particle clean up. 
		// This introduces a massive memory leak.
		if (!bDisableParticleDeletion)
		{
			ParticleHandles.DestroyHandleSwap(Particle);
		}

		UpdateViews();
	}

	/**
	 * A disabled particle is ignored by the solver.
	 */
	void DisableParticle(FGeometryParticleHandle* Particle)
	{
		// Rigid particles express their disabled state with a boolean.
		// Disabled kinematic and static particles get shuffled to differnt SOAs.

		if (auto PBDRigid = Particle->CastToRigidParticle())
		{
			PBDRigid->Disabled() = true;
			PBDRigid->V() = FVec3(0);
			PBDRigid->W() = FVec3(0);

			if (auto PBDRigidClustered = Particle->CastToClustered())
			{
				if (Particle->GetParticleType() == Chaos::EParticleType::GeometryCollection)
				{
					bGeometryCollectionDirty = true;
					return;
				}
				else // clustered
				{
					RemoveClusteredParticle(PBDRigidClustered);
				}
			}
			else
			{
				SetDynamicParticleSOA(PBDRigid);
			}

			// All active particles RIGID particles
			{
				RemoveFromActiveArray(PBDRigid, /*bStillDirty=*/false);
			}
		}
		else if (Particle->CastToKinematicParticle())
		{
			Particle->MoveToSOA(*KinematicDisabledParticles);
		}
		else
		{
			Particle->MoveToSOA(*StaticDisabledParticles);
		}
		UpdateViews();
	}

	void EnableParticle(FGeometryParticleHandle* Particle)
	{
		// Rigid particles express their disabled state with a boolean.
		// Disabled kinematic and static particles get shuffled to differnt SOAs.

		if (auto PBDRigid = Particle->CastToRigidParticle())
		{
			PBDRigid->Disabled() = false;
			// DisableParticle() zeros V and W.  We do nothing here and assume the client
			// sets appropriate values.

			if (auto PBDRigidClustered = Particle->CastToClustered())
			{
				if (Particle->GetParticleType() == Chaos::EParticleType::GeometryCollection)
				{
					bGeometryCollectionDirty = true;
					return;
				}
				else // clustered
				{
					InsertClusteredParticle(PBDRigidClustered);
				}
			}
			else
			{
				SetDynamicParticleSOA(PBDRigid);
			}

			if (!PBDRigid->Sleeping() && Particle->ObjectState() == EObjectStateType::Dynamic)
			{
				AddToActiveArray(PBDRigid);
			}
		}
		else if (Particle->CastToKinematicParticle())
		{
			Particle->MoveToSOA(*KinematicParticles);
		}
		else
		{
			Particle->MoveToSOA(*StaticParticles);
		}
		UpdateViews();
	}

	/**
	 * Wake a sleeping dynamic non-disabled particle.
	 * return true if Geometry collection needs to be updated
	 */
	bool ActivateParticle(FGeometryParticleHandle* Particle)
	{
		if (auto PBDRigid = Particle->CastToRigidParticle())
		{
			if (PBDRigid->ObjectState() == EObjectStateType::Sleeping ||
				PBDRigid->ObjectState() == EObjectStateType::Dynamic)
			{
				if (ensure(!PBDRigid->Disabled()))
				{
					// Sleeping state is currently expressed in 2 places...
					PBDRigid->SetSleeping(false);
					PBDRigid->SetObjectStateLowLevel(EObjectStateType::Dynamic);
		
					bool bUpdateGeometryCollection = false;
					if (auto PBDRigidClustered = Particle->CastToClustered())
					{
						if (Particle->GetParticleType() == Chaos::EParticleType::GeometryCollection)
						{
							bGeometryCollectionDirty = true;
							return true;
						}
					}
					else
					{
						// Non clustered rigid particles:
						AddToActiveArray(PBDRigid);
					}

					UpdateViews();
				}
			}
		}
		return false;
	}

	/**
	 * Wake multiple dynamic non-disabled particles.
	 */
	void ActivateParticles(const TArray<FGeometryParticleHandle*>& Particles)
	{
		bool bUpdateGeometryCollection = false;
		for (auto Particle : Particles)
		{
			bUpdateGeometryCollection |= ActivateParticle(Particle);
		}
		if (bUpdateGeometryCollection)
		{
			UpdateGeometryCollectionViews();
		}
		else
		{
			UpdateViews();
		}
	}

	/**
	 * Put a non-disabled dynamic particle to sleep.
	 *
	 * If \p DeferUpdateViews is \c true, then it's assumed this function
	 * is being called in a loop and it won't update the SOA view arrays.
	 */
	void DeactivateParticle(
		FGeometryParticleHandle* Particle,
		const bool DeferUpdateViews=false)
	{
		if(auto PBDRigid = Particle->CastToRigidParticle())
		{
			if (PBDRigid->ObjectState() == EObjectStateType::Dynamic ||
				PBDRigid->ObjectState() == EObjectStateType::Sleeping)
			{
				if (ensure(!PBDRigid->Disabled()))
				{
					// Sleeping state is currently expressed in 2 places...
					PBDRigid->SetSleeping(true);
					PBDRigid->SetObjectStateLowLevel(EObjectStateType::Sleeping);

					if (auto PBDRigidClustered = Particle->CastToClustered())
					{
						if (Particle->GetParticleType() == Chaos::EParticleType::GeometryCollection)
						{
							bGeometryCollectionDirty = true;
						}
					}
					else
					{
						RemoveFromActiveArray(PBDRigid, /*bStillDirty=*/true);
					}

					if (!DeferUpdateViews)
					{
						UpdateViews();
					}
				}
			}
		}
	}

	/**
	 * Put multiple dynamic non-disabled particles to sleep.
	 */
	void DeactivateParticles(const TArray<FGeometryParticleHandle*>& Particles)
	{
		for (auto Particle : Particles)
		{
			DeactivateParticle(Particle, true);
		}
		UpdateIfNeeded();
		UpdateViews();
	}

	void SetDynamicParticleSOA(FPBDRigidParticleHandle* Particle)
	{
		const EObjectStateType State = Particle->ObjectState();

		if (Particle->Disabled())
		{
			Particle->MoveToSOA(*DynamicDisabledParticles);
			ActiveParticlesMapArray.Remove(Particle->CastToRigidParticle());
		}
		else
		{
			if (Particle->ObjectState() != EObjectStateType::Dynamic)
			{
				RemoveFromActiveArray(Particle->CastToRigidParticle(), /*bStillDirty=*/true);
			}
			else
			{
				AddToActiveArray(Particle->CastToRigidParticle());
			}

			// Move to appropriate dynamic SOA
			switch (State)
			{
			case EObjectStateType::Kinematic:
				Particle->MoveToSOA(*DynamicKinematicParticles);
				break;

			case EObjectStateType::Dynamic:
				Particle->MoveToSOA(*DynamicParticles);
				break;

			default:
				// TODO: Special SOAs for sleeping and static particles?
				Particle->MoveToSOA(*DynamicParticles);
				break;
			}
		}
		UpdateViews();
	}

	void SetClusteredParticleSOA(FPBDRigidClusteredParticleHandle* ClusteredParticle)
	{
		RemoveClusteredParticle(ClusteredParticle);
		InsertClusteredParticle(ClusteredParticle);
		UpdateViews();
	}

	void Serialize(FChaosArchive& Ar)
	{
		static const FName SOAsName = TEXT("PBDRigidsSOAs");
		FChaosArchiveScopedMemory ScopedMemory(Ar, SOAsName, false);

		ParticleHandles.Serialize(Ar);

		Ar << StaticParticles;
		Ar << StaticDisabledParticles;
		Ar << KinematicParticles;
		Ar << KinematicDisabledParticles;
		Ar << DynamicParticles;
		Ar << DynamicDisabledParticles;
		// TODO: Add an entry in UObject/ExternalPhysicsCustomObjectVersion.h when adding these back in:
		//Ar << ClusteredParticles;
		//Ar << GeometryCollectionParticles;

		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddDynamicKinematicSOA)
		{
			Ar << DynamicKinematicParticles;
		}

		{
			//need to assign indices to everything
			auto AssignIdxHelper = [&](const auto& Particles)
			{
				for(uint32 ParticleIdx = 0; ParticleIdx < Particles->Size(); ++ParticleIdx)
				{
					FUniqueIdx Unique = UniqueIndices.GenerateUniqueIdx();
					Particles->UniqueIdx(ParticleIdx) = Unique;
					Particles->GTGeometryParticle(ParticleIdx)->SetUniqueIdx(Unique);
				}
			};

			AssignIdxHelper(StaticParticles);
			AssignIdxHelper(StaticDisabledParticles);
			AssignIdxHelper(KinematicParticles);
			AssignIdxHelper(DynamicParticles);
			AssignIdxHelper(DynamicParticles);
			AssignIdxHelper(DynamicDisabledParticles);
			//AssignIdxHelper(ClusteredParticles);
			//AssignIdxHelper(GeometryCollectionParticles);
		}

		ensure(ClusteredParticles->Size() == 0);	//not supported yet
		//Ar << ClusteredParticles;
		Ar << GeometryCollectionParticles;

		ActiveParticlesMapArray.Serialize(Ar);
		//DynamicClusteredMapArray.Serialize(Ar);

		//todo: update deterministic ID

		//if (!GeometryCollectionParticles || !GeometryCollectionParticles->Size())
			UpdateViews();
		//else
		//	UpdateGeometryCollectionViews();
	}


	const TParticleView<FGeometryParticles>& GetNonDisabledView() const { UpdateIfNeeded();  return NonDisabledView; }

	const TParticleView<FPBDRigidParticles>& GetNonDisabledDynamicView() const { UpdateIfNeeded(); return NonDisabledDynamicView; }

	const TParticleView<FPBDRigidClusteredParticles>& GetNonDisabledClusteredView() const { return NonDisabledClusteredView; }

	const TParticleView<FPBDRigidParticles>& GetActiveParticlesView() const { UpdateIfNeeded(); return ActiveParticlesView; }
	TParticleView<FPBDRigidParticles>& GetActiveParticlesView() { UpdateIfNeeded(); return ActiveParticlesView; }

	const TArray<FPBDRigidParticleHandle*>& GetActiveParticlesArray() const { return ActiveParticlesMapArray.GetArray(); }
	
	const TParticleView<FPBDRigidParticles>& GetDirtyParticlesView() const { UpdateIfNeeded(); return DirtyParticlesView; }
	TParticleView<FPBDRigidParticles>& GetDirtyParticlesView() { UpdateIfNeeded(); return DirtyParticlesView; }

	const TParticleView<FGeometryParticles>& GetAllParticlesView() const { UpdateIfNeeded(); return AllParticlesView; }


	const TParticleView<FKinematicGeometryParticles>& GetActiveKinematicParticlesView() const { return ActiveKinematicParticlesView; }
	TParticleView<FKinematicGeometryParticles>& GetActiveKinematicParticlesView() { return ActiveKinematicParticlesView; }

	const TParticleView<FGeometryParticles>& GetActiveStaticParticlesView() const { return ActiveStaticParticlesView; }
	TParticleView<FGeometryParticles>& GetActiveStaticParticlesView() { return ActiveStaticParticlesView; }

	const TGeometryParticleHandles<FReal, 3>& GetParticleHandles() const { return ParticleHandles; }
	TGeometryParticleHandles<FReal, 3>& GetParticleHandles() { return ParticleHandles; }

	const FPBDRigidParticles& GetDynamicParticles() const { return *DynamicParticles; }
	FPBDRigidParticles& GetDynamicParticles() { return *DynamicParticles; }

	const FGeometryParticles& GetNonDisabledStaticParticles() const { return *StaticParticles; }
	FGeometryParticles& GetNonDisabledStaticParticles() { return *StaticParticles; }

	const TPBDGeometryCollectionParticles<FReal, 3>& GetGeometryCollectionParticles() const { return *GeometryCollectionParticles; }
	TPBDGeometryCollectionParticles<FReal, 3>& GetGeometryCollectionParticles() { return *GeometryCollectionParticles; }

	void UpdateIfNeeded() const 
	{
		if (bGeometryCollectionDirty) 
		{
			FPBDRigidsSOAs* NCThis = const_cast<FPBDRigidsSOAs*>(this);
			NCThis->UpdateGeometryCollectionViews();
		}
	}

	/**
	 * Update which particle arrays geometry collection particles are in based on 
	 * their object state (static, kinematic, dynamic, sleeping) and their disabled 
	 * state.
	 *
	 * The reason for specializing this function for geometry collections is for 
	 * scalability.  That is, we try to process many geometry collection particles
	 * at a time, rather than one by one.
	 */
	void UpdateGeometryCollectionViews(const bool ForceUpdateViews=false)
	{
		if (!GeometryCollectionParticles)
		{
			if (ForceUpdateViews)
				UpdateViews();
			return;
		}

		int32 StaticIdx = 0, KinematicIdx = 0, SleepingIdx = 0, DynamicIdx = 0;
		int32 ActiveIdx = 0, DisabledIdx = 0;

		for(int32 PIdx = 0; PIdx < (int32)GeometryCollectionParticles->Size(); PIdx++)
		{
			const TPBDGeometryCollectionParticleHandle<FReal, 3>* Handle = 
				GeometryCollectionParticles->Handle(PIdx);
			if (!Handle)
				continue;

			const bool bDisabled = Handle->Disabled();
			if (bDisabled)
				continue;

			// Count the number of particles in each state.
			const Chaos::EObjectStateType State = 
				Handle->Sleeping() ? Chaos::EObjectStateType::Sleeping : Handle->ObjectState();
			switch (State)
			{
			case Chaos::EObjectStateType::Static:
				StaticIdx++;
				break;
			case Chaos::EObjectStateType::Kinematic:
				KinematicIdx++;
				break;
			case Chaos::EObjectStateType::Sleeping:
				SleepingIdx++;
				break;
			case Chaos::EObjectStateType::Dynamic:
				DynamicIdx++;
				break;
			default:
				break;
			};
		}

		// Compare with the previous array sizes, and resize if needed.
		bool Changed =
			StaticGeometryCollectionArray.Num() != StaticIdx ||
			KinematicGeometryCollectionArray.Num() != KinematicIdx ||
			SleepingGeometryCollectionArray.Num() != SleepingIdx ||
			DynamicGeometryCollectionArray.Num() != DynamicIdx;
		if (Changed)
		{
			StaticGeometryCollectionArray.SetNumUninitialized(StaticIdx);
			KinematicGeometryCollectionArray.SetNumUninitialized(KinematicIdx);
			SleepingGeometryCollectionArray.SetNumUninitialized(SleepingIdx);
			DynamicGeometryCollectionArray.SetNumUninitialized(DynamicIdx);
		}

		// (Re)populate the arrays, making note if any prior entires differ from the current.
		StaticIdx = KinematicIdx = SleepingIdx = DynamicIdx = 0;
		for(int32 PIdx = 0; PIdx < (int32)GeometryCollectionParticles->Size(); PIdx++)
		{
			TPBDGeometryCollectionParticleHandle<FReal, 3>* Handle = 
				GeometryCollectionParticles->Handle(PIdx);
			if (!Handle)
				continue;

			const bool bDisabled = Handle->Disabled();
			if (bDisabled)
				continue;

			const Chaos::EObjectStateType State = 
				Handle->Sleeping() ? Chaos::EObjectStateType::Sleeping : Handle->ObjectState();
			switch (State)
			{
			case Chaos::EObjectStateType::Static:
				Changed |= StaticGeometryCollectionArray[StaticIdx] != Handle;
				StaticGeometryCollectionArray[StaticIdx++] = Handle;
				break;
			case Chaos::EObjectStateType::Kinematic:
				Changed |= KinematicGeometryCollectionArray[KinematicIdx] != Handle;
				KinematicGeometryCollectionArray[KinematicIdx++] = Handle;
				break;
			case Chaos::EObjectStateType::Sleeping:
				Changed |= SleepingGeometryCollectionArray[SleepingIdx] != Handle;
				SleepingGeometryCollectionArray[SleepingIdx++] = Handle;
				break;
			case Chaos::EObjectStateType::Dynamic:
				Changed |= DynamicGeometryCollectionArray[DynamicIdx] != Handle;
				DynamicGeometryCollectionArray[DynamicIdx++] = Handle;
				break;
			default:
				break;
			};
		}

		if(Changed || ForceUpdateViews)
		{
			UpdateViews();
		}
		bGeometryCollectionDirty = false;
	}

	const auto& GetClusteredParticles() const { return *ClusteredParticles; }
	auto& GetClusteredParticles() { return *ClusteredParticles; }

	auto& GetUniqueIndices() { return UniqueIndices; }

private:
	template <typename TParticleHandleType, typename TParticles>
	TArray<TParticleHandleType*> CreateParticlesHelper(int32 NumParticles, const FUniqueIdx* ExistingIndices,  TUniquePtr<TParticles>& Particles, const FGeometryParticleParameters& Params)
	{
		const int32 ParticlesStartIdx = Particles->Size();
		Particles->AddParticles(NumParticles);
		TArray<TParticleHandleType*> ReturnHandles;
		ReturnHandles.AddUninitialized(NumParticles);

		const int32 HandlesStartIdx = ParticleHandles.Size();
		ParticleHandles.AddHandles(NumParticles);

		for (int32 Count = 0; Count < NumParticles; ++Count)
		{
			const int32 ParticleIdx = Count + ParticlesStartIdx;
			const int32 HandleIdx = Count + HandlesStartIdx;

			TUniquePtr<TParticleHandleType> NewParticleHandle = TParticleHandleType::CreateParticleHandle(MakeSerializable(Particles), ParticleIdx, HandleIdx);
			NewParticleHandle->ParticleID().LocalID = BiggestParticleID++;
			ReturnHandles[Count] = NewParticleHandle.Get();
			//If unique indices are null it means there is no GT particle that already registered an ID, so create one
			if(ExistingIndices)
			{
				ReturnHandles[Count]->SetUniqueIdx(ExistingIndices[Count]);
			}
			else
			{
				ReturnHandles[Count]->SetUniqueIdx(UniqueIndices.GenerateUniqueIdx());
			}
			ParticleHandles.Handle(HandleIdx) = MoveTemp(NewParticleHandle);
			Particles->HasCollision(ParticleIdx) = true;	//todo: find a better place for this
		}

		return ReturnHandles;
	}
	
	void AddToActiveArray(const TArray<FPBDRigidParticleHandle*>& Particles)
	{
		ActiveParticlesMapArray.Insert(Particles);
		
		//dirty contains Active so make sure no duplicates
		for(FPBDRigidParticleHandle* Particle : Particles)
		{
			TransientDirtyMapArray.Remove(Particle);
		}
	}

	void AddToActiveArray(FPBDRigidParticleHandle* Particle)
	{
		ActiveParticlesMapArray.Insert(Particle);

		//dirty contains Active so make sure no duplicates
		TransientDirtyMapArray.Remove(Particle);
	}

	void RemoveFromActiveArray(FPBDRigidParticleHandle* Particle, bool bStillDirty)
	{
		ActiveParticlesMapArray.Remove(Particle);

		if(bStillDirty)
		{
			//no longer active, but still dirty
			ActiveParticlesMapArray.Insert(Particle);
		}
		else
		{
			//might have already been removed from active from a previous call
			//but now removing and don't want it dirty either
			TransientDirtyMapArray.Remove(Particle);
		}
	}
	
	//should be called whenever particles are added / removed / reordered
	void UpdateViews()
	{
		//build various views. Group SOA types together for better branch prediction
		{
			TArray<TSOAView<FGeometryParticles>> TmpArray = 
			{ 
				StaticParticles.Get(), 
				KinematicParticles.Get(), 
				DynamicParticles.Get(),
				DynamicKinematicParticles.Get(),
				{&StaticClusteredMapArray.GetArray()},
				{&KinematicClusteredMapArray.GetArray()},
				{&DynamicClusteredMapArray.GetArray()},
				{&StaticGeometryCollectionArray},
				{&KinematicGeometryCollectionArray},
				{&SleepingGeometryCollectionArray},
				{&DynamicGeometryCollectionArray}
			};
			NonDisabledView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<FPBDRigidParticles>> TmpArray = 
			{ 
				DynamicParticles.Get(), 
				{&DynamicClusteredMapArray.GetArray()},
				{&SleepingGeometryCollectionArray},
				{&DynamicGeometryCollectionArray}
			};
			NonDisabledDynamicView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<FPBDRigidParticles>> TmpArray = 
			{ 
				{&ActiveParticlesMapArray.GetArray()},
			//	{&DynamicClusteredMapArray.GetArray()},  Cluster particles appear in the ActiveParticlesArray
				{&StaticGeometryCollectionArray},
				{&KinematicGeometryCollectionArray},
				{&DynamicGeometryCollectionArray}
			};
			ActiveParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<FPBDRigidParticles>> TmpArray =
			{
				{&ActiveParticlesMapArray.GetArray()},
				//	{&DynamicClusteredMapArray.GetArray()},  Cluster particles appear in the ActiveParticlesArray
				{&StaticGeometryCollectionArray},
				{&KinematicGeometryCollectionArray},
				{&DynamicGeometryCollectionArray},
				{&TransientDirtyMapArray.GetArray()}
			};
			DirtyParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<FGeometryParticles>> TmpArray = 
			{ 
				StaticParticles.Get(), 
				StaticDisabledParticles.Get(), 
				KinematicParticles.Get(), 
				KinematicDisabledParticles.Get(),
				DynamicParticles.Get(), 
				DynamicDisabledParticles.Get(), 
				DynamicKinematicParticles.Get(), 
				ClusteredParticles.Get(), 
				GeometryCollectionParticles.Get() 
			};
			AllParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<FKinematicGeometryParticles>> TmpArray = 
			{ 
				KinematicParticles.Get(),
				DynamicKinematicParticles.Get(),
				{&KinematicGeometryCollectionArray},
				{&KinematicClusteredMapArray.GetArray()}
			};
			ActiveKinematicParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<FGeometryParticles>> TmpArray =
			{
				StaticParticles.Get(),
				{&StaticClusteredMapArray.GetArray()}
			};
			ActiveStaticParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TPBDGeometryCollectionParticles<FReal, 3>>> TmpArray = 
			{ 
				{&StaticGeometryCollectionArray},
				{&KinematicGeometryCollectionArray},
				{&DynamicGeometryCollectionArray}
			};
			ActiveGeometryCollectionParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}

		{
			TArray<TSOAView<FPBDRigidClusteredParticles>> TmpArray =
			{
				{&StaticClusteredMapArray.GetArray()},
				{&KinematicClusteredMapArray.GetArray()},
				{&DynamicClusteredMapArray.GetArray()}
			};
			NonDisabledClusteredView = MakeParticleView(MoveTemp(TmpArray));
		}
		
	}

	void InsertClusteredParticle(FPBDRigidClusteredParticleHandle* ClusteredParticle)
	{
		if (!ClusteredParticle->Disabled())
		{
			switch (ClusteredParticle->ObjectState())
			{
			case EObjectStateType::Uninitialized:
				ensure(false); // we should probably not be here 
				break;
			case EObjectStateType::Static:
				StaticClusteredMapArray.Insert(ClusteredParticle);
				break;
			case EObjectStateType::Kinematic:
				KinematicClusteredMapArray.Insert(ClusteredParticle);
				break;
			case EObjectStateType::Dynamic:
			case EObjectStateType::Sleeping:
				DynamicClusteredMapArray.Insert(ClusteredParticle);
				break;
			}
		}
	}

	void InsertClusteredParticles(const TArray<FPBDRigidClusteredParticleHandle*>& ClusteredParticleArray)
	{
		for (FPBDRigidClusteredParticleHandle* ClusteredParticle : ClusteredParticleArray)
		{
			InsertClusteredParticle(ClusteredParticle);
		}
	}

	void RemoveClusteredParticle(FPBDRigidClusteredParticleHandle* ClusteredParticle)
	{
		StaticClusteredMapArray.Remove(ClusteredParticle);
		KinematicClusteredMapArray.Remove(ClusteredParticle);
		DynamicClusteredMapArray.Remove(ClusteredParticle);
	}

	//Organized by SOA type
	TUniquePtr<FGeometryParticles> StaticParticles;
	TUniquePtr<FGeometryParticles> StaticDisabledParticles;

	TUniquePtr<FKinematicGeometryParticles> KinematicParticles;
	TUniquePtr<FKinematicGeometryParticles> KinematicDisabledParticles;

	TUniquePtr<FPBDRigidParticles> DynamicParticles;
	TUniquePtr<FPBDRigidParticles> DynamicKinematicParticles;
	TUniquePtr<FPBDRigidParticles> DynamicDisabledParticles;

	TUniquePtr<FPBDRigidClusteredParticles> ClusteredParticles;

	TUniquePtr<TPBDGeometryCollectionParticles<FReal, 3>> GeometryCollectionParticles;

	// Geometry collection particle state is controlled via their disabled state and assigned 
	// EObjectStateType, and are shuffled into these corresponding arrays in UpdateGeometryCollectionViews().
	TArray<TPBDGeometryCollectionParticleHandle<FReal, 3>*> StaticGeometryCollectionArray;
	TArray<TPBDGeometryCollectionParticleHandle<FReal, 3>*> KinematicGeometryCollectionArray;
	TArray<TPBDGeometryCollectionParticleHandle<FReal, 3>*> SleepingGeometryCollectionArray;
	TArray<TPBDGeometryCollectionParticleHandle<FReal, 3>*> DynamicGeometryCollectionArray;
	bool bGeometryCollectionDirty;

	//Utility structures for maintaining an Active particles view
	TParticleMapArray<FPBDRigidParticleHandle> ActiveParticlesMapArray;
	TParticleMapArray<FPBDRigidParticleHandle> TransientDirtyMapArray;

	// NonDisabled clustered particle arrays
	TParticleMapArray<FPBDRigidClusteredParticleHandle> StaticClusteredMapArray;
	TParticleMapArray<FPBDRigidClusteredParticleHandle> KinematicClusteredMapArray;
	TParticleMapArray<FPBDRigidClusteredParticleHandle> DynamicClusteredMapArray;

	//Particle Views
	TParticleView<FGeometryParticles> NonDisabledView;							//all particles that are not disabled
	TParticleView<FPBDRigidParticles> NonDisabledDynamicView;						//all dynamic particles that are not disabled
	TParticleView<FPBDRigidClusteredParticles> NonDisabledClusteredView;			//all clustered particles that are not disabled
	TParticleView<FPBDRigidParticles> ActiveParticlesView;						//all particles that are active
	TParticleView<FPBDRigidParticles> DirtyParticlesView;							//all particles that are active + any that were put to sleep this frame
	TParticleView<FGeometryParticles> AllParticlesView;							//all particles
	TParticleView<FKinematicGeometryParticles> ActiveKinematicParticlesView;		//all kinematic particles that are not disabled
	TParticleView<FGeometryParticles> ActiveStaticParticlesView;					//all static particles that are not disabled
	TParticleView<TPBDGeometryCollectionParticles<FReal, 3>> ActiveGeometryCollectionParticlesView; // all geom collection particles that are not disabled

	//Auxiliary data synced with particle handles
	TGeometryParticleHandles<FReal, 3> ParticleHandles;

	FParticleUniqueIndices UniqueIndices;

#if CHAOS_DETERMINISTIC
	int32 BiggestParticleID;
#endif
};

template <typename T, int d>
using TPBDRigidsSOAs UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDRigidsSOAs instead") = FPBDRigidsSOAs;

}
