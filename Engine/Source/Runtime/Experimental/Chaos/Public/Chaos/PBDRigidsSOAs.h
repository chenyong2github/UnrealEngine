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

template <typename T, int d>
class TPBDRigidsSOAs
{
public:
	TPBDRigidsSOAs()
	{
#if CHAOS_DETERMINISTIC
		BiggestParticleID = 0;
#endif

		StaticParticles = MakeUnique<TGeometryParticles<T, d>>();
		StaticDisabledParticles = MakeUnique <TGeometryParticles<T, d>>();

		KinematicParticles = MakeUnique < TKinematicGeometryParticles<T, d>>();
		KinematicDisabledParticles = MakeUnique < TKinematicGeometryParticles<T, d>>();

		DynamicDisabledParticles = MakeUnique<TPBDRigidParticles<T, d>>();
		DynamicParticles = MakeUnique<TPBDRigidParticles<T, d>>();
		DynamicKinematicParticles = MakeUnique<TPBDRigidParticles<T, d>>();

		ClusteredParticles = MakeUnique< TPBDRigidClusteredParticles<T, d>>();
		ClusteredParticles->RemoveParticleBehavior() = ERemoveParticleBehavior::Remove;	//clustered particles maintain relative ordering

		GeometryCollectionParticles = MakeUnique<TPBDGeometryCollectionParticles<T, d>>();
		GeometryCollectionParticles->RemoveParticleBehavior() = ERemoveParticleBehavior::Remove;	//clustered particles maintain relative ordering
		bGeometryCollectionDirty = false;

		UpdateViews();
	}

	TPBDRigidsSOAs(const TPBDRigidsSOAs<T,d>&) = delete;
	TPBDRigidsSOAs(TPBDRigidsSOAs<T, d>&& Other) = delete;

	void Reset()
	{
		check(0);
	}
	
