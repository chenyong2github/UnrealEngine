// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindData.h"
#include "PBDRigidsSolver.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "HAL/IConsoleManager.h"

namespace Chaos
{

int32 EnableResimCache = 1;
FAutoConsoleVariableRef CVarEnableEnableResimCache(TEXT("p.EnableResimCache"), EnableResimCache, TEXT("If enabled, provides a resim cache to speed up certain computations"));

FVec3 FGeometryParticleStateBase::ZeroVector = FVec3(0);

void FGeometryParticleStateBase::SyncSimWritablePropsFromSim(FDirtyPropData Manager,const TPBDRigidParticleHandle<FReal,3>& Rigid)
{
	FParticleDirtyFlags Flags;
	Flags.MarkDirty(EParticleFlags::XR);
	Flags.MarkDirty(EParticleFlags::Velocities);
	Flags.MarkDirty(EParticleFlags::DynamicMisc);
	FParticleDirtyData Dirty;
	Dirty.SetFlags(Flags);

#if 0
	ParticlePositionRotation.SyncRemoteData(Manager,Dirty,[&Rigid](auto& Data)
	{
		Data.CopyFrom(Rigid);
	});

	Velocities.SyncRemoteData(Manager,Dirty,[&Rigid](auto& Data)
	{
		Data.SetV(Rigid.PreV());
		Data.SetW(Rigid.PreW());
	});

	KinematicTarget.SyncRemoteData(Manager, Dirty, [&Rigid](auto& Data)
	{
		Data = Rigid.KinematicTarget();
	});

	DynamicsMisc.SyncRemoteData(Manager, Dirty, [&Rigid](auto& Data)
	{
		Data.CopyFrom(Rigid);
		Data.SetObjectState(Rigid.PreObjectState());	//everything else is not writable by sim so must be the same
	});
#endif
}

void FGeometryParticleStateBase::SyncDirtyDynamics(FDirtyPropData& DestManager,const FParticleDirtyData& Dirty,const FConstDirtyPropData& SrcManager)
{
#if 0
	FParticleDirtyData DirtyFlags;
	DirtyFlags.SetFlags(Dirty.GetFlags());

	Dynamics.SyncRemoteData(DestManager,DirtyFlags,[&Dirty,&SrcManager](auto& Data)
	{
		Data = Dirty.GetDynamics(*SrcManager.Ptr,SrcManager.DataIdx);
	});
#endif
}

bool SimWritablePropsMayChange(const TGeometryParticleHandle<FReal,3>& Handle)
{
	const auto ObjectState = Handle.ObjectState();
	return ObjectState == EObjectStateType::Dynamic || ObjectState == EObjectStateType::Sleeping;
}

template <bool bSkipDynamics>
bool FGeometryParticleStateBase::IsInSync(const FGeometryParticleHandle& Handle, const FFrameAndPhase FrameAndPhase, const FDirtyPropertiesPool& Pool) const
{
	if(!ParticlePositionRotation.IsInSync(Handle, FrameAndPhase, Pool))
	{
		return false;
	}

	if(!NonFrequentData.IsInSync(Handle, FrameAndPhase, Pool))
	{
		return false;
	}

	//todo: deal with state change mismatch

	if(auto Kinematic = Handle.CastToKinematicParticle())
	{
		if(!Velocities.IsInSync(*Kinematic, FrameAndPhase, Pool))
		{
			return false;
		}

		if (!KinematicTarget.IsInSync(*Kinematic, FrameAndPhase, Pool))
		{
			return false;
		}
	}

	if(auto Rigid = Handle.CastToRigidParticle())
	{
		if(!bSkipDynamics)
		{
			if (!Dynamics.IsInSync(*Rigid, FrameAndPhase, Pool))
		{
			return false;
		}
		}

		if(!DynamicsMisc.IsInSync(*Rigid, FrameAndPhase, Pool))
		{
			return false;
		}

		if(!MassProps.IsInSync(*Rigid, FrameAndPhase, Pool))
		{
			return false;
		}
	}

	//TODO: this assumes geometry is never modified. Geometry modification has various issues in higher up Chaos code. Need stable shape id
	//For now iterate over all the shapes in latest and see if they have any mismatches
	/*if(ShapesArrayState.PerShapeData.Num())
	{
		return false;	//if any shapes changed just resim, this is not efficient but at least it's correct
	}*/
	return true;
}

bool FRewindData::RewindToFrame(int32 Frame)
{
	QUICK_SCOPE_CYCLE_COUNTER(RewindToFrame);

	ensure(IsInPhysicsThreadContext());
	//Can't go too far back
	const int32 EarliestFrame = GetEarliestFrame_Internal();
	if(Frame < EarliestFrame)
	{
		return false;
	}

	//If we need to save and we are right on the edge of the buffer, we can't go back to earliest frame
	if(Frame == EarliestFrame && bNeedsSave && FramesSaved == Managers.Capacity())
	{
		return false;
	}

	//If property changed between Frame and CurFrame, record the latest value and rewind to old
	FFrameAndPhase RewindFrameAndPhase{ Frame, FFrameAndPhase::PostPushData };
	FFrameAndPhase CurFrameAndPhase{ CurFrame, FFrameAndPhase::PrePushData };

	//If we're rewinding a particle that doesn't need to save head (resim as slave never checks for desync so we don't care about head)
	auto RewindNoSave = [RewindFrameAndPhase, this](auto Particle, auto& Property, const auto& RewindFunc)
	{
		if (auto Val = Property.Read(RewindFrameAndPhase, PropertiesPool))
	{
			RewindFunc(Particle, *Val);
		}
	};

	//If we're rewinding a particle that needs to save head (during resim when we get back to latest frame and phase we need to check for desync)
	auto RewindAndSave = [RewindFrameAndPhase, CurFrameAndPhase, this](auto Particle, auto& Property, const auto& RewindFunc) -> bool
	{
		if (!Property.IsClean(RewindFrameAndPhase))
		{
			Property.WriteAccessMonotonic(CurFrameAndPhase, PropertiesPool).CopyFrom(*Particle);
			RewindFunc(Particle, *Property.Read(RewindFrameAndPhase, PropertiesPool));

			return true;
		}

		return false;
	};
			
	for(FDirtyParticleInfo& DirtyParticleInfo : AllDirtyParticles)
	{
		FGeometryParticleHandle* PTParticle = DirtyParticleInfo.GetPTParticle();

		//rewind is about to start, all particles should be in sync at this point
		ensure(PTParticle->SyncState() == ESyncState::InSync);

		if(DirtyParticleInfo.bResimAsSlave)
		{
			//simple rewind with all data coming back exactly the same from gt
			//this means we don't need to save head or do anything special
			const FGeometryParticleStateBase& History = DirtyParticleInfo.GetHistory();

			RewindNoSave(PTParticle, History.ParticlePositionRotation, [](auto Particle, const auto& Data) {Particle->SetXR(Data); });
			RewindNoSave(PTParticle, History.NonFrequentData, [](auto Particle, const auto& Data) {Particle->SetNonFrequentData(Data); });
			RewindNoSave(PTParticle->CastToKinematicParticle(), History.Velocities, [](auto Particle, const auto& Data) {Particle->SetVelocities(Data); });
			RewindNoSave(PTParticle->CastToKinematicParticle(), History.KinematicTarget, [](auto Particle, const auto& Data) {Particle->SetKinematicTarget(Data); });
			RewindNoSave(PTParticle->CastToRigidParticle(), History.Dynamics, [](auto Particle, const auto& Data) {Particle->SetDynamics(Data); });
			RewindNoSave(PTParticle->CastToRigidParticle(), History.DynamicsMisc, [Evolution = Solver->GetEvolution()](auto Particle, const auto& Data) {Particle->SetDynamicMisc(Data, *Evolution); });
			RewindNoSave(PTParticle->CastToRigidParticle(), History.MassProps, [](auto Particle, const auto& Data) {Particle->SetMassProps(Data); });
		}
		else
		{
			FGeometryParticleStateBase& History = DirtyParticleInfo.AddFrame(CurFrame);	//non-const in case we need to record what's at head for a rewind (CurFrame has already been increased to the next frame)

			bool bAnyChange = RewindAndSave(PTParticle, History.ParticlePositionRotation, [](auto Particle, const auto& Data) {Particle->SetXR(Data); });
			bAnyChange |= RewindAndSave(PTParticle, History.NonFrequentData, [](auto Particle, const auto& Data) {Particle->SetNonFrequentData(Data); });
			bAnyChange |= RewindAndSave(PTParticle->CastToKinematicParticle(), History.Velocities, [](auto Particle, const auto& Data) {Particle->SetVelocities(Data); });
			bAnyChange |= RewindAndSave(PTParticle->CastToKinematicParticle(), History.KinematicTarget, [](auto Particle, const auto& Data) {Particle->SetKinematicTarget(Data); });
			bAnyChange |= RewindAndSave(PTParticle->CastToRigidParticle(), History.Dynamics, [](auto Particle, const auto& Data) {Particle->SetDynamics(Data); });
			bAnyChange |= RewindAndSave(PTParticle->CastToRigidParticle(), History.DynamicsMisc, [Evolution = Solver->GetEvolution()](auto Particle, const auto& Data) {Particle->SetDynamicMisc(Data, *Evolution); });
			bAnyChange |= RewindAndSave(PTParticle->CastToRigidParticle(), History.MassProps, [](auto Particle, const auto& Data) {Particle->SetMassProps(Data); });

			if (bAnyChange)
			{
				//particle actually changes not just created/streamed so need to update its state

				//Data changes so send back to GT for interpolation. TODO: improve this in case data ends up being identical in resim
				Solver->GetEvolution()->GetParticles().MarkTransientDirtyParticle(DirtyParticleInfo.GetPTParticle());

				DirtyParticleInfo.DirtyDynamics = INDEX_NONE;	//make sure to undo this as we want to record it again during resim
		}

		if (DirtyParticleInfo.InitializedOnStep > Frame)
		{
			//hasn't initialized yet, so disable
			//must do this after rewind because SetDynamicsMisc will re-enable
			//(the disable is a temp way to ignore objects not spawned yet, they weren't really disabled which is why it gets re-enabled)
			Solver->GetEvolution()->DisableParticle(DirtyParticleInfo.GetPTParticle());
		}
	}

	}

	CurFrame = Frame;
	bNeedsSave = false;
	FramesSaved = 0; //can't rewind before this point. This simplifies saving the state at head

	return true;
}

void FRewindData::RemoveParticle(const FUniqueIdx UniqueIdx)
{
	if(const int32* IdxPtr = ParticleToAllDirtyIdx.Find(UniqueIdx))
	{
		const int32 Idx = *IdxPtr;
		AllDirtyParticles.RemoveAtSwap(Idx);
		if(Idx < AllDirtyParticles.Num())
		{
			//update particle in new position
			ParticleToAllDirtyIdx.FindChecked(AllDirtyParticles[Idx].CachedUniqueIdx) = Idx;
		}

		ParticleToAllDirtyIdx.RemoveChecked(UniqueIdx);
	}
}

/* Query the state of particles from the past. Once a rewind happens state captured must be queried using GetFutureStateAtFrame */
FGeometryParticleState FRewindData::GetPastStateAtFrame(const FGeometryParticleHandle& Handle,int32 Frame, FFrameAndPhase::EParticleHistoryPhase Phase) const
{
	ensure(!IsResim());
	ensure(Frame >= GetEarliestFrame_Internal());	//can't get state from before the frame we rewound to

	const FDirtyParticleInfo* Info = FindParticle(Handle.UniqueIdx());
	const FGeometryParticleStateBase* State = Info ? &Info->GetHistory() : nullptr;
	return FGeometryParticleState(State, Handle, PropertiesPool, { Frame, Phase });
}

void FRewindData::FinishFrame()
{
	QUICK_SCOPE_CYCLE_COUNTER(RewindDataFinishFrame);

	if(IsResim())
	{
		FFrameAndPhase FutureFrame{ CurFrame + 1, FFrameAndPhase::PrePushData };

		for(FDirtyParticleInfo& Info : AllDirtyParticles)
		{
			if (Info.bResimAsSlave)
			{
				//resim as slave means always in sync and no cleanup needed
				continue;
			}

			auto& Handle = *Info.GetPTParticle();
			if (auto Rigid = Handle.CastToRigidParticle())
				{
				if(Rigid->ResimType() == EResimType::FullResim)
					{
					if (IsFinalResim())
					{
						//Last resim so mark as in sync
						Handle.SetSyncState(ESyncState::InSync);

						//Anything saved on upcoming frame (was done during rewind) can be removed since we are now at head
						Info.ClearPhaseAndFuture(FutureFrame);
					}
					else
					{
						//solver doesn't affect dynamics, so no reason to test if they desynced from original sim
						//question: should we skip all other properties? dynamics is a commonly changed one but might be worth skipping everything solver skips
						DesyncIfNecessary</*bSkipDynamics=*/true>(Info, FutureFrame);
				}
			}
		}
			}
		}
		

	++CurFrame;
	LatestFrame = FMath::Max(LatestFrame,CurFrame);
}

int32 SkipDesyncTest = 0;
FAutoConsoleVariableRef CVarSkipDesyncTest(TEXT("p.SkipDesyncTest"), SkipDesyncTest, TEXT("Skips hard desync test, this means all particles will assume to be clean except spawning at different times. This is useful for a perf lower bound, not actually correct"));

template <bool bSkipDynamics>
void FRewindData::DesyncIfNecessary(FDirtyParticleInfo& Info, const FFrameAndPhase FrameAndPhase)
{
	ensure(IsResim());	//shouldn't bother with desync unless we're resimming

	auto Handle = Info.GetPTParticle();
	if (Handle->SyncState() == ESyncState::InSync && !Info.GetHistory().IsInSync<bSkipDynamics>(*Handle, FrameAndPhase, PropertiesPool))
	{
		if (!SkipDesyncTest)
		{
			//first time desyncing so need to clear history from this point into the future
			Info.ClearPhaseAndFuture(FrameAndPhase);
			Info.GetPTParticle()->SetSyncState(ESyncState::HardDesync);
		}
	}
}

void FRewindData::AdvanceFrameImp(IResimCacheBase* ResimCache)
{
	FramesSaved = FMath::Min(FramesSaved+1,static_cast<int32>(Managers.Capacity()-1));

	const int32 EarliestFrame = CurFrame - 1 - FramesSaved;
	TArray<FGeometryParticleHandle*> DesyncedParticles;
	if(IsResim())
	{
		DesyncedParticles.Reserve(AllDirtyParticles.Num());
	}

	const FFrameAndPhase FrameAndPhase{ CurFrame, FFrameAndPhase::PostCallbacks };
	for (int32 DirtyIdx = AllDirtyParticles.Num() - 1; DirtyIdx >= 0; --DirtyIdx)
	{
		FDirtyParticleInfo& Info = AllDirtyParticles[DirtyIdx];

		ensure(IsResimAndInSync(*Info.GetPTParticle()) || Info.GetHistory().IsClean(FrameAndPhase));  //Sim hasn't run yet so PostCallbacks (sim results) should be clean

		//if hasn't changed in a while stop tracking
		if (Info.LastDirtyFrame < EarliestFrame)
		{
			RemoveParticle(AllDirtyParticles[DirtyIdx].CachedUniqueIdx);
		}
		else
		{
			if (IsResim() && !Info.bResimAsSlave)
			{
				DesyncIfNecessary(Info, FrameAndPhase);
				}

			auto Handle = Info.GetPTParticle();
			if(IsResim() && Handle->SyncState() != ESyncState::InSync && !SkipDesyncTest)
				{
				DesyncedParticles.Add(Handle);
			}

			if(Info.DirtyDynamics == CurFrame && !IsResimAndInSync(*Handle))
			{
				//we only need to check the cast because right now there's no property system on PT, so any time a sim callback touches a particle we just mark it as dirty dynamics
				if (auto Rigid = Handle->CastToRigidParticle())
			{
					//sim callback is finished so record the dynamics before solve starts
					FGeometryParticleStateBase& Latest = Info.AddFrame(CurFrame);
					Latest.Dynamics.WriteAccessMonotonic(FrameAndPhase, PropertiesPool).CopyFrom(*Rigid);
				}
			}
		}
	}

	if(IsResim() && ResimCache)
	{
		ResimCache->SetDesyncedParticles(MoveTemp(DesyncedParticles));
	}
}

#ifndef REWIND_DESYNC
#define REWIND_DESYNC 0
#endif

template <bool bResim>
void FRewindData::PushGTDirtyData(const FDirtyPropertiesManager& SrcManager,const int32 SrcDataIdx,const FDirtyProxy& Dirty, const FShapeDirtyData* ShapeDirtyData)
{
	//This records changes enqueued by GT.
	bNeedsSave = true;

	if(ensure(Dirty.Proxy->GetType() == EPhysicsProxyType::SingleParticleProxy))
	{
		auto Proxy = static_cast<FSingleParticlePhysicsProxy*>(Dirty.Proxy);
		
		FGeometryParticleHandle* PTParticle = Proxy->GetHandle_LowLevel();

		//Don't bother tracking static particles. We assume they stream in and out and don't need to be rewound
		//TODO: find a way to skip statics that stream in and out - gameplay can technically spawn/destroy these so we can't just ignore statics
		/*if(PTParticle->CastToKinematicParticle() == nullptr)
		{
			return;
		}*/

		//During a resim the same exact push data comes from gt
		//If the particle is already in sync, it will stay in sync so no need to touch history
		if (bResim && PTParticle->SyncState() == ESyncState::InSync)
		{
			return;
		}


		FDirtyParticleInfo& Info = FindOrAddParticle(*PTParticle, Proxy->IsInitialized() ? INDEX_NONE : CurFrame);
		FGeometryParticleStateBase& Latest = Info.AddFrame(CurFrame);

		if(PTParticle->CastToRigidParticle() == nullptr)	//non rigid is always resimming as slave (TODO: we may want to move kinematics from callback)
		{
			Info.bResimAsSlave = true;
		}

		if(auto NewData = Dirty.ParticleData.FindDynamicMisc(SrcManager, SrcDataIdx))
		{
			//question: does modifying this at runtime cause issues? For example a kinematic starting to simulate?
			Info.bResimAsSlave = NewData->ResimType() == EResimType::ResimAsSlave;
		}

		//At this point all phases should be clean
		ensure(Latest.IsClean(FFrameAndPhase{ CurFrame, FFrameAndPhase::PrePushData }));

		//Most particles never change but may be created/destroyed often due to streaming
		//To avoid useless writes we call this function before PushData is processed.
		//This means we will skip particles that are streamed in since they never change
		//So if Proxy has initialized it means the particle isn't just streaming in, it's actually changing
		if(Info.InitializedOnStep < CurFrame)
		{
			const FFrameAndPhase PrePushData = FFrameAndPhase{ CurFrame, FFrameAndPhase::PrePushData };
			auto DirtyPropHelper = [this, &Dirty](auto& Property, const EParticleFlags PropName, const auto& Particle)
			{
				if (Dirty.ParticleData.IsDirty(PropName))
				{
					auto& Data = Property.WriteAccessMonotonic(FFrameAndPhase{ CurFrame, FFrameAndPhase::PrePushData }, PropertiesPool);
					Data.CopyFrom(Particle);
		}
			};

			DirtyPropHelper(Latest.ParticlePositionRotation, EParticleFlags::XR, *PTParticle);
			DirtyPropHelper(Latest.NonFrequentData, EParticleFlags::NonFrequentData, *PTParticle);

			if (auto Kinematic = PTParticle->CastToKinematicParticle())
			{
				DirtyPropHelper(Latest.Velocities, EParticleFlags::Velocities, *Kinematic);
				DirtyPropHelper(Latest.KinematicTarget, EParticleFlags::KinematicTarget, *Kinematic);

				if (auto Rigid = Kinematic->CastToRigidParticle())
				{
					DirtyPropHelper(Latest.DynamicsMisc, EParticleFlags::DynamicMisc, *Rigid);
					DirtyPropHelper(Latest.MassProps, EParticleFlags::MassProps, *Rigid);
				}
			}
		}

		//Dynamics are not available at head (sim zeroes them out), so we have to record them as PostPushData (since they're applied as part of PushData)
		if (auto NewData = Dirty.ParticleData.FindDynamics(SrcManager, SrcDataIdx))
		{
			Latest.Dynamics.WriteAccessMonotonic(FFrameAndPhase{ CurFrame, FFrameAndPhase::PostPushData }, PropertiesPool) = *NewData;
			Info.DirtyDynamics = CurFrame;	//Need to save the dirty dynamics into the next phase as well (it's possible a callback will stomp the dynamics value, so that's why it's pending)
		}

		ensure(Latest.IsCleanExcludingDynamics(FFrameAndPhase{ CurFrame, FFrameAndPhase::PostPushData })); //PostPushData is untouched except for dynamics
		ensure(Latest.IsClean(FFrameAndPhase{ CurFrame, FFrameAndPhase::PostCallbacks }));	//PostCallback should be untouched
	}
}

void FRewindData::SpawnProxyIfNeeded(FSingleParticlePhysicsProxy& Proxy)
{
	if(Proxy.GetInitializedStep() > CurFrame)
	{
		FGeometryParticleHandle* Handle = Proxy.GetHandle_LowLevel();
		FindOrAddParticle(*Handle, CurFrame);

		Solver->GetEvolution()->EnableParticle(Handle, nullptr);
		if(Proxy.GetInitializedStep() != CurFrame)
		{
			Handle->SetSyncState(ESyncState::HardDesync);	//spawn frame changed so desync
			Proxy.SetInitialized(CurFrame);
		}
	}
}

void FRewindData::MarkDirtyFromPT(FGeometryParticleHandle& Handle)
{
	FDirtyParticleInfo& Info = FindOrAddParticle(Handle);

	Info.DirtyDynamics = CurFrame;

	const bool bRecordingHistory = !IsResimAndInSync(Handle);

	FGeometryParticleStateBase& Latest = Info.AddFrame(CurFrame);

	//TODO: use property system
	//For now we just dirty all PT properties that we typically use
	//This means sim callback can't modify mass, geometry, etc... (only properties touched by this function)
	//Note these same properties are sent back to GT, so it's not just this function that needs updating

	const FFrameAndPhase FrameAndPhase{ CurFrame, FFrameAndPhase::PostPushData };

	if(bRecordingHistory || Latest.ParticlePositionRotation.IsClean(FrameAndPhase))
{
		if (auto Data = Latest.ParticlePositionRotation.WriteAccessNonDecreasing(FFrameAndPhase{ CurFrame, FFrameAndPhase::PostPushData }, PropertiesPool))
		{
			Data->CopyFrom(Handle);
		}
}


	if (auto Kinematic = Handle.CastToKinematicParticle())
	{
		if (bRecordingHistory || Latest.Velocities.IsClean(FrameAndPhase))
			{
			if (auto Data = Latest.Velocities.WriteAccessNonDecreasing(FrameAndPhase, PropertiesPool))
{
				Data->CopyFrom(*Kinematic);
		}
	}

		if (auto Rigid = Kinematic->CastToRigidParticle())
	{
			if (bRecordingHistory || Latest.DynamicsMisc.IsClean(FrameAndPhase))
		{
				if (auto Data = Latest.DynamicsMisc.WriteAccessNonDecreasing(FrameAndPhase, PropertiesPool))
			{
					Data->CopyFrom(*Rigid);
				}
			}
			}
		}
	}

template <bool bResim>
void FRewindData::PushPTDirtyData(TPBDRigidParticleHandle<FReal,3>& Handle,const int32 SrcDataIdx)
	{
	const bool bRecordingHistory = !IsResimAndInSync(Handle);

	FDirtyParticleInfo& Info = FindOrAddParticle(Handle);
	FGeometryParticleStateBase& Latest = Info.AddFrame(CurFrame);
	const FFrameAndPhase FrameAndPhase{ CurFrame, FFrameAndPhase::PostCallbacks };

	ensure(!bRecordingHistory || Latest.IsCleanExcludingDynamics(FrameAndPhase));	//PostCallbacks should be clean before we write sim results

	if(bRecordingHistory || Latest.ParticlePositionRotation.IsClean(FrameAndPhase))
		{
		Latest.ParticlePositionRotation.WriteAccessMonotonic(FrameAndPhase, PropertiesPool).CopyFrom(Handle);
			}
			
	if(bRecordingHistory || Latest.Velocities.IsClean(FrameAndPhase))
			{
		FParticleVelocities& PreVelocities = Latest.Velocities.WriteAccessMonotonic(FrameAndPhase, PropertiesPool);
		PreVelocities.SetV(Handle.PreV());
		PreVelocities.SetW(Handle.PreW());
		}

	if(bRecordingHistory || Latest.DynamicsMisc.IsClean(FrameAndPhase))
	{
		FParticleDynamicMisc& PreDynamicMisc = Latest.DynamicsMisc.WriteAccessMonotonic(FrameAndPhase, PropertiesPool);
		PreDynamicMisc.CopyFrom(Handle);	//everything is immutable except object state
		PreDynamicMisc.SetObjectState(Handle.PreObjectState());
	}
}

FRewindData::FDirtyParticleInfo& FRewindData::FindOrAddParticle(TGeometryParticleHandle<FReal,3>& PTParticle, const int32 InitializedOnFrame)
{
	const FUniqueIdx UniqueIdx = PTParticle.UniqueIdx();
	if(FDirtyParticleInfo* Info = FindParticle(UniqueIdx))
	{
		return *Info;
	}

	const int32 DirtyIdx = AllDirtyParticles.Add(FDirtyParticleInfo(PropertiesPool,PTParticle,UniqueIdx,CurFrame,Managers.Capacity()));
	ParticleToAllDirtyIdx.Add(UniqueIdx,DirtyIdx);
	if(InitializedOnFrame != INDEX_NONE)
	{
		AllDirtyParticles[DirtyIdx].InitializedOnStep = InitializedOnFrame;
	}

	return AllDirtyParticles[DirtyIdx];
}

template <bool bResim>
bool FRewindData::FSimWritableState::SyncSimWritablePropsFromSim(const TPBDRigidParticleHandle<FReal,3>& Rigid, const int32 Frame)
{
	FrameRecordedHack = Frame;
	bool bDesynced = false;
	if(bResim)
	{
		bDesynced |= Rigid.P() != MX;
		bDesynced |= Rigid.Q() != MR;
		bDesynced |= Rigid.V() != MV;
		bDesynced |= Rigid.W() != MW;
	}

	MX = Rigid.P();
	MR = Rigid.Q();
	MV = Rigid.V();
	MW = Rigid.W();

	return bDesynced;
}

void FRewindData::FSimWritableState::SyncToParticle(TPBDRigidParticleHandle<FReal,3>& Rigid) const
{
	Rigid.SetX(MX);
	Rigid.SetR(MR);
	Rigid.SetV(MV);
	Rigid.SetW(MW);
}

FRewindData::FDirtyParticleInfo::~FDirtyParticleInfo()
{
	if(PropertiesPool)
	{
		History.Release(*PropertiesPool);
}
}

template void FRewindData::PushGTDirtyData<true>(const FDirtyPropertiesManager& SrcManager,const int32 SrcDataIdx,const FDirtyProxy& Dirty, const FShapeDirtyData* DirtyShapesData);
template void FRewindData::PushGTDirtyData<false>(const FDirtyPropertiesManager& SrcManager,const int32 SrcDataIdx,const FDirtyProxy& Dirty, const FShapeDirtyData* DirtyShapesData);

template void FRewindData::PushPTDirtyData<true>(TPBDRigidParticleHandle<FReal,3>& Rigid,const int32 SrcDataIdx);
template void FRewindData::PushPTDirtyData<false>(TPBDRigidParticleHandle<FReal,3>& Rigid,const int32 SrcDataIdx);

}