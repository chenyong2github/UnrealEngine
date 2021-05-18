// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindData.h"
#include "PBDRigidsSolver.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

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

bool FGeometryParticleStateBase::IsSimWritableDesynced(TPBDRigidParticleHandle<FReal,3>& Particle) const
{
#if 0
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
#endif
	return false;
}

template <typename TParticle>
void FGeometryParticleStateBase::SyncToParticle(TParticle& Particle, const FDirtyPropertiesPool& Manager) const
{
	if(const auto* Data = ParticlePositionRotation.Read(Manager))
	{
		Particle.SetXR(*Data);
	}

	if(auto Kinematic = Particle.CastToKinematicParticle())
	{
		if(const auto* Data = Velocities.Read(Manager))
		{
			Kinematic->SetVelocities(*Data);

			// If we changed the velocity, reset the smoothed velocity.
			// This is not strictly correct but should be close. Worst case would 
			// be a delay in sleeping after a rewind.
			if (auto Rigid = Kinematic->CastToRigidParticle())
			{
				Rigid->ResetSmoothedVelocities();
			}
		}

		if(const auto* Data = KinematicTarget.Read(Manager))
		{
			Kinematic->SetKinematicTarget(*Data);
		}
	}

	if(const auto* Data = NonFrequentData.Read(Manager))
	{
		Particle.SetNonFrequentData(*Data);
	}

	if(auto Rigid = Particle.CastToRigidParticle())
	{
		if(const auto* Data = DynamicsMisc.Read(Manager))
		{
			Rigid->SetDynamicMisc(*Data);
		}

		if(const auto* Data = MassProps.Read(Manager))
		{
			Rigid->SetMassProps(*Data);
		}

		if(Rigid->ResimType() != EResimType::FullResim)
		{
			//Not full resim so apply dynamics automatically
			if(const auto* Data = Dynamics.Read(Manager))
			{
				Rigid->SetDynamics(*Data);
			}
		}
	}
}

bool SimWritablePropsMayChange(const TGeometryParticleHandle<FReal,3>& Handle)
{
	const auto ObjectState = Handle.ObjectState();
	return ObjectState == EObjectStateType::Dynamic || ObjectState == EObjectStateType::Sleeping;
}

void FGeometryParticleStateBase::RecordDynamics(const FParticleDynamics& NewDynamics, FDirtyPropertiesPool& Manager)
{
	ensure(!Dynamics.IsSet());	//Only record dynamics once per phase
	Dynamics.Write(Manager, NewDynamics);
}

