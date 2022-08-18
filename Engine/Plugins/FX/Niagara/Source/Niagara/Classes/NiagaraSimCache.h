// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "NiagaraScript.h"
#include "NiagaraSimCache.generated.h"

class UNiagaraComponent;

USTRUCT()
struct FNiagaraSimCacheCreateParameters
{
	GENERATED_BODY()

	FNiagaraSimCacheCreateParameters()
		: bAllowRebasing(true)
		, bAllowDataInterfaceCaching(true)
	{
	}

	/**
	When enabled allows the SimCache to be re-based.
	i.e. World space emitters can be moved to the new component's location
	*/
	UPROPERTY(EditAnywhere, Category="SimCache")
	uint32 bAllowRebasing : 1;

	/**
	When enabled Data Interface data will be stored in the SimCache.
	This can result in a large increase to the cache size, depending on what Data Interfaces are used
	*/
	UPROPERTY(EditAnywhere, Category="SimCache")
	uint32 bAllowDataInterfaceCaching : 1;

	/**
	When enabled the SimCache will only be useful for rendering a replay, it can not be used to restart
	the simulation from as only attributes & data interface that impact rendering will be stored.
	This should result in a much smaller caches.
	*/
	//UPROPERTY(EditAnywhere, Category="SimCache")
	//uint32 bRenderOnly : 1;

	/**
	List of Attributes to force include in the SimCache rebase, they should be the full path to the attribute
	For example, MyEmitter.Particles.MyQuat would force the particle attribute MyQuat to be included for MyEmitter
	*/
	UPROPERTY(EditAnywhere, Category = "SimCache")
	TArray<FName> RebaseIncludeList;

	/**
	List of Attributes to force exclude from the SimCache rebase, they should be the full path to the attribute
	For example, MyEmitter.Particles.MyQuat would force the particle attribute MyQuat to be included for MyEmitter
	*/
	UPROPERTY(EditAnywhere, Category = "SimCache")
	TArray<FName> RebaseExcludeList;
};

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
	FTransform LocalToWorld;

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

	// Copy Function parameters are
	// Dest, DestStride, Source, SourceStride, NumInstances, RebasedTransform
	typedef void (*FVariableCopyFunction)(uint8*, uint32, const uint8*, uint32, uint32, const FTransform&);

	struct FVariableCopyInfo
	{
		FVariableCopyInfo() = default;
		explicit FVariableCopyInfo(uint16 InComponentFrom, uint16 InComponentTo, FVariableCopyFunction InCopyFunc)
			: ComponentFrom(InComponentFrom)
			, ComponentTo(InComponentTo)
			, CopyFunc(InCopyFunc)
		{
		}

		uint16					ComponentFrom = 0;
		uint16					ComponentTo = 0;
		FVariableCopyFunction	CopyFunc;
	};

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

	UPROPERTY()
	TArray<FName> RebaseVariableNames;

	TArray<uint16> ComponentMappingsToDataBuffer;
	TArray<FVariableCopyInfo> VariableMappingsToDataBuffer;

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

	UPROPERTY()
	FNiagaraSimCacheCreateParameters CreateParameters;

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

	void BeginWrite(FNiagaraSimCacheCreateParameters InCreateParameters, UNiagaraComponent* NiagaraComponent);
	void WriteFrame(UNiagaraComponent* NiagaraComponent);
	void EndWrite();

	bool CanRead(UNiagaraSystem* NiagaraSystem);
	bool Read(float TimeSeconds, FNiagaraSystemInstance* SystemInstance) const;
	bool ReadFrame(int32 FrameIndex, float FrameFraction, FNiagaraSystemInstance* SystemInstance) const;

private:
	mutable std::atomic<int32> PendingCommandsInFlight;
};
