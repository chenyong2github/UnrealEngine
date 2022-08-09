// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "NiagaraScript.h"
#include "NiagaraSimCache.generated.h"

class UNiagaraComponent;

USTRUCT()
struct FNiagaraSimCacheDataBuffers
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 NumInstances = 0;

	UPROPERTY()
	TArray<uint8> FloatData;

	UPROPERTY()
	TArray<uint8> HalfData;

	UPROPERTY()
	TArray<uint8> Int32Data;

	UPROPERTY()
	TArray<int32> IDToIndexTable;

	UPROPERTY()
	uint32 IDAcquireTag = 0;
};

USTRUCT()
struct FNiagaraSimCacheEmitterFrame
{
	GENERATED_BODY()

	//-TODO: We may not require these
	UPROPERTY()
	FBox LocalBounds = FBox(EForceInit::ForceInit);

	UPROPERTY()
	int32 TotalSpawnedParticles = 0;

	UPROPERTY()
	FNiagaraSimCacheDataBuffers ParticleDataBuffers;
};

USTRUCT()
struct FNiagaraSimCacheSystemFrame
{
	GENERATED_BODY()
		
	UPROPERTY()
	FBox LocalBounds = FBox(EForceInit::ForceInit);

	UPROPERTY()
	FNiagaraSimCacheDataBuffers SystemDataBuffers;
};

USTRUCT()
struct FNiagaraSimCacheFrame
{
	GENERATED_BODY()

	UPROPERTY()
	FNiagaraSimCacheSystemFrame SystemData;

	UPROPERTY()
	TArray<FNiagaraSimCacheEmitterFrame> EmitterData;
};

USTRUCT()
struct FNiagaraSimCacheVariable
{
	GENERATED_BODY()

	UPROPERTY()
	FNiagaraVariableBase Variable;

	UPROPERTY()
	uint16 FloatOffset = INDEX_NONE;

	UPROPERTY()
	uint16 FloatCount = 0;

	UPROPERTY()
	uint16 HalfOffset = INDEX_NONE;

	UPROPERTY()
	uint16 HalfCount = 0;

	UPROPERTY()
	uint16 Int32Offset = INDEX_NONE;

	UPROPERTY()
	uint16 Int32Count = 0;
};

USTRUCT()
struct FNiagaraSimCacheDataBuffersLayout
{
	GENERATED_BODY()

	UPROPERTY()
	FName LayoutName;

	UPROPERTY()
	ENiagaraSimTarget SimTarget = ENiagaraSimTarget::CPUSim;

	UPROPERTY()
	TArray<FNiagaraSimCacheVariable> Variables;

	UPROPERTY()
	uint16 FloatCount = 0;

	UPROPERTY()
	uint16 HalfCount = 0;

	UPROPERTY()
	uint16 Int32Count = 0;

	UPROPERTY(transient)
	TArray<uint16> ComponentMappingsToDataBuffer;

	UPROPERTY(transient)
	TArray<uint16> ComponentMappingsFromDataBuffer;
};

USTRUCT()
struct FNiagaraSimCacheLayout
{
	GENERATED_BODY()

	UPROPERTY()
	FNiagaraSimCacheDataBuffersLayout SystemLayout;

	UPROPERTY()
	TArray<FNiagaraSimCacheDataBuffersLayout> EmitterLayouts;
};

UCLASS(Experimental)
class NIAGARA_API UNiagaraSimCache : public UObject
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(VisibleAnywhere, Category=SimCache, meta=(DisplayName="Niagara System"))
	TSoftObjectPtr<UNiagaraSystem> SoftNiagaraSystem;

	UPROPERTY(VisibleAnywhere, Category=SimCache)
	float StartSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, Category=SimCache)
	float DurationSeconds = 0.0f;

	UPROPERTY(transient)
	bool bNeedsReadComponentMappingRecache = true;

#if WITH_EDITORONLY_DATA
	UPROPERTY(transient)
	TArray<FNiagaraVMExecutableDataId> CachedScriptVMIds;
#endif

	UPROPERTY()
	FNiagaraSimCacheLayout CacheLayout;

	UPROPERTY()
	TArray<FNiagaraSimCacheFrame> CacheFrames;

	UPROPERTY()
	TMap<FNiagaraVariableBase, TObjectPtr<UObject>> DataInterfaceStorage;

	// UObject Interface
	virtual bool IsReadyForFinishDestroy() override;
	// UObject Interface

	bool IsCacheValid() const { return SoftNiagaraSystem.IsNull() == false; }

	void BeginWrite(UNiagaraComponent* NiagaraComponent);
	void WriteFrame(UNiagaraComponent* NiagaraComponent);
	void EndWrite();

	bool CanRead(UNiagaraSystem* NiagaraSystem);
	bool Read(float TimeSeconds, FNiagaraSystemInstance* SystemInstance) const;
	bool ReadFrame(int32 FrameIndex, float FrameFraction, FNiagaraSystemInstance* SystemInstance) const;

	/*
	Create a simulation cache from the component's current data.
	The cache can be applied to a component to warmup the system, for example, or can be used to debug the system.
	*/
	static UNiagaraSimCache* CreateSingleFrame(UObject* OuterObject, UNiagaraComponent* NiagaraComponent);

private:
	mutable std::atomic<int32> PendingCommandsInFlight;
};