void FGeometryParticleStateBase::RecordPreDirtyData(const FDirtyProxy& Dirty, const FShapeDirtyData* ShapeDirtyData, FDirtyPropertiesPool& Manager)
{
	//syncs the data before it was made dirty
	ensure(IsClean());
	const auto Proxy = static_cast<const FSingleParticlePhysicsProxy*>(Dirty.Proxy);
	const FGeometryParticleHandle* Handle = Proxy->GetHandle_LowLevel();

	if(Dirty.ParticleData.IsDirty(EParticleFlags::XR))
	{
		FParticlePositionRotation Data;
		Data.CopyFrom(*Handle);
		ParticlePositionRotation.Write(Manager, Data);
	}

	if(Dirty.ParticleData.IsDirty(EParticleFlags::NonFrequentData))
	{
		FParticleNonFrequentData Data;
		Data.CopyFrom(*Handle);
		NonFrequentData.Write(Manager, Data);
	}

	if (auto Kinematic = Handle->CastToKinematicParticle())
	{
		if (Dirty.ParticleData.IsDirty(EParticleFlags::Velocities))
		{
			FParticleVelocities Data;
			Data.CopyFrom(*Kinematic);
			Velocities.Write(Manager, Data);
		}

		if(Dirty.ParticleData.IsDirty(EParticleFlags::KinematicTarget))
		{
			KinematicTarget.Write(Manager, Kinematic->KinematicTarget());
		}

		if (auto Rigid = Handle->CastToRigidParticle())
		{
			if(Dirty.ParticleData.IsDirty(EParticleFlags::DynamicMisc))
			{
				FParticleDynamicMisc Data;
				Data.CopyFrom(*Rigid);
				DynamicsMisc.Write(Manager, Data);
			}

			if (Dirty.ParticleData.IsDirty(EParticleFlags::MassProps))
			{
				FParticleMassProps Data;
				Data.CopyFrom(*Rigid);
				MassProps.Write(Manager, Data);
			}
		}
	}

	//shape properties
	const int32 NumCurrentShapes = Handle->ShapesArray().Num();
	for (int32 ShapeDataIdx : Dirty.ShapeDataIndices)
	{
		const FShapeDirtyData& ShapeData = ShapeDirtyData[ShapeDataIdx];
		const int32 ShapeIdx = ShapeData.GetShapeIdx();
		
		FPerShapeDataStateBase& ShapeState = ShapesArrayState.FindOrAdd(ShapeIdx);

		if(ShapeIdx >= NumCurrentShapes)
		{
			// We are updating the rewind data here before the handle was dirty but the dirty data may
			// contain shapes we do not yet know about (if an actor had shapes added to it within the
			// last update - this can happen when welding). If we encounter a shape we don't know about
			// we skip the shape. Can't fully early out as the dirty indices may not be ordered.
			// The actual shapes array in the handle will be updated after this step when we get into
			// the proxy push function.
			continue;
		}

		if (Handle->ShapesArray()[ShapeIdx] == nullptr)
		{ 
			//TODO: why is this needed?
			continue;
		}

		if (ShapeData.IsDirty<EShapeProperty::CollisionData>())
		{
			ShapeState.CollisionData.Write(Handle->ShapesArray()[ShapeIdx]->GetCollisionData());
		}

		if (ShapeData.IsDirty<EShapeProperty::Materials>())
		{
			ShapeState.MaterialData.Write(Handle->ShapesArray()[ShapeIdx]->GetMaterialData());
		}
	}
}

void FGeometryParticleStateBase::RecordAnyDirty(const FGeometryParticleHandle& Handle, FDirtyPropertiesPool& Manager, const FGeometryParticleStateBase& OldState)
{
	if (!ParticlePositionRotation.IsSet() && OldState.ParticlePositionRotation.IsSet())
	{
		FParticlePositionRotation Data;
		Data.CopyFrom(Handle);
		ParticlePositionRotation.Write(Manager, Data);
	}

	if (!NonFrequentData.IsSet() && OldState.NonFrequentData.IsSet())
	{
		FParticleNonFrequentData Data;
		Data.CopyFrom(Handle);
		NonFrequentData.Write(Manager, Data);
	}

	if (auto Kinematic = Handle.CastToKinematicParticle())
	{
		if (!Velocities.IsSet() && OldState.Velocities.IsSet())
		{
			FParticleVelocities Data;
			Data.CopyFrom(*Kinematic);
			Velocities.Write(Manager, Data);
		}

		if (!KinematicTarget.IsSet() && OldState.KinematicTarget.IsSet())
		{
			KinematicTarget.Write(Manager, Kinematic->KinematicTarget());
		}

		if (auto Rigid = Kinematic->CastToRigidParticle())
		{
			if (!DynamicsMisc.IsSet() && OldState.DynamicsMisc.IsSet())
			{
				FParticleDynamicMisc Data;
				Data.CopyFrom(*Rigid);
				DynamicsMisc.Write(Manager, Data);
			}

			if (!MassProps.IsSet() && OldState.MassProps.IsSet())
			{
				FParticleMassProps Data;
				Data.CopyFrom(*Rigid);
				MassProps.Write(Manager, Data);
			}
		}
	}

	//question: do we need to do this for shapes?
}

