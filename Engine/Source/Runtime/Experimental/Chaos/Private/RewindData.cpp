// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindData.h"

namespace Chaos
{

template <typename T,EParticleProperty PropName>
template <typename LambdaWrite>
void TParticleStateProperty<T,PropName>::SyncToParticle(const LambdaWrite& WriteFunc) const
{
	if(Manager.Ptr)
	{
		const TDirtyElementPool<T>& Pool = Manager.Ptr->GetParticlePool<T,PropName>();
		const T& Value = Pool.GetElement(Manager.DataIdx);
		WriteFunc(Value);
	}
}

template <typename T,EParticleProperty PropName>
template <typename LambdaSet>
void TParticleStateProperty<T,PropName>::SyncRemoteDataForced(const FDirtyPropData& InManager,const LambdaSet& SetFunc)
{
	Manager = InManager;
	T& NewVal = Manager.Ptr->GetParticlePool<T,PropName>().GetElement(Manager.DataIdx);
	SetFunc(NewVal);
}

template <typename T,EParticleProperty PropName>
template <typename LambdaSet>
void TParticleStateProperty<T,PropName>::SyncRemoteData(const FDirtyPropData& InManager,const FParticleDirtyData& DirtyData,const LambdaSet& SetFunc)
{
	checkSlow(InManager.Ptr != nullptr);
	if(DirtyData.IsDirty(ParticlePropToFlag(PropName)))
	{
		SyncRemoteDataForced(InManager,SetFunc);
	}
}

template <typename T,EParticleProperty PropName>
template <typename TParticleHandle>
bool TParticleStateProperty<T,PropName>::IsInSync(const FConstDirtyPropData& SrcManager,const FParticleDirtyFlags Flags,const TParticleHandle& Handle) const
{
	const T* RecordedEntry = Manager.Ptr ? &GetValue(Manager.Ptr,Manager.DataIdx) : nullptr;
	const T* NewEntry = Flags.IsDirty(ParticlePropToFlag(PropName)) ? &GetValue(SrcManager.Ptr,SrcManager.DataIdx) : nullptr;

	if(NewEntry)
	{
		if(RecordedEntry)
		{
			//We have an entry from current run and previous run, so check that they are equal
			return NewEntry->IsEqual(*RecordedEntry);
		} else
		{
			//Previous run had no entry. If the current PT data matches the new data, then this is a harmless idnetical write and we are still in sync
			return NewEntry->IsEqual(Handle);
		}
	} else
	{
		if(RecordedEntry)
		{
			//We have an entry from previous run, but not anymore. It's possible this will get written out by PT and hasn't yet, so check if the values are the same
			return RecordedEntry->IsEqual(Handle);
		} else
		{
			//Both current run and recorded run have no entry, so both pointed at head and saw no change
			return true;
		}
	}
}

void FGeometryParticleStateBase::SyncSimWritablePropsFromSim(FDirtyPropData Manager,const TPBDRigidParticleHandle<FReal,3>& Rigid)
{
	FParticleDirtyFlags Flags;
	Flags.MarkDirty(EParticleFlags::XR);
	Flags.MarkDirty(EParticleFlags::Velocities);
	Flags.MarkDirty(EParticleFlags::DynamicMisc);
	FParticleDirtyData Dirty;
	Dirty.SetFlags(Flags);

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
}

void FGeometryParticleStateBase::SyncDirtyDynamics(FDirtyPropData& DestManager,const FParticleDirtyData& Dirty,const FConstDirtyPropData& SrcManager)
{
	FParticleDirtyData DirtyFlags;
	DirtyFlags.SetFlags(Dirty.GetFlags());

	Dynamics.SyncRemoteData(DestManager,DirtyFlags,[&Dirty,&SrcManager](auto& Data)
	{
		Data = Dirty.GetDynamics(*SrcManager.Ptr,SrcManager.DataIdx);
	});
}

bool FGeometryParticleStateBase::IsSimWritableDesynced(TPBDRigidParticleHandle<FReal,3>& Particle) const
{
	if(ParticlePositionRotation.IsSet())
	{
		const FParticlePositionRotation& XR = ParticlePositionRotation.Read();
		if(XR.X() != Particle.X())
		{
			return true;
		}

		if(XR.R() != Particle.R())
		{
			return true;
		}
	}

	if(Velocities.IsSet())
	{
		const FParticleVelocities& Vels = Velocities.Read();
		if(Vels.V() != Particle.V())
		{
			return true;
		}

		if(Vels.W() != Particle.W())
		{
			return true;
		}
	}

	if (KinematicTarget.IsSet())
	{
		const FKinematicTarget& Target = KinematicTarget.Read();
		if (Target == Particle.KinematicTarget())
		{
			return true;
		}
	}

	return false;
}

template <typename TParticle>
void FGeometryParticleStateBase::SyncToParticle(TParticle& Particle) const
{
	ParticlePositionRotation.SyncToParticle([&Particle](const auto& Data)
	{
		Particle.SetXR(Data);
	});

	if(auto Kinematic = Particle.CastToKinematicParticle())
	{
		Velocities.SyncToParticle([Kinematic](const auto& Data)
		{
			Kinematic->SetVelocities(Data);

			// If we changed the velocity, reset the smoothed velocity.
			// This is not strictly correct but should be close. Worst case would 
			// be a delay in sleeping after a rewind.
			if (auto Rigid = Kinematic->CastToRigidParticle())
			{
				Rigid->ResetSmoothedVelocities();
			}
		});

		KinematicTarget.SyncToParticle([Kinematic](const auto& Data)
		{
			Kinematic->SetKinematicTarget(Data);
		});
	}

	NonFrequentData.SyncToParticle([&Particle](const auto& Data)
	{
		Particle.SetNonFrequentData(Data);
	});

	if(auto Rigid = Particle.CastToRigidParticle())
	{
		DynamicsMisc.SyncToParticle([Rigid](const auto& Data)
		{
			Rigid->SetDynamicMisc(Data);
		});

		MassProps.SyncToParticle([Rigid](const auto& Data)
		{
			Rigid->SetMassProps(Data);
		});

		if(Rigid->ResimType() != EResimType::FullResim)
		{
			//Not full resim so apply dynamics automatically
			Dynamics.SyncToParticle([Rigid](const auto& Data)
			{
				Rigid->SetDynamics(Data);
			});
		}
	}
}

bool SimWritablePropsMayChange(const TGeometryParticleHandle<FReal,3>& Handle)
{
	const auto ObjectState = Handle.ObjectState();
	return ObjectState == EObjectStateType::Dynamic || ObjectState == EObjectStateType::Sleeping;
}

void FGeometryParticleStateBase::SyncPrevFrame(FDirtyPropData& Manager,const FDirtyProxy& Dirty)
{
	//syncs the data before it was made dirty
	//for sim-writable props this is only possible if those props are immutable from the sim side (sleeping, not simulated, etc...)

	const auto Proxy = static_cast<const FSingleParticlePhysicsProxy*>(Dirty.Proxy);
	const auto Handle = Proxy->GetHandle_LowLevel();

	const bool bSyncSimWritable = !SimWritablePropsMayChange(*Handle);

	//note: there is a potential bug here if in one frame we put an object to sleep and change its position
	//not sure if that's a valid operation, we probably need to catch sleep/awake and handle it in a special way
	if(bSyncSimWritable)
	{
		ParticlePositionRotation.SyncRemoteData(Manager,Dirty.ParticleData,[Handle](FParticlePositionRotation& Data)
		{
			Data.CopyFrom(*Handle);
		});

		if(auto Kinematic = Handle->CastToKinematicParticle())
		{
			Velocities.SyncRemoteData(Manager,Dirty.ParticleData,[Kinematic](auto& Data)
			{
				Data.CopyFrom(*Kinematic);
			});

			KinematicTarget.SyncRemoteData(Manager, Dirty.ParticleData, [Kinematic](auto& Data)
			{
				Data = Kinematic->KinematicTarget();
			});
		}
	}

	NonFrequentData.SyncRemoteData(Manager,Dirty.ParticleData,[Handle](FParticleNonFrequentData& Data)
	{
		Data.CopyFrom(*Handle);
	});

	if(auto Rigid = Handle->CastToRigidParticle())
	{
		DynamicsMisc.SyncRemoteData(Manager,Dirty.ParticleData,[Rigid](FParticleDynamicMisc& Data)
		{
			Data.CopyFrom(*Rigid);
		});

		MassProps.SyncRemoteData(Manager,Dirty.ParticleData,[Rigid](FParticleMassProps& Data)
		{
			Data.CopyFrom(*Rigid);
		});
	}

}

void FGeometryParticleStateBase::SyncIfDirty(const FDirtyPropData& Manager,const FGeometryParticleHandle& InHandle,const FGeometryParticleStateBase& RewindState)
{
	ensure(IsInPhysicsThreadContext());
	const auto Handle = &InHandle;

	if(RewindState.ParticlePositionRotation.IsSet())
	{
		ParticlePositionRotation.SyncRemoteDataForced(Manager,[Handle](FParticlePositionRotation& Data)
		{
			Data.CopyFrom(*Handle);
		});
	}

	if(const auto Kinematic = Handle->CastToKinematicParticle())
	{
		if(RewindState.Velocities.IsSet())
		{
			Velocities.SyncRemoteDataForced(Manager,[Kinematic](auto& Data)
			{
				Data.CopyFrom(*Kinematic);
			});
		}

		if (RewindState.KinematicTarget.IsSet())
		{
			KinematicTarget.SyncRemoteDataForced(Manager, [Kinematic](auto& Data)
			{
				Data = Kinematic->KinematicTarget();
			});
		}
	}

	if(auto Rigid = Handle->CastToRigidParticle())
	{
		if(RewindState.DynamicsMisc.IsSet())
		{
			DynamicsMisc.SyncRemoteDataForced(Manager,[Rigid](FParticleDynamicMisc& Data)
			{
				Data.CopyFrom(*Rigid);
			});
		}

		if(RewindState.MassProps.IsSet())
		{
			MassProps.SyncRemoteDataForced(Manager,[Rigid](FParticleMassProps& Data)
			{
				Data.CopyFrom(*Rigid);
			});
		}
	}
}

bool FGeometryParticleStateBase::CoalesceState(const FGeometryParticleStateBase& LatestState)
{
	bool bCoalesced = false;
	if(!ParticlePositionRotation.IsSet() && LatestState.ParticlePositionRotation.IsSet())
	{
		ParticlePositionRotation = LatestState.ParticlePositionRotation;
		bCoalesced = true;
	}

	if(!NonFrequentData.IsSet() && LatestState.NonFrequentData.IsSet())
	{
		NonFrequentData = LatestState.NonFrequentData;
		bCoalesced = true;
	}

	if(!Velocities.IsSet() && LatestState.Velocities.IsSet())
	{
		Velocities = LatestState.Velocities;
		bCoalesced = true;
	}

	if (!KinematicTarget.IsSet() && LatestState.KinematicTarget.IsSet())
	{
		KinematicTarget = LatestState.KinematicTarget;
		bCoalesced = true;
	}

	if(!MassProps.IsSet() && LatestState.MassProps.IsSet())
	{
		MassProps = LatestState.MassProps;
		bCoalesced = true;
	}

	if(!DynamicsMisc.IsSet() && LatestState.DynamicsMisc.IsSet())
	{
		DynamicsMisc = LatestState.DynamicsMisc;
		bCoalesced = true;
	}

	//dynamics do not coalesce since they are always written when dirty

	return bCoalesced;
}

bool FGeometryParticleStateBase::IsDesynced(const FConstDirtyPropData& SrcManager,const TGeometryParticleHandle<FReal,3>& Handle,const FParticleDirtyFlags Flags) const
{
	bool Desynced = false;
	{
		if(!ParticlePositionRotation.IsInSync(SrcManager,Flags,Handle))
		{
			return true;
		}

		if(!NonFrequentData.IsInSync(SrcManager,Flags,Handle))
		{
			return true;
		}

		if(auto Kinematic = Handle.CastToKinematicParticle())
		{
			if(!Velocities.IsInSync(SrcManager,Flags,*Kinematic))
			{
				return true;
			}

			if (!KinematicTarget.IsInSync(SrcManager, Flags, *Kinematic))
			{
				return true;
			}
		}

		if(auto Rigid = Handle.CastToRigidParticle())
		{
			if(!Dynamics.IsInSync(SrcManager,Flags,*Rigid))
			{
				return true;
			}

			if(!DynamicsMisc.IsInSync(SrcManager,Flags,*Rigid))
			{
				return true;
			}

			if(!MassProps.IsInSync(SrcManager,Flags,*Rigid))
			{
				return true;
			}
		}
	}

	return false;
}

bool FRewindData::RewindToFrame(int32 Frame)
{
	ensure(IsInPhysicsThreadContext());
	//Can't go too far back
	const int32 EarliestFrame = CurFrame - FramesSaved;
	if(Frame < EarliestFrame)
	{
		return false;
	}

	//If we need to save and we are right on the edge of the buffer, we can't go back to earliest frame
	if(Frame == EarliestFrame && bNeedsSave && FramesSaved == Managers.Capacity())
	{
		return false;
	}

	FDirtyPropertiesManager* DestManager = nullptr;
	if(bNeedsSave)
	{
		PrepareFrame(AllDirtyParticles.Num());
		DestManager = Managers[CurFrame].Manager.Get();
	}

	//todo: parallel for
	int32 DataIdx = 0;
	for(FDirtyParticleInfo& DirtyParticleInfo : AllDirtyParticles)
	{
		DirtyParticleInfo.bDesync = false;	//after rewind particle is pristine
		DirtyParticleInfo.MostDesynced = ESyncState::InSync;

		const auto ObjectState = DirtyParticleInfo.GetPTParticle()->ObjectState();
		//don't sync kinematics
		const bool bAllowSync = ObjectState == EObjectStateType::Sleeping || ObjectState == EObjectStateType::Dynamic;
		if(bNeedsSave)
		{
			//GetStateAtFrameImp returns a pointer from the TArray that holds state data
			//But it's possible that we'll need to save state from head, which would grow that TArray
			//So preallocate just in case
			FGeometryParticleStateBase& LatestState = DirtyParticleInfo.AddFrame(CurFrame);

			if(const FGeometryParticleStateBase* RewindState = GetStateAtFrameImp(DirtyParticleInfo,Frame))
			{

				LatestState.SyncIfDirty(FDirtyPropData(DestManager,DataIdx++),*DirtyParticleInfo.GetPTParticle(),*RewindState);
				CoalesceBack(DirtyParticleInfo.Frames,CurFrame);

				if(bAllowSync)
				{
					RewindState->SyncToParticle(*DirtyParticleInfo.GetPTParticle());
				}
			}
		} else if(bAllowSync)
		{
			if(const FGeometryParticleStateBase* RewindState = GetStateAtFrameImp(DirtyParticleInfo,Frame))
			{
				RewindState->SyncToParticle(*DirtyParticleInfo.GetPTParticle());
			}
		}

	}

	CurFrame = Frame;
	bNeedsSave = false;
	FramesSaved = 0; //can't rewind before this point. This simplifies saving the state at head
	++CurWave;
	if(CurWave == 0)
	{
		//0 indicates nothing written so skip it
		CurWave = 1;
	}

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
FGeometryParticleState FRewindData::GetPastStateAtFrame(const FGeometryParticleHandle& Handle,int32 Frame) const
{
	ensure(!IsResim());
	if(const FDirtyParticleInfo* Info = FindParticle(Handle.UniqueIdx()))
	{
		if(const FGeometryParticleStateBase* State = GetStateAtFrameImp(*Info,Frame))
		{
			return FGeometryParticleState(*State,Handle);
		}
	}

	//If no data, or past capture, just use head
	return FGeometryParticleState(Handle);
}

/* Query the state of particles in the future. This operation can fail for particles that are desynced or that we have not been tracking */
EFutureQueryResult FRewindData::GetFutureStateAtFrame(FGeometryParticleState& OutState,int32 Frame) const
{
	ensure(IsResim());
	const FGeometryParticleHandle& Handle = OutState.GetHandle();

	if(const FDirtyParticleInfo* Info = FindParticle(Handle.UniqueIdx()))
	{
		if(Info->bDesync)
		{
			return EFutureQueryResult::Desync;
		}

		if(const FGeometryParticleStateBase* State = GetStateAtFrameImp(*Info,Frame))
		{
			OutState.SetState(*State);
			return EFutureQueryResult::Ok;
		}
	}

	return EFutureQueryResult::Untracked;
}

void FRewindData::FinishFrame()
{
	QUICK_SCOPE_CYCLE_COUNTER(RewindDataFinishFrame);

	if(IsResim())
	{
		const bool bLastResim = IsFinalResim();
		ensure(IsInPhysicsThreadContext());
		//snap particles forward that are not desynced or  do not have resim enabled
		//TODO: handle desync case
		for(FDirtyParticleInfo& Info : AllDirtyParticles)
		{
			if(auto* Rigid = Info.GetPTParticle()->CastToRigidParticle())
			{
				const bool bSnapFromCache = bResimOptimization && Rigid->SyncState() == ESyncState::InSync;
				if(Rigid->ResimType() == EResimType::ResimAsSlave || bSnapFromCache)
				{
					//Resim as slave means we snap everything as it was regardless of divergence
					//We do this in FinishFrame and AdvanceFrame because the state must be preserved before and after
					//This is because gameplay code could modify state before or after
					ensure(!Info.bDesync);
					if(const FSimWritableState* SimWritableState = GetSimWritableStateAtFrame(Info,CurFrame))
					{
						if(SimWritableState->FrameRecordedHack == CurFrame)
						{
							SimWritableState->SyncToParticle(*Rigid);
						}
					}
				}
			}

			//Last resim so mark everything as in sync
			if(bLastResim)
			{
				Info.GetPTParticle()->SetSyncState(ESyncState::InSync);
			}
		}
	}

	++CurFrame;
	LatestFrame = FMath::Max(LatestFrame,CurFrame);
}

void FRewindData::AdvanceFrameImp(IResimCacheBase* ResimCache)
{
	FramesSaved = FMath::Min(FramesSaved+1,static_cast<int32>(Managers.Capacity()));

	const int32 EarliestFrame = CurFrame - 1 - FramesSaved;

	TArray<TGeometryParticleHandle<FReal,3>*> DesyncedParticles;
	if(IsResim() && ResimCache)
	{
		DesyncedParticles.Reserve(AllDirtyParticles.Num());
	}

	for(int32 DirtyIdx = AllDirtyParticles.Num() - 1; DirtyIdx >= 0; --DirtyIdx)
	{
		FDirtyParticleInfo& Info = AllDirtyParticles[DirtyIdx];
		//if hasn't changed in a while stop tracking
		if(Info.LastDirtyFrame < EarliestFrame)
		{
			RemoveParticle(AllDirtyParticles[DirtyIdx].CachedUniqueIdx);
		} else if(IsResim())
		{
			EResimType ResimType = EResimType::FullResim;
			auto* Rigid = Info.GetPTParticle()->CastToRigidParticle();
			if(Rigid)
			{
				ResimType = Rigid->ResimType();
			}

			if(ResimType == EResimType::FullResim && !Info.bDesync)
			{
				//During a resim it's possible the user will not dirty a particle that was previously dirty.
				//If this happens we need to mark the particle as desynced
				if(Info.GTDirtyOnFrame[CurFrame].MissingWrite(CurFrame,CurWave))
				{
					Info.Desync(CurFrame,LatestFrame);
				}
				else if(Rigid && Rigid->ObjectState() != EObjectStateType::Kinematic)
				{
					//If we have a simulated particle, make sure its sim-writable properties are still in sync
					const FGeometryParticleStateBase* ExpectedState = GetStateAtFrameImp(Info,CurFrame);
					if(ExpectedState)
					{
						if(ExpectedState->IsSimWritableDesynced(*Rigid))
						{
							Info.Desync(CurFrame,LatestFrame);
						} else if(!Info.bDesync)
						{
							//Particle may have been marked as soft desync "may desync", but we see it's in sync so mark it as such
							Rigid->SetSyncState(ESyncState::InSync);
						}
					}
				}
			}
			else if(ResimType == EResimType::ResimAsSlave)
			{
				//Resim as slave means we snap everything as it was regardless of divergence
				//We do this in FinishFrame and AdvanceFrame because the state must be preserved before and after 
				//This is because gameplay code could modify state before or after
				const FGeometryParticleStateBase* ExpectedState = GetStateAtFrameImp(Info,CurFrame);
				ensure(!Info.bDesync);
				ensure(Info.MostDesynced == ESyncState::InSync);
				if(ensure(ExpectedState != nullptr))
				{
					ExpectedState->SyncToParticle(*Rigid);
				}
			}

			if(Info.bDesync)
			{
				//Any desync from GT is considered a hard desync - in theory we could make this more fine grained
				Info.GetPTParticle()->SetSyncState(ESyncState::HardDesync);

				if(ResimCache)
				{
					DesyncedParticles.Add(Info.GetPTParticle());
				}
			}
		}
		else
		{
			//not a resim so reset most desynced (this can't be done during resim because user may need info after final resim but before first normal sim)
			Info.MostDesynced = ESyncState::InSync;
		}
	}

	if(IsResim() && ResimCache)
	{
		ResimCache->SetDesyncedParticles(MoveTemp(DesyncedParticles));
	}
}

void FRewindData::CoalesceBack(TCircularBuffer<FFrameInfo>& Frames,int32 LatestIdx)
{
	const FGeometryParticleStateBase& LatestState = Frames[LatestIdx].GetStateChecked(LatestIdx);
	const int32 EarliestFrame = LatestIdx - FramesSaved;
	for(int32 FrameIdx = LatestIdx - 1; FrameIdx >= EarliestFrame; --FrameIdx)
	{
		if(FGeometryParticleStateBase* State = Frames[FrameIdx].GetState(FrameIdx))
		{
			if(State->CoalesceState(LatestState) == false)
			{
				//nothing to coalesce so no need to check earlier frames
				break;
			}
		}
	}
}

void FRewindData::PrepareFrame(int32 NumDirtyParticles)
{
	FFrameManagerInfo& Info = Managers[CurFrame];
	if(Info.Manager == nullptr)
	{
		Info.Manager = MakeUnique<FDirtyPropertiesManager>();
	}

	DataIdxOffset = Info.Manager->GetNumParticles();
	Info.Manager->SetNumParticles(DataIdxOffset + NumDirtyParticles);
	Info.FrameCreatedFor = CurFrame;
}

void FRewindData::PrepareFrameForPTDirty(int32 NumActiveParticles)
{
	bNeedsSave = true;

	//If manager already exists for previous frame, use it
	FFrameManagerInfo& Info = Managers[CurFrame];
	ensure(Info.Manager && Info.FrameCreatedFor == (CurFrame));

	DataIdxOffset = Info.Manager->GetNumParticles();
	Info.Manager->SetNumParticles(DataIdxOffset + NumActiveParticles);
}

template <bool bResim>
void FRewindData::PushGTDirtyData(const FDirtyPropertiesManager& SrcManager,const int32 SrcDataIdx,const FDirtyProxy& Dirty)
{
	const int32 DestDataIdx = SrcDataIdx + DataIdxOffset;
	//This records changes enqueued by GT.
	//Most new particles do not change, so to avoid useless writes we wait until the next frame's dirty flag
	//This is possible because most properties are const on the physics thread
	//For sim-writable properties (forces, position, velocities, etc...) we must immediately write the data because there is no way to know what the previous data was next frame
	//Some sim-writable properties can change without the GT knowing about it, see PushPTDirtyData

	//User called PrepareManagerForFrame for this frame so use it
	FConstDirtyPropData SrcManagerWrapper(&SrcManager,SrcDataIdx);
	FDirtyPropData DestManagerWrapper(Managers[CurFrame].Manager.Get(),DestDataIdx);
	bNeedsSave = true;

	auto ProcessProxy = [this,&SrcManagerWrapper,SrcDataIdx,Dirty,&DestManagerWrapper](const auto Proxy)
	{
		const auto PTParticle = Proxy->GetHandle_LowLevel();
		FDirtyParticleInfo& Info = FindOrAddParticle(*PTParticle);
		Info.LastDirtyFrame = CurFrame;
		Info.GTDirtyOnFrame[CurFrame].SetWave(CurFrame,CurWave);

		//check if particle has desynced
		if(bResim)
		{
			EResimType ResimType = EResimType::FullResim;
			if(const auto* Rigid = PTParticle->CastToRigidParticle())
			{
				ResimType = Rigid->ResimType();
			}

			//Only desync if full resim - might be nice to give a log warning for other cases
			if(ResimType == EResimType::FullResim)
			{
				//TODO: should not be passing GTParticle in here, it's not used so ok but not safe if someone decides to use it by mistake
				FGeometryParticleState FutureState(*Proxy->GetHandle_LowLevel());
				if(GetFutureStateAtFrame(FutureState,CurFrame) == EFutureQueryResult::Ok)
				{
					if(FutureState.IsDesynced(SrcManagerWrapper,*PTParticle,Dirty.ParticleData.GetFlags()))
					{
						Info.Desync(CurFrame-1,LatestFrame);
					}
				} else if(!Info.bDesync)
				{
					Info.Desync(CurFrame-1,LatestFrame);
				}
			}
		}

		//Most properties are always a frame behind
		if(Proxy->IsInitialized())	//Frame delay so proxy must be initialized
		{
			//If we're in a resim and this is the first frame or the resim, no need to save prev frame
			//In fact, since we have a circular buffer the prev state could end up overwriting head which we need for fast forward
			if(!bResim || FramesSaved > 0)
			{
				FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame-1);
				LatestState.SyncPrevFrame(DestManagerWrapper,Dirty);
				CoalesceBack(Info.Frames,CurFrame-1);
			}
		}

		//If dynamics are dirty we must record them immediately because the sim resets them to 0
		if(Dirty.ParticleData.IsDirty(EParticleFlags::Dynamics))
		{
			FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame);
			LatestState.SyncDirtyDynamics(DestManagerWrapper,Dirty.ParticleData,SrcManagerWrapper);
		}
	};

