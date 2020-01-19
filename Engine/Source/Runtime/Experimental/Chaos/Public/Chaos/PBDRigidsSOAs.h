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
		auto Results =  CreateParticlesHelper<TGeometryParticleHandle<T, d>>(NumParticles, ExistingIndices, Params.bDisabled ? StaticDisabledParticles : StaticParticles, Params);
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
		//TArray<TPBDRigidParticleHandle<T, d>*>& RigidHandles = (TArray<TPBDRigidParticleHandle<T, d>*>*)&Results;//*static_cast<TArray<TPBDRigidParticleHandle<T, d>*>*>(&Results);
		if (!Params.bStartSleeping)
		{
			InsertToMapAndArray(Results, ActiveGeometryCollectionToIndex, ActiveGeometryCollectionArray);
		}
		UpdateGeometryCollectionViews();
		UpdateViews();
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
			for (int32 Idx = 0; Idx < SleepData.Num(); ++Idx)
			{
				if (SleepData[Idx].Particle == Particle)
				{
					SleepData.RemoveAtSwap(Idx);
					break;
				}
			}
			GetDynamicParticles().GetSleepDataLock().WriteUnlock();
		}

		//NOTE: This assumes that we are never creating a PT particle that is replicated to GT
		//At the moment that is true, and it seems like we have enough mechanisms to avoid this direction
		//If we want to support that, the UniqueIndex must be kept around until GT goes away
		//This is hard to do, but would probably mean the ownership of the index is in the proxy
		UniqueIndices.ReleaseIdx(Particle->UniqueIdx());
		ParticleHandles.DestroyHandleSwap(Particle);
		
		UpdateViews();
	}

	void DisableParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		if (auto PBDRigid = Particle->CastToRigidParticle())
		{
			PBDRigid->Disabled() = true;
			PBDRigid->V() = TVector<T, d>(0);
			PBDRigid->W() = TVector<T, d>(0);

			if (auto PBDRigidClustered = Particle->CastToClustered())
			{
				RemoveFromMapAndArray(PBDRigidClustered, NonDisabledClusteredToIndex, NonDisabledClusteredArray);
				RemoveFromMapAndArray(PBDRigidClustered, ActiveClusteredToIndex, ActiveClusteredArray);
			}
			else
			{
				Particle->MoveToSOA(*DynamicDisabledParticles);
			}

			if (Particle->ObjectState() == EObjectStateType::Dynamic)
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
		if (auto PBDRigid = Particle->CastToRigidParticle())
		{
			if (auto PBDRigidClustered = Particle->CastToClustered())
			{
				InsertToMapAndArray(PBDRigidClustered, NonDisabledClusteredToIndex, NonDisabledClusteredArray);
				if (!PBDRigid->Sleeping() && Particle->ObjectState() == EObjectStateType::Dynamic)
				{
					InsertToMapAndArray(PBDRigidClustered, ActiveClusteredToIndex, ActiveClusteredArray);
				}
			}
			else
			{
				SetDynamicParticleSOA(Particle->CastToRigidParticle());
			}

			if (!PBDRigid->Sleeping() && Particle->ObjectState() == EObjectStateType::Dynamic)
			{
				InsertToMapAndArray(PBDRigid, ActiveParticlesToIndex, ActiveParticlesArray);
			}

			PBDRigid->Disabled() = false;
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

	void ActivateParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		auto PBDRigid = Particle->CastToRigidParticle();
		if(PBDRigid && PBDRigid->ObjectState() == EObjectStateType::Dynamic)
		{
			check(!PBDRigid->Disabled());
			if (auto PBDRigidClustered = Particle->CastToClustered())
			{
				InsertToMapAndArray(PBDRigidClustered, ActiveClusteredToIndex, ActiveClusteredArray);
			}

			InsertToMapAndArray(PBDRigid, ActiveParticlesToIndex, ActiveParticlesArray);
		}
		
		UpdateViews();
	}

	void DeactivateParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		auto PBDRigid = Particle->CastToRigidParticle();
		if(PBDRigid)
		{
			if (   PBDRigid->ObjectState() == EObjectStateType::Dynamic
				|| PBDRigid->ObjectState() == EObjectStateType::Sleeping)
			{
				check(!PBDRigid->Disabled());
				if (auto PBDRigidClustered = Particle->CastToClustered())
				{
					RemoveFromMapAndArray(PBDRigidClustered, ActiveClusteredToIndex, ActiveClusteredArray);
				}

				RemoveFromMapAndArray(PBDRigid, ActiveParticlesToIndex, ActiveParticlesArray);
			}
		}

		UpdateViews();
	}

	void DeactivateParticles(const TArray<TGeometryParticleHandle<T, d>*>& Particles)
	{
		for (auto Particle : Particles)
		{
			DeactivateParticle(Particle);
		}
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
		}

		ensure(ClusteredParticles->Size() == 0);	//not supported yet
		//Ar << ClusteredParticles;
		Ar << GeometryCollectionParticles;

		SerializeMapAndArray(Ar, ActiveParticlesToIndex, ActiveParticlesArray);
		//SerializeMapAndArray(Ar, ActiveClusteredToIndex, ActiveClusteredArray);
		//SerializeMapAndArray(Ar, NonDisabledClusteredToIndex, NonDisabledClusteredArray);

		//todo: update deterministic ID

		UpdateViews();
	}


	const TParticleView<TGeometryParticles<T, d>>& GetNonDisabledView() const { return NonDisabledView; }

	const TParticleView<TPBDRigidParticles<T, d>>& GetNonDisabledDynamicView() const { return NonDisabledDynamicView; }

	const TParticleView<TPBDRigidParticles<T, d>>& GetActiveParticlesView() const { return ActiveParticlesView; }
	TParticleView<TPBDRigidParticles<T, d>>& GetActiveParticlesView() { return ActiveParticlesView; }

	const TParticleView<TGeometryParticles<T, d>>& GetAllParticlesView() const { return AllParticlesView; }

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

	const TParticleView<TPBDGeometryCollectionParticles<T, d>>& GetActiveGeometryCollectionParticlesView() const { return ActiveGeometryCollectionParticlesView; }
	TParticleView<TPBDGeometryCollectionParticles<T, d>>& GetActiveGeometryCollectionParticlesView() { return ActiveGeometryCollectionParticlesView; }

	/**
	 * Update which particle arrays geometry collection particles are in based on 
	 * their object state (static, kinematic, dynamic, sleeping) and their disabled 
	 * state.
	 */
	void UpdateGeometryCollectionViews()
	{
		int32 AIdx = 0, SIdx = 0, KIdx = 0, DIdx = 0;

		for (TPBDGeometryCollectionParticleHandle<T, d>* Handle : ActiveGeometryCollectionArray)
		{
			// If the particle is disabled we treat it as static, but for no reason 
			// other than immediate convenience.
			const Chaos::EObjectStateType State = Handle->Disabled() ? Chaos::EObjectStateType::Static : Handle->ObjectState();

			switch (State)
			{
			case Chaos::EObjectStateType::Static:
				SIdx++;
				AIdx += (int32)(!Handle->Disabled());
				break;

			case Chaos::EObjectStateType::Kinematic:
				KIdx++;
				AIdx += (int32)(!Handle->Disabled());
				break;

			case Chaos::EObjectStateType::Sleeping: // Sleeping is a modified dynamic state
				DIdx++;
				break;

			case Chaos::EObjectStateType::Dynamic:
				DIdx++;
				AIdx += (int32)(!Handle->Disabled());
				break;

			default:
				break;
			};
		}

		bool Changed = 
			ActiveGeometryCollectionArray.Num() != AIdx ||
			StaticGeometryCollectionArray.Num() != SIdx || 
			KinematicGeometryCollectionArray.Num() != KIdx || 
			DynamicGeometryCollectionArray.Num() != DIdx;

		if (Changed)
		{
			ActiveGeometryCollectionArray.SetNumUninitialized(AIdx);
			StaticGeometryCollectionArray.SetNumUninitialized(SIdx);
			KinematicGeometryCollectionArray.SetNumUninitialized(KIdx);
			DynamicGeometryCollectionArray.SetNumUninitialized(DIdx);
		}

		AIdx = SIdx = KIdx = DIdx = 0;

		for (TPBDGeometryCollectionParticleHandle<T, d>* Handle : ActiveGeometryCollectionArray)
		{
			const Chaos::EObjectStateType State = 
				Handle->Disabled() ? Chaos::EObjectStateType::Static : Handle->ObjectState();
			switch (State)
			{
			case Chaos::EObjectStateType::Static:
				Changed |= StaticGeometryCollectionArray[SIdx] != Handle;
				StaticGeometryCollectionArray[SIdx++] = Handle;
				if (!Handle->Disabled())
				{
					Changed |= ActiveGeometryCollectionArray[AIdx] != Handle;
					ActiveGeometryCollectionArray[AIdx++] = Handle;
				}
				break;

			case Chaos::EObjectStateType::Kinematic:
				Changed |= KinematicGeometryCollectionArray[KIdx] != Handle;
				KinematicGeometryCollectionArray[KIdx++] = Handle;
				if (!Handle->Disabled())
				{
					Changed |= ActiveGeometryCollectionArray[AIdx] != Handle;
					ActiveGeometryCollectionArray[AIdx++] = Handle;
				}
				break;

			case Chaos::EObjectStateType::Sleeping: // Sleeping is a modified dynamic state
				Changed |= DynamicGeometryCollectionArray[DIdx] != Handle;
				DynamicGeometryCollectionArray[DIdx++] = Handle;
				break;

			case Chaos::EObjectStateType::Dynamic:
				Changed |= DynamicGeometryCollectionArray[DIdx] != Handle;
				DynamicGeometryCollectionArray[DIdx++] = Handle;
				if (!Handle->Disabled())
				{
					Changed |= ActiveGeometryCollectionArray[AIdx] != Handle;
					ActiveGeometryCollectionArray[AIdx++] = Handle;
				}
				break;

			default:
				break;
			};
		}

		if(Changed)
		{
			UpdateViews();
		}
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
		// TODO: Compile time check ensuring TParticle2 is derived from TParticle1?
		int32 NextIdx = ParticleArray.Num();
		for (auto Particle : ParticlesToInsert)
		{
			ParticleToIndex.Add(Particle, NextIdx++);
		}
		ParticleArray.Append(ParticlesToInsert);
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
				{&DynamicGeometryCollectionArray}
			};
			NonDisabledView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TPBDRigidParticles<T, d>>> TmpArray = 
			{ 
				DynamicParticles.Get(), 
				{&NonDisabledClusteredArray}, 
				{&DynamicGeometryCollectionArray}
			};
			NonDisabledDynamicView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TPBDRigidParticles<T, d>>> TmpArray = 
			{ 
				{&ActiveParticlesArray},
				{&ActiveGeometryCollectionArray}
			};
			ActiveParticlesView = MakeParticleView(MoveTemp(TmpArray));
		}
		{
			TArray<TSOAView<TGeometryParticles<T, d>>> TmpArray = { StaticParticles.Get(), StaticDisabledParticles.Get(), KinematicParticles.Get(), KinematicDisabledParticles.Get(),
				DynamicParticles.Get(), DynamicDisabledParticles.Get(), DynamicKinematicParticles.Get(), ClusteredParticles.Get(), GeometryCollectionParticles.Get() };
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
			TArray<TSOAView<TPBDGeometryCollectionParticles<T, d>>> TmpArray = { {&ActiveGeometryCollectionArray} };
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

	TMap<TPBDGeometryCollectionParticleHandle<T, d>*, int32> ActiveGeometryCollectionToIndex;
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> ActiveGeometryCollectionArray;

	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> StaticGeometryCollectionArray;
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> KinematicGeometryCollectionArray;
	TArray<TPBDGeometryCollectionParticleHandle<T, d>*> DynamicGeometryCollectionArray;

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