void FGeometryParticleStateBase::MarkAllDirty(const FGeometryParticleHandle& Handle, FDirtyPropertiesPool& Manager)
{
	//TODO: this function should be removed and we should use the property system instead
	//For now it just dirties all PT properties that we typically use
	//This means sim callback can't modify mass, geometry, etc... (only properties touched by this function)
	//Note these same properties are sent back to GT, so it's not just this function that needs updating

	if (!ParticlePositionRotation.IsSet())
	{
		FParticlePositionRotation Data;
		Data.CopyFrom(Handle);
		ParticlePositionRotation.Write(Manager, Data);
	}

	if (auto Kinematic = Handle.CastToKinematicParticle())
	{
		if (!Velocities.IsSet())
		{
			FParticleVelocities Data;
			Data.CopyFrom(*Kinematic);
			Velocities.Write(Manager, Data);
		}

		if (auto Rigid = Kinematic->CastToRigidParticle())
		{
			if (!DynamicsMisc.IsSet())
			{
				FParticleDynamicMisc Data;
				Data.CopyFrom(*Rigid);
				DynamicsMisc.Write(Manager, Data);
			}
		}
	}
}

void FGeometryParticleStateBase::RecordSimResults(const FPBDRigidParticleHandle& Handle, FDirtyPropertiesPool& Manager)
{
	//todo: return desync info
	{
		FParticlePositionRotation Data;
		Data.CopyFrom(Handle);
		ParticlePositionRotation.Write(Manager, Data);	//X/R still the same as input, so we can use them directly
	}

	{
		FParticleVelocities Data;
		Data.SetV(Handle.PreV());
		Data.SetW(Handle.PreW());
		Velocities.Write(Manager, Data);
	}

	{
		FParticleDynamicMisc Data;
		Data.CopyFrom(Handle);	//everything is immutable except object state
		Data.SetObjectState(Handle.PreObjectState());
		DynamicsMisc.Write(Manager, Data);
	}
}

void FGeometryParticleStateBase::CopyToParticle(FGeometryParticleHandle& Handle, FDirtyPropertiesPool& Manager)
{
	ensure(IsInPhysicsThreadContext());

	if(const auto* Data = ParticlePositionRotation.Read(Manager))
	{
		Handle.SetXR(*Data);
	}

	if(const auto* Data = NonFrequentData.Read(Manager))
	{
		Handle.SetNonFrequentData(*Data);
	}

	if (const auto Kinematic = Handle.CastToKinematicParticle())
	{
		if (const auto* Data = Velocities.Read(Manager))
		{
			Kinematic->SetVelocities(*Data);
		}

		if (const auto* Data = KinematicTarget.Read(Manager))
		{
			Kinematic->SetKinematicTarget(*Data);
		}

		if (auto Rigid = Kinematic->CastToRigidParticle())
		{
			if (const auto* Data = DynamicsMisc.Read(Manager))
			{
				Rigid->SetDynamicMisc(*Data);
			}

			if (const auto* Data = Dynamics.Read(Manager))
			{
				Rigid->SetDynamics(*Data);
			}

			if (const auto* Data = MassProps.Read(Manager))
			{
				Rigid->SetMassProps(*Data);
			}
		}
	}
}