	if(ensure(Dirty.Proxy->GetType() == EPhysicsProxyType::SingleParticleProxy))
	{
		auto Proxy = static_cast<FSingleParticlePhysicsProxy*>(Dirty.Proxy);
		ProcessProxy(Proxy);
	}
}

template <bool bResim>
void FRewindData::PushPTDirtyData(TPBDRigidParticleHandle<FReal,3>& Rigid,const int32 SrcDataIdx)
{
	const int32 DestDataIdx = SrcDataIdx + DataIdxOffset;

	//during resim only full resim objects should modify future
	if(bResim)
	{
		if(Rigid.ResimType() != EResimType::FullResim)
		{
			if(FindParticle(Rigid.UniqueIdx()) == nullptr)
			{
				//no history but collision moved/woke us up so snap back manually (if history exists we'll snap in FinishFrame
				Rigid.SetP(Rigid.X());
				Rigid.SetQ(Rigid.R());
				Rigid.SetV(Rigid.PreV());
				Rigid.SetW(Rigid.PreW());
			}
			return;
		} else if(bResimOptimization && Rigid.SyncState() == ESyncState::InSync)
		{
			//fully in sync means no sim was done - don't write current intermediate values since we snap later anyway
			return;
		}
	}

	//todo: is this check needed? why do we pass sleeping rigids into this function?
	if(SimWritablePropsMayChange(Rigid))
	{
		FDirtyParticleInfo& Info = FindOrAddParticle(Rigid);
		Info.LastDirtyFrame = CurFrame;

		//User called PrepareManagerForFrame (or PrepareFrameForPTDirty) for the previous frame, so use it
		FDirtyPropData DestManagerWrapper(Managers[CurFrame].Manager.Get(),DestDataIdx);

		//sim-writable properties changed at head, so we must write down what they were
		FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame);
		LatestState.SyncSimWritablePropsFromSim(DestManagerWrapper,Rigid);

		//copy results of end of frame in case user changes inputs of next frame (for example they can teleport at start frame)
		const bool bDesynced = Info.Frames[CurFrame].GetSimWritableStateChecked(CurFrame).SyncSimWritablePropsFromSim<bResim>(Rigid, CurFrame);

		if(bResim)
		{
			if(bDesynced)
			{
				Info.Desync(CurFrame+1,LatestFrame);	//next frame must be desynced since results of this frame are different
				Rigid.SetSyncState(ESyncState::HardDesync);
			}
			
			//If we are only at soft desync, make sure to record as such
			Info.MostDesynced = Rigid.SyncState();
		}

		//update any previous frames that were pointing at head
		CoalesceBack(Info.Frames,CurFrame);
	}
}

