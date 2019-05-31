// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraCollision.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraStats.h"
#include "NiagaraComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

DECLARE_CYCLE_STAT(TEXT("Collision"), STAT_NiagaraCollision, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Event Emission"), STAT_NiagaraEventWrite, STATGROUP_Niagara);

void FNiagaraCollisionBatch::KickoffNewBatch(FNiagaraEmitterInstance *Sim, float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraCollision);
	FNiagaraVariable PosVar(FNiagaraTypeDefinition::GetVec3Def(), "Position");
	FNiagaraVariable VelVar(FNiagaraTypeDefinition::GetVec3Def(), "Velocity");
	FNiagaraVariable SizeVar(FNiagaraTypeDefinition::GetVec2Def(), "SpriteSize");
	FNiagaraVariable TstVar(FNiagaraTypeDefinition::GetBoolDef(), "PerformCollision");
	FNiagaraDataSetAccessor<FVector> PosData(Sim->GetData(), PosVar);
	FNiagaraDataSetAccessor<FVector> VelData(Sim->GetData(), VelVar);
	FNiagaraDataSetAccessor<FVector2D> SizeData(Sim->GetData(), SizeVar);
	//FNiagaraDataSetIterator<int32> TstIt(Sim->GetData(), TstVar, 0, false);

	bool bUseSize = SizeData.IsValidForRead();

	if (!PosData.IsValidForRead() || !VelData.IsValidForRead() /*|| !TstIt.IsValid()*/)
	{
		return;
	}

	UWorld *SystemWorld = Sim->GetParentSystemInstance()->GetComponent()->GetWorld();
	if (SystemWorld)
	{
		CollisionTraces.Empty();

		for (uint32 i = 0; i < Sim->GetData().GetCurrentDataChecked().GetNumInstances(); i++)
		{
			//int32 TestCollision = *TstIt;
			//if (TestCollision)
			{
				FVector Position;
				FVector EndPosition;
				FVector Velocity;

				PosData.Get(i, Position);
				VelData.Get(i, Velocity);
				EndPosition = Position + Velocity * DeltaSeconds;

				if (bUseSize)
				{
					//TODO:  Handle mesh particles too.  Also this can probably be better and or faster.
					FVector2D SpriteSize;
					SizeData.Get(i, SpriteSize);
					float MaxSize = FMath::Max(SpriteSize.X, SpriteSize.Y);

					float Length;
					FVector Direction;
					Velocity.ToDirectionAndLength(Direction, Length);

					Position -= Direction * (MaxSize / 2);
					EndPosition += Direction * (MaxSize / 2);
				}

				FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiagraAsync));
				QueryParams.OwnerTag = "Niagara";
				FTraceHandle Handle = SystemWorld->AsyncLineTraceByChannel(EAsyncTraceType::Single, Position, EndPosition, ECollisionChannel::ECC_WorldStatic, QueryParams, FCollisionResponseParams::DefaultResponseParam, nullptr, i);
				FNiagaraCollisionTrace Trace;
				Trace.CollisionTraceHandle = Handle;
				Trace.SourceParticleIndex = i;
				CollisionTraces.Add(Trace);
			}
		}
	}
}

