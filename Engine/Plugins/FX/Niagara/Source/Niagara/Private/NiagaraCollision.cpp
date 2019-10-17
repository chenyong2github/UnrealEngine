// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraCollision.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraStats.h"
#include "NiagaraComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

DECLARE_CYCLE_STAT(TEXT("Collision"), STAT_NiagaraCollision, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Event Emission"), STAT_NiagaraEventWrite, STATGROUP_Niagara);


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
	static FName NiagaraName("Niagara");
	QueryParams.OwnerTag = NiagaraName;
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