bool FGeometryParticleStateBase::CoalesceState(const FGeometryParticleStateBase& LatestState, FDirtyPropertiesPool& Manager)
{
	bool bCoalesced = false;
	if(!ParticlePositionRotation.IsSet() && LatestState.ParticlePositionRotation.IsSet())
	{
		ParticlePositionRotation.SetRefFrom(LatestState.ParticlePositionRotation, Manager);
		bCoalesced = true;
	}

	if(!NonFrequentData.IsSet() && LatestState.NonFrequentData.IsSet())
	{
		NonFrequentData.SetRefFrom(LatestState.NonFrequentData, Manager);
		bCoalesced = true;
	}

	if(!Velocities.IsSet() && LatestState.Velocities.IsSet())
	{
		Velocities.SetRefFrom(LatestState.Velocities, Manager);
		bCoalesced = true;
	}

	if (!KinematicTarget.IsSet() && LatestState.KinematicTarget.IsSet())
	{
		KinematicTarget.SetRefFrom(LatestState.KinematicTarget, Manager);
		bCoalesced = true;
	}

	if(!MassProps.IsSet() && LatestState.MassProps.IsSet())
	{
		MassProps.SetRefFrom(LatestState.MassProps, Manager);
		bCoalesced = true;
	}

	if(!DynamicsMisc.IsSet() && LatestState.DynamicsMisc.IsSet())
	{
		DynamicsMisc.SetRefFrom(LatestState.DynamicsMisc, Manager);
		bCoalesced = true;
	}

	//TODO: this assumes geometry is never modified. Geometry modification has various issues in higher up Chaos code. Need stable shape id
	//For now iterate over all the dirty shapes in latest and see if they have any new dirty properties. This assumes shape indices refer to the same shapes
	//Note, the shapes array only include dirty shapes (or clean shapes that came before it in the array), so the size of the array may not match

	const TArray<FPerShapeDataStateBase>& LatestShapes = LatestState.ShapesArrayState.PerShapeData;
	TArray<FPerShapeDataStateBase>& Shapes = ShapesArrayState.PerShapeData;

	if(LatestShapes.Num() > Shapes.Num())
	{
		//add clean shapes
		Shapes.SetNum(LatestShapes.Num());
	}

	for(int32 ShapeIdx = 0; ShapeIdx < LatestShapes.Num(); ++ShapeIdx)
	{
		const FPerShapeDataStateBase& LatestShape = LatestShapes[ShapeIdx];
		FPerShapeDataStateBase& Shape = Shapes[ShapeIdx];
		if(!Shape.CollisionData.IsSet() && LatestShape.CollisionData.IsSet())
		{
			Shape.CollisionData = LatestShape.CollisionData;
			bCoalesced = true;
		}

		if(!Shape.MaterialData.IsSet() && LatestShape.MaterialData.IsSet())
		{
			Shape.MaterialData = LatestShape.MaterialData;
			bCoalesced = true;
		}
	}
	
	//dynamics do not coalesce since they are always written when dirty

	return bCoalesced;
}

bool FGeometryParticleStateBase::IsDesynced(const FConstDirtyPropData& SrcManager,const TGeometryParticleHandle<FReal,3>& Handle,const FParticleDirtyFlags Flags) const
{
	bool Desynced = false;
	{
#if 0
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
#endif

	}
	return false;
}