void FNiagaraCollisionBatch::GenerateEventsFromResults(FNiagaraEmitterInstance *Sim)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraCollision);
	//CollisionEventDataSet.Allocate(CollisionTraceHandles.Num());

	UWorld *SystemWorld = Sim->GetParentSystemInstance()->GetComponent()->GetWorld();
	if (SystemWorld)
	{
		TArray<FNiagaraCollisionEventPayload> Payloads;

		// generate events for last frame's collisions
		//
		for (FNiagaraCollisionTrace CurCheck: CollisionTraces)
		{
			FTraceHandle Handle = CurCheck.CollisionTraceHandle;
			FTraceDatum CurTrace;
			// wait for trace handles; this should block rarely to never
			bool bReady = false;
			while (!bReady)
			{
				bReady = SystemWorld->QueryTraceData(Handle, CurTrace);
				if (!bReady)
				{
					// if the query came back false, it's possible that the hanle is invalid for some reason; skip in that case
					// TODO: handle this more gracefully
					if (!SystemWorld->IsTraceHandleValid(Handle, false))
					{
						break;
					}
					break;
				}
			}

			if (bReady && CurTrace.OutHits.Num())
			{
				// grab the first hit that blocks
				FHitResult *Hit = FHitResult::GetFirstBlockingHit(CurTrace.OutHits);
				if (Hit && Hit->IsValidBlockingHit())
				{
					FNiagaraCollisionEventPayload Event;
					Event.CollisionNormal = Hit->ImpactNormal;
					Event.CollisionPos = Hit->ImpactPoint;
					Event.ParticleIndex = CurTrace.UserData;
					check(!Event.CollisionNormal.ContainsNaN());
					check(Event.CollisionNormal.IsNormalized());
					check(!Event.CollisionPos.ContainsNaN());
					check(!Event.CollisionVelocity.ContainsNaN());

					// TODO add to unique list of physical materials for Blueprint
					Event.PhysicalMaterialIndex = 0;// Hit->PhysMaterial->GetUniqueID();

					Payloads.Add(Event);
				}
			}
		}

		int32 NumInstances = Payloads.Num();
		if (NumInstances)
		{
			// now allocate the data set and write all the event structs
			//
			CollisionEventDataSet->Allocate(NumInstances);
			CollisionEventDataSet->GetDestinationDataChecked().SetNumInstances(NumInstances);
			//FNiagaraVariable ValidVar(FNiagaraTypeDefinition::GetIntDef(), "Valid");
			FNiagaraVariable PosVar(FNiagaraTypeDefinition::GetVec3Def(), "CollisionLocation");
			FNiagaraVariable VelVar(FNiagaraTypeDefinition::GetVec3Def(), "CollisionVelocity");
			FNiagaraVariable NormVar(FNiagaraTypeDefinition::GetVec3Def(), "CollisionNormal");
			FNiagaraVariable PhysMatIdxVar(FNiagaraTypeDefinition::GetIntDef(), "PhysicalMaterialIndex");
			FNiagaraVariable ParticleIndexVar(FNiagaraTypeDefinition::GetIntDef(), "ParticleIndex");
			//FNiagaraDataSetIterator<int32> ValidItr(*CollisionEventDataSet, ValidVar, 0, true);
			FNiagaraDataSetAccessor<FVector> PosItr(*CollisionEventDataSet, PosVar);
			FNiagaraDataSetAccessor<FVector> NormItr(*CollisionEventDataSet, NormVar);
			FNiagaraDataSetAccessor<FVector> VelItr(*CollisionEventDataSet, VelVar);
			FNiagaraDataSetAccessor<int32> PhysMatItr(*CollisionEventDataSet, PhysMatIdxVar);
			FNiagaraDataSetAccessor<int32> ParticleIndexItr(*CollisionEventDataSet, ParticleIndexVar);

			for (int32 i = 0; i < NumInstances; ++i)
			{
				SCOPE_CYCLE_COUNTER(STAT_NiagaraEventWrite);
				FNiagaraCollisionEventPayload& Payload = Payloads[i];

				check(/*ValidItr.IsValid() && */PosItr.IsValid() && VelItr.IsValid() && NormItr.IsValid() && PhysMatItr.IsValid());
				//ValidItr.Set(1);
				PosItr.Set(i, Payload.CollisionPos);
				VelItr.Set(i, Payload.CollisionVelocity);
				NormItr.Set(i, Payload.CollisionNormal);
				ParticleIndexItr.Set(i, Payload.ParticleIndex);
				PhysMatItr.Set(i, 0);
			}
		}
		else
		{
			CollisionEventDataSet->ResetBuffers();
		}

	}
}

int32 FNiagaraDICollisionQueryBatch::SubmitQuery(FVector Position, FVector Direction, float CollisionSize, float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraCollision);
	int32 Ret = INDEX_NONE;
	if (CollisionWorld)
	{
		//int32 TestCollision = *TstIt;
		//if (TestCollision)
		{
			FVector EndPosition = Position + Direction * DeltaSeconds;

			float Length;
			FVector NormDir;
			Direction.ToDirectionAndLength(NormDir, Length);
			Position -= NormDir * (CollisionSize / 2);
			EndPosition += NormDir * (CollisionSize/ 2);

			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiagraAsync));
			QueryParams.OwnerTag = "Niagara";
			QueryParams.bFindInitialOverlaps = false;
			QueryParams.bReturnFaceIndex = false;
			QueryParams.bReturnPhysicalMaterial = true;
			QueryParams.bTraceComplex = false;
			QueryParams.bIgnoreTouches = true;
			FTraceHandle Handle = CollisionWorld->AsyncLineTraceByChannel(EAsyncTraceType::Single, Position, EndPosition, ECollisionChannel::ECC_WorldStatic, QueryParams, FCollisionResponseParams::DefaultResponseParam, nullptr, TraceID);
			FNiagaraCollisionTrace Trace;
			Trace.CollisionTraceHandle = Handle;
			Trace.SourceParticleIndex = TraceID;
			Trace.CollisionSize = CollisionSize;
			Trace.DeltaSeconds = DeltaSeconds;

			int32 TraceIdx = CollisionTraces[GetWriteBufferIdx()].Add(Trace);
			IdToTraceIdx[GetWriteBufferIdx()].Add(TraceID) = TraceIdx;
			Ret = TraceID;
			TraceID++;
		}
	}

	return Ret;
}

int32 FNiagaraDICollisionQueryBatch::SubmitQuery(FVector StartPos, FVector EndPos, ECollisionChannel TraceChannel)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraCollision);
	if (!CollisionWorld)
	{
		return INDEX_NONE;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiagaraAsync));
	QueryParams.OwnerTag = "Niagara";
	QueryParams.bFindInitialOverlaps = false;
	QueryParams.bReturnFaceIndex = false;
	QueryParams.bReturnPhysicalMaterial = true;
	QueryParams.bTraceComplex = false;
	QueryParams.bIgnoreTouches = true;
	FTraceHandle Handle = CollisionWorld->AsyncLineTraceByChannel(EAsyncTraceType::Single, StartPos, EndPos, TraceChannel, QueryParams, FCollisionResponseParams::DefaultResponseParam, nullptr, TraceID);
	FNiagaraCollisionTrace Trace;
	Trace.CollisionTraceHandle = Handle;
	Trace.SourceParticleIndex = TraceID;

	int32 TraceIdx = CollisionTraces[GetWriteBufferIdx()].Add(Trace);
	IdToTraceIdx[GetWriteBufferIdx()].Add(TraceID) = TraceIdx;

	int32 Ret = TraceID;
	TraceID++;
	return Ret;
}

