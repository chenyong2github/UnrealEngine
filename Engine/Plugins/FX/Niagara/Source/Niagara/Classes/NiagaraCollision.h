// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEvents.h"
#include "WorldCollision.h"

UENUM()
enum class ENiagaraCollisionMode : uint8
{
	None = 0,
	SceneGeometry,
	DepthBuffer,
	DistanceField
};


struct FNiagaraCollisionTrace
{
	FTraceHandle CollisionTraceHandle;
	uint32 SourceParticleIndex;
	float CollisionSize;
	float DeltaSeconds;
};


struct FNiagaraDICollsionQueryResult
{
	uint32 TraceID;
	FVector CollisionPos;
	FVector CollisionNormal;
	FVector CollisionVelocity;
	int32 PhysicalMaterialIdx;
	float Friction;
	float Restitution;
	bool IsInsideMesh;
};

class FNiagaraDICollisionQueryBatch
{
public:
	FNiagaraDICollisionQueryBatch()
		: BatchID(NAME_None)
		, CurrBuffer(0)
	{
	}

	~FNiagaraDICollisionQueryBatch()
	{
	}

	int32 GetWriteBufferIdx()
	{
		return CurrBuffer;
	}

	int32 GetReadBufferIdx()
	{
		return CurrBuffer ^ 0x1;
	}

	void Tick(ENiagaraSimTarget Target)
	{
		CurrBuffer = CurrBuffer ^ 0x1;
	}

	void ClearWrite()
	{
		uint32 PrevNum = CollisionTraces[CurrBuffer].Num();
		CollisionTraces[CurrBuffer].Empty(PrevNum);
		IdToTraceIdx[CurrBuffer].Empty(PrevNum);
	}


	void Init(FNiagaraSystemInstanceID InBatchID, UWorld *InCollisionWorld)
	{
		BatchID = InBatchID;
		CollisionWorld = InCollisionWorld;
		CollisionTraces[0].Empty();
		CollisionTraces[1].Empty();
		IdToTraceIdx[0].Empty();
		IdToTraceIdx[1].Empty();
		CurrBuffer = 0;
		TraceID = 0;
	}

	int32 SubmitQuery(FVector Position, FVector Direction, float CollisionSize, float DeltaSeconds);
	int32 SubmitQuery(FVector StartPos, FVector EndPos, ECollisionChannel TraceChannel);
	bool PerformQuery(FVector StartPos, FVector EndPos, FNiagaraDICollsionQueryResult &Result, ECollisionChannel TraceChannel);
	bool GetQueryResult(uint32 TraceID, FNiagaraDICollsionQueryResult &Result);

private:
	//TArray<FTraceHandle> CollisionTraceHandles;

	TArray<FNiagaraCollisionEventPayload> CollisionEvents;
	FNiagaraDataSet *CollisionEventDataSet;

	FNiagaraSystemInstanceID BatchID;
	TArray<FNiagaraCollisionTrace> CollisionTraces[2];
	TMap<uint32, int32> IdToTraceIdx[2];
	uint32 CurrBuffer;
	uint32 TraceID;
	UWorld *CollisionWorld;
};