bool FRewindData::RewindToFrame(int32 Frame)
{
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


	int32 DataIdx = 0;
	for(FDirtyParticleInfo& DirtyParticleInfo : AllDirtyParticles)
	{
		DirtyParticleInfo.bDesync = false;	//after rewind particle is pristine
		DirtyParticleInfo.MostDesynced = ESyncState::InSync;

		//Note: it's debatable if we should rewind to PrePushData or PostPushData
		//If you rewind to PrePushData and resim the data will be reapplied automatically
		//The downside with this is that for unit testing it makes it harder to see that the GT data is being properly aligned as an input for the solver step
		if (FGeometryParticleStateBase* RewindState = DirtyParticleInfo.Frames[Frame].GetState(FParticleHistoryEntry::PostPushData, Frame, Buffer))
		{
			//at least one property will change between Frame and CurFrame, so make sure that property is saved (needed for quick resim to snap to future)
			FParticleHistoryEntry& LatestEntry = DirtyParticleInfo.AddFrame(CurFrame, Buffer, PropertiesPool);
			FGeometryParticleStateBase& LatestState = LatestEntry.GetStateChecked(FParticleHistoryEntry::PostPushData, CurFrame, Buffer);
			LatestState.RecordAnyDirty(*DirtyParticleInfo.GetPTParticle(), PropertiesPool, *RewindState);

			//since at least one property changed, update the particle as needed
			RewindState->CopyToParticle(*DirtyParticleInfo.GetPTParticle(), PropertiesPool);
			
		}

		if(DirtyParticleInfo.InitializedOnStep > Frame)
		{
			//hasn't initialized yet, so disable
			Solver->GetEvolution()->DisableParticle(DirtyParticleInfo.GetPTParticle());
		}

		//reset old buffer (we'll be using it for resim now)
		for(uint32 BufferFrame = 0; BufferFrame < DirtyParticleInfo.Frames.Capacity(); ++BufferFrame)
		{
			DirtyParticleInfo.Frames[BufferFrame].Reset(PropertiesPool, !Buffer);
		}
	}


	CurFrame = Frame;
	bNeedsSave = false;
	FramesSaved = 0; //can't rewind before this point. This simplifies saving the state at head
	bNeedBufferFlip = true;

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
	ensure(Frame >= GetEarliestFrame_Internal());	//can't get state from before the frame we rewound to

	const FDirtyParticleInfo* Info = FindParticle(Handle.UniqueIdx());
	const FGeometryParticleStateBase* State = Info ? GetStateAtFrameImp(*Info, Frame) : nullptr;
	return FGeometryParticleState(State,Handle, PropertiesPool);
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
			OutState.SetState(State);
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
#if 0
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
#endif
	}

	++CurFrame;
	LatestFrame = FMath::Max(LatestFrame,CurFrame);
}

void FRewindData::AdvanceFrameImp(IResimCacheBase* ResimCache)
{
	FlipBufferIfNeeded();

	FramesSaved = FMath::Min(FramesSaved+1,static_cast<int32>(Managers.Capacity()-1));

	const int32 EarliestFrame = CurFrame - 1 - FramesSaved;

	TArray<TGeometryParticleHandle<FReal,3>*> DesyncedParticles;
	if(IsResim() && ResimCache)
	{
		DesyncedParticles.Reserve(AllDirtyParticles.Num());
	}

	for(int32 DirtyIdx = AllDirtyParticles.Num() - 1; DirtyIdx >= 0; --DirtyIdx)
	{
		FDirtyParticleInfo& Info = AllDirtyParticles[DirtyIdx];

		//Sim hasn't run yet so PostCallbacks (sim results) should be clean
		ensure(Info.Frames[CurFrame].GetState(FParticleHistoryEntry::PostCallbacks, CurFrame, Buffer) == nullptr ||
			Info.Frames[CurFrame].GetState(FParticleHistoryEntry::PostCallbacks, CurFrame, Buffer)->IsClean());

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
#if 0
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
					ExpectedState->SyncToParticle(*Rigid, PropertiesPool);
				}
			}
#endif

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

void FRewindData::CoalesceBack(TCircularBuffer<FParticleHistoryEntry>& Frames, const FParticleHistoryEntry::EParticleHistoryPhase LatestPhase)
{
	//CoalesceBack should be called after writing state to latest, so CurFrame should always have a state
	const FGeometryParticleStateBase& LatestState = Frames[CurFrame].GetStateChecked(LatestPhase, CurFrame, Buffer);

	bool bContinueToCoalesce = true;
	if(LatestPhase > FParticleHistoryEntry::PrePushData)
	{
		//make sure state is coalesced back CurFrame first (all phases must know about it)
		bContinueToCoalesce = Frames[CurFrame].CoalesceState(LatestState, PropertiesPool, static_cast<FParticleHistoryEntry::EParticleHistoryPhase>(LatestPhase-1), Buffer);
	}

	//coalesce state back to earlier frames
	const int32 EarliestFrame = FMath::Max(CurFrame - FramesSaved, 0);
	for(int32 FrameIdx = CurFrame - 1; bContinueToCoalesce && FrameIdx >= EarliestFrame; --FrameIdx)
	{
		Frames[FrameIdx].NewFrameIfNeeded(FrameIdx, Buffer, PropertiesPool);
		bContinueToCoalesce = Frames[FrameIdx].CoalesceState(LatestState, PropertiesPool, FParticleHistoryEntry::PostCallbacks, Buffer);	//make sure to fill all phases
	}
}