bool FNiagaraDICollisionQueryBatch::PerformQuery(FVector StartPos, FVector EndPos, FNiagaraDICollsionQueryResult &Result, ECollisionChannel TraceChannel)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraCollision);
	if (!CollisionWorld)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiagaraSync));
	QueryParams.OwnerTag = "Niagara";
	QueryParams.bFindInitialOverlaps = false;
	QueryParams.bReturnFaceIndex = false;
	QueryParams.bReturnPhysicalMaterial = true;
	QueryParams.bTraceComplex = false;
	QueryParams.bIgnoreTouches = true;
	FHitResult TraceResult;
	bool ValidHit = CollisionWorld->LineTraceSingleByChannel(TraceResult, StartPos, EndPos, TraceChannel, QueryParams);
	if (ValidHit)
	{
		Result.IsInsideMesh = TraceResult.bStartPenetrating;
		Result.CollisionPos = TraceResult.ImpactPoint;
		Result.CollisionNormal = TraceResult.ImpactNormal;
		if (TraceResult.PhysMaterial.IsValid())
		{
			Result.PhysicalMaterialIdx = TraceResult.PhysMaterial->GetUniqueID();
			Result.Friction = TraceResult.PhysMaterial->Friction;
			Result.Restitution = TraceResult.PhysMaterial->Restitution;
		}
		else
		{
			Result.PhysicalMaterialIdx = -1;
			Result.Friction = 0.0f;
			Result.Restitution = 0.0f;
		}
	}

	return ValidHit;
}

bool FNiagaraDICollisionQueryBatch::GetQueryResult(uint32 InTraceID, FNiagaraDICollsionQueryResult &Result)
{
	int32 *TraceIdxPtr = IdToTraceIdx[GetReadBufferIdx()].Find(InTraceID);
	int32 TraceIdx = INDEX_NONE;

	if (TraceIdxPtr)
	{
		TraceIdx = *TraceIdxPtr;
		check(CollisionTraces[GetReadBufferIdx()].IsValidIndex(TraceIdx));
		FNiagaraCollisionTrace &CurTrace = CollisionTraces[GetReadBufferIdx()][TraceIdx];
		FTraceHandle Handle = CurTrace.CollisionTraceHandle;
		FTraceDatum CurData;
		// wait for trace handles; this should block rarely to never
		bool bReady = CollisionWorld->QueryTraceData(Handle, CurData);
		/*
		if (!bReady)
		{
			// if the query came back false, it's possible that the hanle is invalid for some reason; skip in that case
			// TODO: handle this more gracefully
			if (!CollisionWorld->IsTraceHandleValid(Handle, false))
			{
				break;
			}
			break;
			}
		}
		*/

		if (bReady && CurData.OutHits.Num())
		{
			// grab the first hit that blocks
			FHitResult *Hit = FHitResult::GetFirstBlockingHit(CurData.OutHits);
			if (Hit && Hit->bBlockingHit)
			{
				/*
				FNiagaraCollisionEventPayload Event;
				Event.CollisionNormal = Hit->ImpactNormal;
				Event.CollisionPos = Hit->ImpactPoint;
				Event.CollisionVelocity = CurCheck.OriginalVelocity;
				Event.ParticleIndex = CurData.UserData;
				check(!Event.CollisionNormal.ContainsNaN());
				check(Event.CollisionNormal.IsNormalized());
				check(!Event.CollisionPos.ContainsNaN());
				check(!Event.CollisionVelocity.ContainsNaN());

				// TODO add to unique list of physical materials for Blueprint
				Event.PhysicalMaterialIndex = 0;// Hit->PhysMaterial->GetUniqueID();

				Payloads.Add(Event);
				*/
				Result.IsInsideMesh = Hit->bStartPenetrating;
				Result.CollisionPos = Hit->ImpactPoint;// -NormVel*(CurTrace.CollisionSize / 2);
				Result.CollisionNormal = Hit->ImpactNormal;
				Result.TraceID = InTraceID;
				if (Hit->PhysMaterial.IsValid())
				{
					Result.PhysicalMaterialIdx = Hit->PhysMaterial->GetUniqueID();
					Result.Friction = Hit->PhysMaterial->Friction;
					Result.Restitution = Hit->PhysMaterial->Restitution;
				}
				else
				{
					Result.PhysicalMaterialIdx = -1;
					Result.Friction = 0.0f;
					Result.Restitution = 0.0f;
				}
				return true;
			}
		}
	}

	return false;
}


/*
int32 FNiagaraDICollisionQueryBatch::Tick()
{

}
/*/