const FGeometryParticleStateBase* FRewindData::GetStateAtFrameImp(const FDirtyParticleInfo& Info,int32 Frame) const
{
	const TCircularBuffer<FFrameInfo>& Frames = Info.Frames;
	if(const FGeometryParticleStateBase* FrameState = Frames[Frame].GetState(Frame))
	{
		return FrameState;
	}

	//If frame is between two captures, use later capture. We always store the last data before a change
	//We can never use an earlier capture because the fact that we captured at all implies _something_ is different from proceeding frames

	for(int32 FrameIdx = Frame + 1; FrameIdx <= LatestFrame; ++FrameIdx)
	{
		if(const FGeometryParticleStateBase* FrameState = Frames[FrameIdx].GetState(FrameIdx))
		{
			return FrameState;
		}
	}

	//If no data, or past capture, just use head
	return nullptr;
}

FRewindData::FDirtyParticleInfo& FRewindData::FindOrAddParticle(TGeometryParticleHandle<FReal,3>& PTParticle)
{
	const FUniqueIdx UniqueIdx = PTParticle.UniqueIdx();
	if(FDirtyParticleInfo* Info = FindParticle(UniqueIdx))
	{
		return *Info;
	}

	auto* GTUnsafeParticle = PTParticle.GTGeometryParticle();
	UE_ASSUME(GTUnsafeParticle != nullptr);
	const int32 DirtyIdx = AllDirtyParticles.Add(FDirtyParticleInfo(*GTUnsafeParticle,PTParticle,UniqueIdx,CurFrame,Managers.Capacity()));
	ParticleToAllDirtyIdx.Add(UniqueIdx,DirtyIdx);

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

FGeometryParticleStateBase& FRewindData::FDirtyParticleInfo::AddFrame(int32 FrameIdx)
{
	FFrameInfo& Info = Frames[FrameIdx];
	if(FGeometryParticleStateBase* State = Info.GetState(FrameIdx))
	{
		return *State;
	}

	return Info.NewState(FrameIdx);
}

void FRewindData::FDirtyParticleInfo::Desync(int32 StartDesync,int32 LastFrame)
{
	bDesync = true;
	MostDesynced = ESyncState::HardDesync;
	for(int32 Frame = StartDesync; Frame <= LastFrame; ++Frame)
	{
		Frames[Frame].ClearState();
	}
}

TArray<FDesyncedParticleInfo> FRewindData::ComputeDesyncInfo() const
{
	TArray<FDesyncedParticleInfo> Results;
	Results.Reserve(AllDirtyParticles.Num());

	for(const FDirtyParticleInfo& Info : AllDirtyParticles)
	{
		if(Info.MostDesynced != ESyncState::InSync)
		{
			Results.Add(FDesyncedParticleInfo{Info.GetPTParticle(),Info.MostDesynced});
		}
	}

	return Results;
}

template void FRewindData::PushGTDirtyData<true>(const FDirtyPropertiesManager& SrcManager,const int32 SrcDataIdx,const FDirtyProxy& Dirty);
template void FRewindData::PushGTDirtyData<false>(const FDirtyPropertiesManager& SrcManager,const int32 SrcDataIdx,const FDirtyProxy& Dirty);

template void FRewindData::PushPTDirtyData<true>(TPBDRigidParticleHandle<FReal,3>& Rigid,const int32 SrcDataIdx);
template void FRewindData::PushPTDirtyData<false>(TPBDRigidParticleHandle<FReal,3>& Rigid,const int32 SrcDataIdx);

}