#ifndef REWIND_DESYNC
#define REWIND_DESYNC 0
#endif

void FRewindData::FlipBufferIfNeeded()
{
	if(bNeedBufferFlip)
	{
		bNeedBufferFlip = false;
		Buffer = !Buffer;
	}
}

template <bool bResim>
void FRewindData::PushGTDirtyData(const FDirtyPropertiesManager& SrcManager,const int32 SrcDataIdx,const FDirtyProxy& Dirty, const FShapeDirtyData* ShapeDirtyData)
{
	FlipBufferIfNeeded();
	//This records changes enqueued by GT.
	bNeedsSave = true;

	if(ensure(Dirty.Proxy->GetType() == EPhysicsProxyType::SingleParticleProxy))
	{
		auto Proxy = static_cast<FSingleParticlePhysicsProxy*>(Dirty.Proxy);
		
		FGeometryParticleHandle* PTParticle = Proxy->GetHandle_LowLevel();

		FDirtyParticleInfo& Info = FindOrAddParticle(*PTParticle, Proxy->IsInitialized() ? INDEX_NONE : CurFrame);
		Info.LastDirtyFrame = CurFrame;

		FParticleHistoryEntry& Latest = Info.AddFrame(CurFrame, Buffer, PropertiesPool);

		//At this point all phases should be clean
		ensure(Latest.GetStateChecked(FParticleHistoryEntry::PrePushData, CurFrame, Buffer).IsClean());
		ensure(Latest.GetStateChecked(FParticleHistoryEntry::PostPushData, CurFrame, Buffer).IsClean());
		ensure(Latest.GetStateChecked(FParticleHistoryEntry::PostCallbacks, CurFrame, Buffer).IsClean());


		//Most particles never change but may be created/destroyed often due to streaming
		//To avoid useless writes we call this function before PushData is processed.
		//This means we will skip particles that are streamed in since they never change
		//So if Proxy->IsInitialized() == true it means the particle isn't just streaming in, it's actually changing
		if (Proxy->IsInitialized())
		{
			Latest.GetStateChecked(FParticleHistoryEntry::PrePushData, CurFrame, Buffer).RecordPreDirtyData(Dirty, ShapeDirtyData, PropertiesPool);
			CoalesceBack(Info.Frames, FParticleHistoryEntry::PrePushData);
		}

		//Dynamics are not available at head (sim zeroes them out), so we have to record them as PostPushData (since they're applied as part of PushData)
		if (auto NewData = Dirty.ParticleData.FindDynamics(SrcManager, SrcDataIdx))
		{
			Latest.GetStateChecked(FParticleHistoryEntry::PostPushData, CurFrame, Buffer).RecordDynamics(*NewData, PropertiesPool);
			//No need to coalesce because Dynamics are always zero in PrePushData (Solver zeros out dynamics every frame)
		}

		ensure(Latest.GetStateChecked(FParticleHistoryEntry::PostCallbacks, CurFrame, Buffer).IsClean());	//PostCallbacks must be untouched
	}
}

void FRewindData::SpawnProxyIfNeeded(FSingleParticlePhysicsProxy& Proxy)
{
	if(Proxy.GetInitializedStep() > CurFrame)
	{
		FGeometryParticleHandle* Handle = Proxy.GetHandle_LowLevel();
		FindOrAddParticle(*Handle, CurFrame);

		Solver->GetEvolution()->EnableParticle(Handle, nullptr);
		Proxy.SetInitialized(CurFrame);
	}
}

