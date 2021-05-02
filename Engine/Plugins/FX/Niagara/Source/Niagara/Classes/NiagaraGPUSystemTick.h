// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraComputeExecutionContext.h"
#include "RHIGPUReadback.h"

struct FNiagaraComputeDataInterfaceInstanceData
{
	void* PerInstanceDataForRT = nullptr;
	TMap<FNiagaraDataInterfaceProxy*, int32> InterfaceProxiesToOffsets;
	uint32 PerInstanceDataSize = 0;
	uint32 Instances = 0;
};

struct FNiagaraComputeInstanceData
{
	FNiagaraComputeInstanceData()
	{
		bResetData = false;
		bStartNewOverlapGroup = false;
		bUsesSimStages = false;
		bUsesOldShaderStages = false;
	}

	FNiagaraGpuSpawnInfo SpawnInfo;
	uint8* EmitterParamData = nullptr;
	uint8* ExternalParamData = nullptr;
	FNiagaraComputeExecutionContext* Context = nullptr;
	TArray<FNiagaraDataInterfaceProxy*> DataInterfaceProxies;
	TArray<FNiagaraDataInterfaceProxyRW*> IterationDataInterfaceProxies;
	uint32 ParticleCountFence = INDEX_NONE;
	uint32 bResetData : 1;
	uint32 bStartNewOverlapGroup : 1;
	uint32 bUsesSimStages : 1;
	uint32 bUsesOldShaderStages : 1;

	bool IsOutputStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const;
	bool IsIterationStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const;
	FNiagaraDataInterfaceProxyRW* FindIterationInterface(uint32 SimulationStageIndex) const;
};

/*
	Represents all the information needed to dispatch a single tick of a FNiagaraSystemInstance.
	This object will be created on the game thread and passed to the renderthread.

	It contains the PerInstance data buffer for every DataInterface referenced by the system as well
	as the Data required to dispatch updates for each Emitter in the system.

	DataInterface data is packed tightly. It includes a TMap that associates the data interface with
	the offset into the packed buffer. At that offset is the Per-Instance data for this System.

	InstanceData_ParamData_Packed packs FNiagaraComputeInstanceData and ParamData into one buffer.
	There is padding after the array of FNiagaraComputeInstanceData so we can upload ParamData directly into a UniformBuffer
	(it is 16 byte aligned).

*/
class FNiagaraGPUSystemTick
{
public:
	void Init(FNiagaraSystemInstance* InSystemInstance);
	void Destroy();

	FORCEINLINE TArrayView<FNiagaraComputeInstanceData> GetInstances() const
	{
		return MakeArrayView(reinterpret_cast<FNiagaraComputeInstanceData*>(InstanceData_ParamData_Packed), InstanceCount);
	};

	enum EUniformBufferType
	{
		UBT_FirstSystemType = 0,
		UBT_Global = UBT_FirstSystemType,
		UBT_System,
		UBT_Owner,
		UBT_NumSystemTypes,

		UBT_FirstInstanceType = UBT_NumSystemTypes,
		UBT_Emitter = UBT_FirstInstanceType,
		UBT_External,

		UBT_NumTypes,

		UBT_NumInstanceTypes = UBT_NumTypes - UBT_NumSystemTypes,
	};

	FUniformBufferRHIRef GetUniformBuffer(EUniformBufferType Type, const FNiagaraComputeInstanceData* InstanceData, bool Previous) const;
	const uint8* GetUniformBufferSource(EUniformBufferType Type, const FNiagaraComputeInstanceData* InstanceData, bool Previous) const;

public:
	// Transient data used by the RT
	TArray<FUniformBufferRHIRef> UniformBuffers;

	// data assigned by GT
	FNiagaraSystemInstanceID SystemInstanceID = 0LL;						//-TODO: Remove?
	class FNiagaraSystemGpuComputeProxy* SystemGpuComputeProxy = nullptr;	//-TODO: Can we remove this?
	FNiagaraComputeDataInterfaceInstanceData* DIInstanceData = nullptr;
	uint8* InstanceData_ParamData_Packed = nullptr;
	uint8* GlobalParamData = nullptr;
	uint8* SystemParamData = nullptr;
	uint8* OwnerParamData = nullptr;
	uint32 InstanceCount = 0;
	uint32 TotalDispatches = 0;
	uint32 NumInstancesWithSimStages = 0;									//-TODO: Remove me
	bool bIsFinalTick = false;

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	// Debugging only
	TConstArrayView<FNiagaraComputeInstanceData> InstanceDataDebuggingOnly;
#endif
};
