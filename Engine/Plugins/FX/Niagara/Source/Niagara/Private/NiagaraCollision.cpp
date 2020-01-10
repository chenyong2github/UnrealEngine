// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCollision.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraStats.h"
#include "NiagaraComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

DECLARE_CYCLE_STAT(TEXT("Collision"), STAT_NiagaraCollision, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Event Emission"), STAT_NiagaraEventWrite, STATGROUP_Niagara);

const FName FNiagaraDICollisionQueryBatch::CollisionTagName = FName("Niagara");

void FNiagaraDICollisionQueryBatch::DispatchQueries()
{
	check(IsInGameThread());

	SCOPE_CYCLE_COUNTER(STAT_NiagaraCollision);

	// swap the buffers, and use our new read buffer for issuing all of our accumulated queries now that we're on the main thread
	FlipBuffers();

	// locks are not used here because we assume that the per instance ticking is done from the main thread
	if (CollisionWorld)
	{
		const int32 ReadBufferIdx = GetReadBufferIdx();
		const int32 TraceCount = CollisionTraces[ReadBufferIdx].Num();

		for (int32 TraceIt = 0; TraceIt < TraceCount; ++TraceIt)
		{
			FNiagaraCollisionTrace& CollisionTrace = CollisionTraces[ReadBufferIdx][TraceIt];

			CollisionTrace.CollisionTraceHandle = CollisionWorld->AsyncLineTraceByChannel(
				EAsyncTraceType::Single,
				CollisionTrace.StartPos,
				CollisionTrace.EndPos,
				CollisionTrace.Channel,
				CollisionTrace.CollisionQueryParams,
				FCollisionResponseParams::DefaultResponseParam,
				nullptr,
				TraceIt);
		}
	}
}

void FNiagaraDICollisionQueryBatch::CollectResults()
{
	check(IsInGameThread());

	SCOPE_CYCLE_COUNTER(STAT_NiagaraCollision);

	// locks are not used here because we assume that the per instance ticking is done from the main thread
	if (CollisionWorld)
	{
		const int32 ReadBufferIdx = GetReadBufferIdx();
		const int32 TraceCount = CollisionTraces[ReadBufferIdx].Num();

		CollisionResults.Reset(TraceCount);

		for (int32 TraceIt = 0; TraceIt < TraceCount; ++TraceIt)
		{
			FNiagaraCollisionTrace& CollisionTrace = CollisionTraces[ReadBufferIdx][TraceIt];

			FTraceDatum TraceResult;
			const bool TraceReady = CollisionWorld->QueryTraceData(CollisionTrace.CollisionTraceHandle, TraceResult);

			if (TraceReady && TraceResult.OutHits.Num())
			{
				FHitResult* Hit = FHitResult::GetFirstBlockingHit(TraceResult.OutHits);
				if (Hit && Hit->bBlockingHit)
				{
					CollisionTrace.HitIndex = CollisionResults.AddUninitialized();
					FNiagaraDICollsionQueryResult& Result = CollisionResults[CollisionTrace.HitIndex];

					Result.IsInsideMesh = Hit->bStartPenetrating;
					Result.CollisionPos = Hit->ImpactPoint;// -NormVel*(CurTrace.CollisionSize / 2);
					Result.CollisionNormal = Hit->ImpactNormal;
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
				}
			}
		}
	}
}

int32 FNiagaraDICollisionQueryBatch::SubmitQuery(FVector StartPos, FVector Direction, float CollisionSize, float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraCollision);

	FVector EndPos = StartPos + Direction * DeltaSeconds;

	float Length;
	FVector NormDir;
	Direction.ToDirectionAndLength(NormDir, Length);
	StartPos -= NormDir * (CollisionSize / 2);
	EndPos += NormDir * (CollisionSize / 2);

	FCollisionQueryParams CollisionQueryParams;
	CollisionQueryParams.OwnerTag = CollisionTagName;
	CollisionQueryParams.bFindInitialOverlaps = false;
	CollisionQueryParams.bReturnFaceIndex = false;
	CollisionQueryParams.bReturnPhysicalMaterial = true;
	CollisionQueryParams.bTraceComplex = false;
	CollisionQueryParams.bIgnoreTouches = true;

	int32 TraceIdx = INDEX_NONE;

	if (Length > SMALL_NUMBER)
	{
		FRWScopeLock WriteScope(CollisionTraceLock, SLT_Write);

		const int32 WriteBufferIdx = GetWriteBufferIdx();

		TraceIdx = CollisionTraces[WriteBufferIdx].Emplace(StartPos, EndPos, ECC_WorldStatic, CollisionQueryParams);
	}

	return TraceIdx;
}

int32 FNiagaraDICollisionQueryBatch::SubmitQuery(FVector StartPos, FVector EndPos, ECollisionChannel TraceChannel)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraCollision);
	
	FCollisionQueryParams CollisionQueryParams;
	CollisionQueryParams.OwnerTag = CollisionTagName;
	CollisionQueryParams.bFindInitialOverlaps = false;
	CollisionQueryParams.bReturnFaceIndex = false;
	CollisionQueryParams.bReturnPhysicalMaterial = true;
	CollisionQueryParams.bTraceComplex = false;
	CollisionQueryParams.bIgnoreTouches = true;

	int32 TraceIdx = INDEX_NONE;

	if ((EndPos - StartPos).SizeSquared() > SMALL_NUMBER)
	{
		FRWScopeLock WriteScope(CollisionTraceLock, SLT_Write);

		const int32 WriteBufferIdx = GetWriteBufferIdx();

		TraceIdx = CollisionTraces[WriteBufferIdx].Emplace(StartPos, EndPos, TraceChannel, CollisionQueryParams);
	}

	return TraceIdx;
}

// Work has been done on the collision world side of things to support synchronous queries from multiple threads
bool FNiagaraDICollisionQueryBatch::PerformQuery(FVector StartPos, FVector EndPos, FNiagaraDICollsionQueryResult &Result, ECollisionChannel TraceChannel)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraCollision);
	if (!CollisionWorld)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiagaraSync));
	QueryParams.OwnerTag = CollisionTagName;
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
	const int32 ReadBufferIdx = GetReadBufferIdx();

	if (CollisionTraces[ReadBufferIdx].IsValidIndex(InTraceID))
	{
		const int32 ResultIndex = CollisionTraces[ReadBufferIdx][InTraceID].HitIndex;

		if (CollisionResults.IsValidIndex(ResultIndex))
		{
			Result = CollisionResults[ResultIndex];
			return true;
		}
	}

	return false;
}