void FRewindData::MarkDirtyFromPT(FGeometryParticleHandle& Handle)
{
	FDirtyParticleInfo& Info = FindOrAddParticle(Handle);
	Info.LastDirtyFrame = CurFrame;
	FParticleHistoryEntry& Latest = Info.AddFrame(CurFrame, Buffer, PropertiesPool);
	ensure(Latest.GetStateChecked(FParticleHistoryEntry::PostCallbacks, CurFrame, Buffer).IsClean());	//PostCallbacks must be untouched
	Latest.GetStateChecked(FParticleHistoryEntry::PostPushData, CurFrame, Buffer).MarkAllDirty(Handle, PropertiesPool);
	CoalesceBack(Info.Frames, FParticleHistoryEntry::PostPushData);
	ensure(Latest.GetStateChecked(FParticleHistoryEntry::PostCallbacks, CurFrame, Buffer).IsClean());	//PostCallbacks must be untouched
}

template <bool bResim>
void FRewindData::PushPTDirtyData(TPBDRigidParticleHandle<FReal,3>& Handle,const int32 SrcDataIdx)
{
	FDirtyParticleInfo& Info = FindOrAddParticle(Handle);
	Info.LastDirtyFrame = CurFrame;
	FParticleHistoryEntry& Latest = Info.AddFrame(CurFrame, Buffer, PropertiesPool);
	ensure(Latest.GetStateChecked(FParticleHistoryEntry::PostCallbacks, CurFrame, Buffer).IsClean());	//PostCallbacks should be clean before we write sim results
	Latest.GetStateChecked(FParticleHistoryEntry::PostCallbacks, CurFrame, Buffer).RecordSimResults(Handle, PropertiesPool);
	CoalesceBack(Info.Frames, FParticleHistoryEntry::PostCallbacks);
#if 0
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
		FGeometryParticleStateBase& LatestState = Info.AddFrame(CurFrame, Buffer, PropertiesPool);
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
#endif
}

const FGeometryParticleStateBase* FRewindData::GetStateAtFrameImp(const FDirtyParticleInfo& Info,int32 Frame) const
{
	//User wants state including PushData
	//Maybe we should make the function more explicit?
	return Info.Frames[Frame].GetState(FParticleHistoryEntry::PostPushData, Frame, Buffer);
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
		for (uint32 Frame = 0; Frame < Frames.Capacity(); ++Frame)
		{
			Frames[Frame].ResetAll(*PropertiesPool);
		}
	}
}

FParticleHistoryEntry& FRewindData::FDirtyParticleInfo::AddFrame(int32 FrameIdx, int32 InBuffer, FDirtyPropertiesPool& Manager)
{
	FParticleHistoryEntry& Info = Frames[FrameIdx];
	Info.NewFrameIfNeeded(FrameIdx, InBuffer, Manager);
	return Info;
}

void FRewindData::FDirtyParticleInfo::Desync(int32 StartDesync,int32 LastFrame)
{
	check(false);
#if 0
	bDesync = true;
	MostDesynced = ESyncState::HardDesync;
	for(int32 Frame = StartDesync; Frame <= LastFrame; ++Frame)
	{
		Frames[Frame].ClearState();
	}
#endif
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

template void FRewindData::PushGTDirtyData<true>(const FDirtyPropertiesManager& SrcManager,const int32 SrcDataIdx,const FDirtyProxy& Dirty, const FShapeDirtyData* DirtyShapesData);
template void FRewindData::PushGTDirtyData<false>(const FDirtyPropertiesManager& SrcManager,const int32 SrcDataIdx,const FDirtyProxy& Dirty, const FShapeDirtyData* DirtyShapesData);

template void FRewindData::PushPTDirtyData<true>(TPBDRigidParticleHandle<FReal,3>& Rigid,const int32 SrcDataIdx);
template void FRewindData::PushPTDirtyData<false>(TPBDRigidParticleHandle<FReal,3>& Rigid,const int32 SrcDataIdx);

}