	TArray<TGeometryParticleHandle<T, d>*> CreateStaticParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr, const TGeometryParticleParameters<T, d>& Params = TGeometryParticleParameters<T, d>())
	{
		auto Results = CreateParticlesHelper<TGeometryParticleHandle<T, d>>(NumParticles, ExistingIndices, Params.bDisabled ? StaticDisabledParticles : StaticParticles, Params);
		UpdateViews();
		return Results;
	}
	TArray<TKinematicGeometryParticleHandle<T, d>*> CreateKinematicParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr,  const TKinematicGeometryParticleParameters<T, d>& Params = TKinematicGeometryParticleParameters<T, d>())
	{
		auto Results = CreateParticlesHelper<TKinematicGeometryParticleHandle<T, d>>(NumParticles, ExistingIndices, Params.bDisabled ? KinematicDisabledParticles : KinematicParticles, Params);
		UpdateViews();
		return Results;
	}
	TArray<TPBDRigidParticleHandle<T, d>*> CreateDynamicParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr,  const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		auto Results = CreateParticlesHelper<TPBDRigidParticleHandle<T, d>>(NumParticles, ExistingIndices, Params.bDisabled ? DynamicDisabledParticles : DynamicParticles, Params);;

		if (!Params.bStartSleeping)
		{
			InsertToMapAndArray(Results, ActiveParticlesToIndex, ActiveParticlesArray);
		}
		UpdateViews();
		return Results;
	}
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> CreateGeometryCollectionParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		TArray<TPBDGeometryCollectionParticleHandle<T, d>*> Results = CreateParticlesHelper<TPBDGeometryCollectionParticleHandle<T, d>>(
			NumParticles, ExistingIndices, GeometryCollectionParticles, Params);
		for (auto* Handle : Results)
		{
			if (Params.bStartSleeping)
			{
				Handle->SetObjectState(Chaos::EObjectStateType::Sleeping);
				Handle->SetSleeping(true);
			}
			else
			{
				Handle->SetObjectState(Chaos::EObjectStateType::Dynamic);
				Handle->SetSleeping(false);
			}
		}
		bGeometryCollectionDirty = true;
		return Results;
	}

	/** Used specifically by PBDRigidClustering. These have special properties for maintaining relative order, efficiently switching from kinematic to dynamic, disable to enable, etc... */
	TArray<TPBDRigidClusteredParticleHandle<T, d>*> CreateClusteredParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr,  const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		auto NewClustered = CreateParticlesHelper<TPBDRigidClusteredParticleHandle<T, d>>(NumParticles, ExistingIndices, ClusteredParticles, Params);
		
		if (!Params.bDisabled)
		{
			InsertToMapAndArray(NewClustered, NonDisabledClusteredToIndex, NonDisabledClusteredArray);
		}

		if (!Params.bStartSleeping)
		{
			InsertToMapAndArray(reinterpret_cast<TArray<TPBDRigidParticleHandle<T,d>*>&>(NewClustered), ActiveParticlesToIndex, ActiveParticlesArray);
			InsertToMapAndArray(NewClustered, ActiveClusteredToIndex, ActiveClusteredArray);
		}

		UpdateViews();
		
		return NewClustered;
	}

	void DestroyParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		check(Particle->CastToClustered() == nullptr);	//not supported

		auto PBDRigid = Particle->CastToRigidParticle();
		if(PBDRigid)
		{
			RemoveFromMapAndArray(PBDRigid, ActiveParticlesToIndex, ActiveParticlesArray);
		}


		if (PBDRigid)
		{
			// Check for sleep events referencing this particle
			// TODO think about this case more
			GetDynamicParticles().GetSleepDataLock().WriteLock();
			auto& SleepData = GetDynamicParticles().GetSleepData();

			SleepData.RemoveAllSwap([Particle](TSleepData<T, d>& Entry) {
				return Entry.Particle == Particle;
			});

			GetDynamicParticles().GetSleepDataLock().WriteUnlock();
		}

		ParticleHandles.DestroyHandleSwap(Particle);
		
		UpdateViews();
	}

	/**
	 * A disabled particle is ignored by the solver.
	 */
	void DisableParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		// Rigid particles express their disabled state with a boolean.
		// Disabled kinematic and static particles get shuffled to differnt SOAs.

		if (auto PBDRigid = Particle->CastToRigidParticle())
		{
			PBDRigid->Disabled() = true;
			PBDRigid->V() = TVector<T, d>(0);
			PBDRigid->W() = TVector<T, d>(0);

			if (auto PBDRigidClustered = Particle->CastToClustered())
			{
				if (Particle->GetParticleType() == Chaos::EParticleType::GeometryCollection)
				{
					bGeometryCollectionDirty = true;
					return;
				}
				else // clustered
				{
					RemoveFromMapAndArray(PBDRigidClustered, 
						NonDisabledClusteredToIndex, NonDisabledClusteredArray);
					if (Particle->ObjectState() == EObjectStateType::Dynamic)
					{
						RemoveFromMapAndArray(PBDRigidClustered, 
							ActiveClusteredToIndex, ActiveClusteredArray);
					}
				}
			}
			else
			{
				Particle->MoveToSOA(*DynamicDisabledParticles);
			}

			// All active particles RIGID particles
			{
				RemoveFromMapAndArray(PBDRigid, ActiveParticlesToIndex, ActiveParticlesArray);
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

	void EnableParticle(TGeometryParticleHandle<T, d>* Particle)
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
					InsertToMapAndArray(PBDRigidClustered, 
						NonDisabledClusteredToIndex, NonDisabledClusteredArray);
					if (!PBDRigid->Sleeping() && Particle->ObjectState() == EObjectStateType::Dynamic)
					{
						// Clustered, enabled, (dynamic, !sleeping)
						InsertToMapAndArray(PBDRigidClustered, 
							ActiveClusteredToIndex, ActiveClusteredArray);
					}
				}
			}
			else
			{
				SetDynamicParticleSOA(PBDRigid);
			}

			if (!PBDRigid->Sleeping() && Particle->ObjectState() == EObjectStateType::Dynamic)
			{
				InsertToMapAndArray(PBDRigid, 
					ActiveParticlesToIndex, ActiveParticlesArray);
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
	 */
	void ActivateParticle(TGeometryParticleHandle<T, d>* Particle)
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
					PBDRigid->SetObjectState(EObjectStateType::Dynamic);
		
					bool bUpdateGeometryCollection = false;
					if (auto PBDRigidClustered = Particle->CastToClustered())
					{
						if (Particle->GetParticleType() == Chaos::EParticleType::GeometryCollection)
						{
							bGeometryCollectionDirty = true;
							return;
						}
						else
						{
							// Clustered, non geometry collection:
							InsertToMapAndArray(PBDRigidClustered, 
								ActiveClusteredToIndex, ActiveClusteredArray);
						}
					}
					else
					{
						// Non clustered rigid particles:
						InsertToMapAndArray(PBDRigid, 
							ActiveParticlesToIndex, ActiveParticlesArray);
					}

					UpdateViews();
				}
			}
		}
	}

	/**
	 * Wake multiple dynamic non-disabled particles.
	 */
	void ActivateParticles(const TArray<TGeometryParticleHandle<T, d>*>& Particles)
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
		TGeometryParticleHandle<T, d>* Particle,
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
					PBDRigid->SetObjectState(EObjectStateType::Sleeping);

					if (auto PBDRigidClustered = Particle->CastToClustered())
					{
						if (Particle->GetParticleType() == Chaos::EParticleType::GeometryCollection)
						{
							bGeometryCollectionDirty = true;
						}
						else
						{
							RemoveFromMapAndArray(PBDRigidClustered, 
								ActiveClusteredToIndex, ActiveClusteredArray);
						}
					}
					else
					{
						RemoveFromMapAndArray(PBDRigid, 
							ActiveParticlesToIndex, ActiveParticlesArray);
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
	void DeactivateParticles(const TArray<TGeometryParticleHandle<T, d>*>& Particles)
	{
		for (auto Particle : Particles)
		{
			DeactivateParticle(Particle, true);
		}
		UpdateIfNeeded();
		UpdateViews();
	}

	void SetDynamicParticleSOA(TPBDRigidParticleHandle<T, d>* Particle)
	{
		const EObjectStateType State = Particle->ObjectState();


		if (Particle->ObjectState() != EObjectStateType::Dynamic)
		{
			RemoveFromMapAndArray(Particle->CastToRigidParticle(), ActiveParticlesToIndex, ActiveParticlesArray);
		}
		else
		{
			InsertToMapAndArray(Particle->CastToRigidParticle(), ActiveParticlesToIndex, ActiveParticlesArray);
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

		SerializeMapAndArray(Ar, ActiveParticlesToIndex, ActiveParticlesArray);
		//SerializeMapAndArray(Ar, ActiveClusteredToIndex, ActiveClusteredArray);
		//SerializeMapAndArray(Ar, NonDisabledClusteredToIndex, NonDisabledClusteredArray);

		//todo: update deterministic ID

		//if (!GeometryCollectionParticles || !GeometryCollectionParticles->Size())
			UpdateViews();
		//else
		//	UpdateGeometryCollectionViews();
	}


	const TParticleView<TGeometryParticles<T, d>>& GetNonDisabledView() const { UpdateIfNeeded();  return NonDisabledView; }

	const TParticleView<TPBDRigidParticles<T, d>>& GetNonDisabledDynamicView() const { UpdateIfNeeded(); return NonDisabledDynamicView; }

	const TParticleView<TPBDRigidParticles<T, d>>& GetActiveParticlesView() const { UpdateIfNeeded(); return ActiveParticlesView; }
	TParticleView<TPBDRigidParticles<T, d>>& GetActiveParticlesView() { UpdateIfNeeded(); return ActiveParticlesView; }

	const TParticleView<TGeometryParticles<T, d>>& GetAllParticlesView() const { UpdateIfNeeded(); return AllParticlesView; }


	const TParticleView<TKinematicGeometryParticles<T, d>>& GetActiveKinematicParticlesView() const { return ActiveKinematicParticlesView; }
	TParticleView<TKinematicGeometryParticles<T, d>>& GetActiveKinematicParticlesView() { return ActiveKinematicParticlesView; }

	const TGeometryParticleHandles<T, d>& GetParticleHandles() const { return ParticleHandles; }
	TGeometryParticleHandles<T, d>& GetParticleHandles() { return ParticleHandles; }

	const TPBDRigidParticles<T, d>& GetDynamicParticles() const { return *DynamicParticles; }
	TPBDRigidParticles<T, d>& GetDynamicParticles() { return *DynamicParticles; }

	const TGeometryParticles<T, d>& GetNonDisabledStaticParticles() const { return *StaticParticles; }
	TGeometryParticles<T, d>& GetNonDisabledStaticParticles() { return *StaticParticles; }

	const TPBDGeometryCollectionParticles<T, d>& GetGeometryCollectionParticles() const { return *GeometryCollectionParticles; }
	TPBDGeometryCollectionParticles<T, d>& GetGeometryCollectionParticles() { return *GeometryCollectionParticles; }

	void UpdateIfNeeded() const 
	{
		if (bGeometryCollectionDirty) 
		{
			TPBDRigidsSOAs<T, d>* NCThis = const_cast<TPBDRigidsSOAs<T, d>*>(this);
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
			const TPBDGeometryCollectionParticleHandle<T, d>* Handle = 
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
			TPBDGeometryCollectionParticleHandle<T, d>* Handle = 
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

	//TEMP: only needed while clustering code continues to use direct indices
	const auto& GetActiveClusteredArray() const { return ActiveClusteredArray; }
	const auto& GetNonDisabledClusteredArray() const { return NonDisabledClusteredArray; }

	const auto& GetClusteredParticles() const { return *ClusteredParticles; }
	auto& GetClusteredParticles() { return *ClusteredParticles; }

	auto& GetUniqueIndices() { return UniqueIndices; }

private:
	template <typename TParticleHandleType, typename TParticles>
	TArray<TParticleHandleType*> CreateParticlesHelper(int32 NumParticles, const FUniqueIdx* ExistingIndices,  TUniquePtr<TParticles>& Particles, const TGeometryParticleParameters<T, d>& Params)
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
#if CHAOS_DETERMINISTIC
			NewParticleHandle->ParticleID() = BiggestParticleID++;
#endif
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
		}

		return ReturnHandles;
	}

	template <typename TParticle1, typename TParticle2>
	void InsertToMapAndArray(const TArray<TParticle1*>& ParticlesToInsert, TMap<TParticle2*, int32>& ParticleToIndex, TArray<TParticle2*>& ParticleArray)
	{
		TArray<bool> Contains;
		Contains.AddZeroed(ParticlesToInsert.Num());

		// TODO: Compile time check ensuring TParticle2 is derived from TParticle1?
		int32 NextIdx = ParticleArray.Num();
		for(int32 Idx = 0; Idx < ParticlesToInsert.Num(); ++Idx)
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

	template <typename TParticle>
	static void InsertToMapAndArray(TParticle* Particle, TMap<TParticle*, int32>& ParticleToIndex, TArray<TParticle*>& ParticleArray)
	{
		if (ParticleToIndex.Contains(Particle) == false)
		{
			ParticleToIndex.Add(Particle, ParticleArray.Num());
			ParticleArray.Add(Particle);
		}
	}

	template <typename TParticle>
	static void RemoveFromMapAndArray(TParticle* Particle, TMap<TParticle*, int32>& ParticleToIndex, TArray<TParticle*>& ParticleArray)
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

	template <typename TParticle>
	void SerializeMapAndArray(FChaosArchive& Ar, TMap<TParticle*, int32>& ParticleToIndex, TArray<TParticle*>& ParticleArray)
	{
		TArray<TSerializablePtr<TParticle>>& SerializableArray = AsAlwaysSerializableArray(ParticleArray);
		Ar << SerializableArray;

		int32 Idx = 0;
		for (auto Particle : ParticleArray)
		{
			ParticleToIndex.Add(Particle, Idx++);
		}
	}
	
	//should be called whenever particles are added / removed / reordered
	void UpdateViews()
	{
		//build various views. Group SOA types together for better branch prediction
		{
			TArray<TSOAView<TGeometryParticles<T, d>>> TmpArray = 
			{ 
				StaticParticles.Get(), 
				KinematicParticles.Get(), 
				DynamicParticles.Get(),
				DynamicKinematicParticles.Get(),
				{&NonDisabledClusteredArray},
				{&StaticGeometryCollectionArray},
				{&KinematicGeometryCollectionArray},
				{&SleepingGeometryCollectionArray},
				{&DynamicGeometryCollectionArray}
			};
			NonDisabledView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TPBDRigidParticles<T, d>>> TmpArray = 
			{ 
				DynamicParticles.Get(), 
				{&NonDisabledClusteredArray}, 
				{&SleepingGeometryCollectionArray},
				{&DynamicGeometryCollectionArray}
			};
			NonDisabledDynamicView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TPBDRigidParticles<T, d>>> TmpArray = 
			{ 
				{&ActiveParticlesArray},
				{&NonDisabledClusteredArray},
				{&StaticGeometryCollectionArray},
				{&KinematicGeometryCollectionArray},
				{&DynamicGeometryCollectionArray}
			};
			ActiveParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TGeometryParticles<T, d>>> TmpArray = 
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
			TArray<TSOAView<TKinematicGeometryParticles<T, d>>> TmpArray = 
			{ 
				KinematicParticles.Get(),
				DynamicKinematicParticles.Get(),
				{&KinematicGeometryCollectionArray}
			};
			ActiveKinematicParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TPBDGeometryCollectionParticles<T, d>>> TmpArray = 
			{ 
				{&StaticGeometryCollectionArray},
				{&KinematicGeometryCollectionArray},
				{&DynamicGeometryCollectionArray}
			};
			ActiveGeometryCollectionParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
	}

	//Organized by SOA type
	TUniquePtr<TGeometryParticles<T, d>> StaticParticles;
	TUniquePtr<TGeometryParticles<T, d>> StaticDisabledParticles;

	TUniquePtr<TKinematicGeometryParticles<T, d>> KinematicParticles;
	TUniquePtr<TKinematicGeometryParticles<T, d>> KinematicDisabledParticles;

	TUniquePtr<TPBDRigidParticles<T, d>> DynamicParticles;
	TUniquePtr<TPBDRigidParticles<T, d>> DynamicKinematicParticles;
	TUniquePtr<TPBDRigidParticles<T, d>> DynamicDisabledParticles;

	TUniquePtr<TPBDRigidClusteredParticles<T, d>> ClusteredParticles;

	TUniquePtr<TPBDGeometryCollectionParticles<T, d>> GeometryCollectionParticles;

	// Geometry collection particle state is controlled via their disabled state and assigned 
	// EObjectStateType, and are shuffled into these corresponding arrays in UpdateGeometryCollectionViews().
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> StaticGeometryCollectionArray;
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> KinematicGeometryCollectionArray;
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> SleepingGeometryCollectionArray;
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> DynamicGeometryCollectionArray;
	bool bGeometryCollectionDirty;

	//Utility structures for maintaining an Active particles view
	TMap<TPBDRigidParticleHandle<T, d>*, int32> ActiveParticlesToIndex;
	TArray<TPBDRigidParticleHandle<T, d>*> ActiveParticlesArray;
	TMap<TPBDRigidClusteredParticleHandle<T, d>*, int32> ActiveClusteredToIndex;
	TArray<TPBDRigidClusteredParticleHandle<T, d>*> ActiveClusteredArray;

	//Utility structures for maintaining a NonDisabled particle view
	TMap<TPBDRigidClusteredParticleHandle<T, d>*, int32> NonDisabledClusteredToIndex;
	TArray<TPBDRigidClusteredParticleHandle<T, d>*> NonDisabledClusteredArray;

	//Particle Views
	TParticleView<TGeometryParticles<T, d>> NonDisabledView;							//all particles that are not disabled
	TParticleView<TPBDRigidParticles<T, d>> NonDisabledDynamicView;						//all dynamic particles that are not disabled
	TParticleView<TPBDRigidParticles<T, d>> ActiveParticlesView;						//all particles that are active
	TParticleView<TGeometryParticles<T, d>> AllParticlesView;							//all particles
	TParticleView<TKinematicGeometryParticles<T, d>> ActiveKinematicParticlesView;		//all kinematic particles that are not disabled
	TParticleView<TPBDGeometryCollectionParticles<T, d>> ActiveGeometryCollectionParticlesView; // all geom collection particles that are not disabled

	//Auxiliary data synced with particle handles
	TGeometryParticleHandles<T, d> ParticleHandles;

	FParticleUniqueIndices UniqueIndices;

#if CHAOS_DETERMINISTIC
	int32 BiggestParticleID;
#endif
};
}
