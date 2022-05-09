// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGPUSystemTick.h"
#include "NiagaraSystemGpuComputeProxy.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemSimulation.h"

void FNiagaraGPUSystemTick::Init(FNiagaraSystemInstance* InSystemInstance)
{
	ensure(InSystemInstance != nullptr);
	CA_ASSUME(InSystemInstance != nullptr);
	ensure(!InSystemInstance->IsComplete());
	SystemInstanceID = InSystemInstance->GetId();
	SystemGpuComputeProxy = InSystemInstance->GetSystemGpuComputeProxy();
 
	uint32 DataSizeForGPU = InSystemInstance->GPUDataInterfaceInstanceDataSize;

	if (DataSizeForGPU > 0)
	{
		uint32 AllocationSize = DataSizeForGPU;

		DIInstanceData = new FNiagaraComputeDataInterfaceInstanceData;
		DIInstanceData->PerInstanceDataSize = AllocationSize;
		DIInstanceData->PerInstanceDataForRT = FMemory::Malloc(AllocationSize);
		DIInstanceData->Instances = InSystemInstance->DataInterfaceInstanceDataOffsets.Num();

		uint8* InstanceDataBase = (uint8*) DIInstanceData->PerInstanceDataForRT;
		uint32 RunningOffset = 0;

		DIInstanceData->InterfaceProxiesToOffsets.Reserve(InSystemInstance->GPUDataInterfaces.Num());

		for (const auto& Pair : InSystemInstance->GPUDataInterfaces)
		{
			UNiagaraDataInterface* Interface = Pair.Key.Get();
			if (Interface == nullptr)
			{
				continue;
			}

			FNiagaraDataInterfaceProxy* Proxy = Interface->GetProxy();
			const int32 Offset = Pair.Value;

			const int32 RTDataSize = Align(Interface->PerInstanceDataPassedToRenderThreadSize(), 16);
			ensure(RTDataSize > 0);
			check(Proxy);

			void* PerInstanceData = &InSystemInstance->DataInterfaceInstanceData[Offset];

			Interface->ProvidePerInstanceDataForRenderThread(InstanceDataBase, PerInstanceData, SystemInstanceID);

			// @todo rethink this. So ugly.
			DIInstanceData->InterfaceProxiesToOffsets.Add(Proxy, RunningOffset);

			InstanceDataBase += RTDataSize;
			RunningOffset += RTDataSize;
		}
	}

	check(MAX_uint32 > InSystemInstance->ActiveGPUEmitterCount);

	// Layout our packet.
	const uint32 PackedDispatchesSize = InSystemInstance->ActiveGPUEmitterCount * sizeof(FNiagaraComputeInstanceData);
	// We want the Params after the instance data to be aligned so we can upload to the gpu.
	uint32 PackedDispatchesSizeAligned = Align(PackedDispatchesSize, SHADER_PARAMETER_STRUCT_ALIGNMENT);
	uint32 TotalParamSize = InSystemInstance->TotalGPUParamSize;

	uint32 TotalPackedBufferSize = PackedDispatchesSizeAligned + TotalParamSize;

	InstanceData_ParamData_Packed = (uint8*)FMemory::Malloc(TotalPackedBufferSize);

	FNiagaraComputeInstanceData* Instances = (FNiagaraComputeInstanceData*)(InstanceData_ParamData_Packed);
	uint8* ParamDataBufferPtr = InstanceData_ParamData_Packed + PackedDispatchesSizeAligned;

	// we want to include interpolation parameters (current and previous frame) if any of the emitters in the system
	// require it
	const bool IncludeInterpolationParameters = InSystemInstance->GPUParamIncludeInterpolation;
	const int32 InterpFactor = IncludeInterpolationParameters ? 2 : 1;

	GlobalParamData = ParamDataBufferPtr;
	SystemParamData = GlobalParamData + InterpFactor * sizeof(FNiagaraGlobalParameters);
	OwnerParamData = SystemParamData + InterpFactor * sizeof(FNiagaraSystemParameters);

	// actually copy all of the data over, for the system data we only need to do it once (rather than per-emitter)
	FMemory::Memcpy(GlobalParamData, &InSystemInstance->GetGlobalParameters(), sizeof(FNiagaraGlobalParameters));
	FMemory::Memcpy(SystemParamData, &InSystemInstance->GetSystemParameters(), sizeof(FNiagaraSystemParameters));
	FMemory::Memcpy(OwnerParamData, &InSystemInstance->GetOwnerParameters(), sizeof(FNiagaraOwnerParameters));

	if (IncludeInterpolationParameters)
	{
		FMemory::Memcpy(GlobalParamData + sizeof(FNiagaraGlobalParameters), &InSystemInstance->GetGlobalParameters(true), sizeof(FNiagaraGlobalParameters));
		FMemory::Memcpy(SystemParamData + sizeof(FNiagaraSystemParameters), &InSystemInstance->GetSystemParameters(true), sizeof(FNiagaraSystemParameters));
		FMemory::Memcpy(OwnerParamData + sizeof(FNiagaraOwnerParameters), &InSystemInstance->GetOwnerParameters(true), sizeof(FNiagaraOwnerParameters));
	}

	ParamDataBufferPtr = OwnerParamData + InterpFactor * sizeof(FNiagaraOwnerParameters);

	// Now we will generate instance data for every GPU simulation we want to run on the render thread.
	// This is spawn rate as well as DataInterface per instance data and the ParameterData for the emitter.
	// @todo Ideally we would only update DataInterface and ParameterData bits if they have changed.
	uint32 InstanceIndex = 0;
	bool bStartNewOverlapGroup = false;

	const TConstArrayView<FNiagaraEmitterExecutionIndex> EmitterExecutionOrder = InSystemInstance->GetEmitterExecutionOrder();
	for (const FNiagaraEmitterExecutionIndex& EmiterExecIndex : EmitterExecutionOrder)
	{
		// The dependency resolution code does not consider CPU and GPU emitters separately, so the flag which marks the start of a new overlap group can be set on either
		// a CPU or GPU emitter. We must turn on bStartNewOverlapGroup when we encounter the flag, and reset it when we've actually marked a GPU emitter as starting a new group.
		bStartNewOverlapGroup |= EmiterExecIndex.bStartNewOverlapGroup;

		const uint32 EmitterIdx = EmiterExecIndex.EmitterIndex;
		if (FNiagaraEmitterInstance* EmitterInstance = &InSystemInstance->GetEmitters()[EmitterIdx].Get())
		{
			if (EmitterInstance->IsComplete() )
			{
				continue;
			}

			const FVersionedNiagaraEmitterData* EmitterData = EmitterInstance->GetCachedEmitterData();
			FNiagaraComputeExecutionContext* GPUContext = EmitterInstance->GetGPUContext();

			check(EmitterData);

			if (!EmitterData || !GPUContext || EmitterData->SimTarget != ENiagaraSimTarget::GPUComputeSim)
			{
				continue;
			}

			// Handle edge case where an emitter was set to inactive on the first frame by scalability
			// In which case it will never have ticked so we should not execute a GPU tick for this until it becomes active
			// See FNiagaraSystemInstance::Tick_Concurrent for details
			if (EmitterInstance->HasTicked() == false)
			{
				ensure((EmitterInstance->GetExecutionState() == ENiagaraExecutionState::Inactive) || (EmitterInstance->GetExecutionState() == ENiagaraExecutionState::InactiveClear));
				continue;
			}

			FNiagaraComputeInstanceData* InstanceData = new (&Instances[InstanceIndex]) FNiagaraComputeInstanceData;
			InstanceIndex++;

			InstanceData->Context = GPUContext;
			check(GPUContext->MainDataSet);

			InstanceData->SpawnInfo = GPUContext->GpuSpawnInfo_GT;

			// Consume pending reset
			if ( GPUContext->bResetPending_GT )
			{
				InstanceData->bResetData = GPUContext->bResetPending_GT;
				GPUContext->bResetPending_GT = false;

				++GPUContext->ParticleCountReadFence;
			}
			InstanceData->ParticleCountFence = GPUContext->ParticleCountReadFence;

			InstanceData->EmitterParamData = ParamDataBufferPtr;
			ParamDataBufferPtr += InterpFactor * sizeof(FNiagaraEmitterParameters);

			InstanceData->ExternalParamData = ParamDataBufferPtr;
			InstanceData->ExternalParamDataSize = GPUContext->CombinedParamStore.GetPaddedParameterSizeInBytes();
			ParamDataBufferPtr += InstanceData->ExternalParamDataSize;

			// actually copy all of the data over
			FMemory::Memcpy(InstanceData->EmitterParamData, &InSystemInstance->GetEmitterParameters(EmitterIdx), sizeof(FNiagaraEmitterParameters));
			if (IncludeInterpolationParameters)
			{
				FMemory::Memcpy(InstanceData->EmitterParamData + sizeof(FNiagaraEmitterParameters), &InSystemInstance->GetEmitterParameters(EmitterIdx, true), sizeof(FNiagaraEmitterParameters));
			}

			GPUContext->CombinedParamStore.CopyParameterDataToPaddedBuffer(InstanceData->ExternalParamData, InstanceData->ExternalParamDataSize);

			// Calling PostTick will push current -> previous parameters this must be done after copying the parameter data
			GPUContext->PostTick();

			InstanceData->bStartNewOverlapGroup = bStartNewOverlapGroup;
			bStartNewOverlapGroup = false;

			// @todo-threadsafety Think of a better way to do this!
			const TArray<UNiagaraDataInterface*>& DataInterfaces = GPUContext->CombinedParamStore.GetDataInterfaces();
			InstanceData->DataInterfaceProxies.Reserve(DataInterfaces.Num());
			InstanceData->IterationDataInterfaceProxies.Reserve(DataInterfaces.Num());

			for (UNiagaraDataInterface* DI : DataInterfaces)
			{
				FNiagaraDataInterfaceProxy* DIProxy = DI->GetProxy();
				check(DIProxy);
				InstanceData->DataInterfaceProxies.Add(DIProxy);

				if ( FNiagaraDataInterfaceProxyRW* RWProxy = DIProxy->AsIterationProxy() )
				{
					InstanceData->IterationDataInterfaceProxies.Add(RWProxy);
				}
			}

			// Gather number of iterations for each stage, and if the stage should run or not
			InstanceData->bHasMultipleStages = false;
			InstanceData->PerStageInfo.Reserve(GPUContext->SimStageInfo.Num());
			for ( FSimulationStageMetaData& SimStageMetaData : GPUContext->SimStageInfo )
			{
				int32 NumIterations = SimStageMetaData.NumIterations;
				int32 UserElementCount = -1;
				if (SimStageMetaData.ShouldRunStage(InstanceData->bResetData))
				{
					InstanceData->bHasMultipleStages = true;
					if (!SimStageMetaData.NumIterationsBinding.IsNone())
					{
						FNiagaraParameterStore& BoundParamStore = EmitterInstance->GetRendererBoundVariables();
						if ( const uint8* ParameterData = BoundParamStore.GetParameterData(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), SimStageMetaData.NumIterationsBinding)) )
						{
							NumIterations = *reinterpret_cast<const int32*>(ParameterData);
							NumIterations = FMath::Max(NumIterations, 0);
						}
					}
					if (!SimStageMetaData.EnabledBinding.IsNone())
					{
						FNiagaraParameterStore& BoundParamStore = EmitterInstance->GetRendererBoundVariables();
						if (const uint8* ParameterData = BoundParamStore.GetParameterData(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), SimStageMetaData.EnabledBinding)))
						{
							const FNiagaraBool StageEnabled = *reinterpret_cast<const FNiagaraBool*>(ParameterData);
							NumIterations = StageEnabled.GetValue() ? NumIterations : 0;
						}
					}
					if (!SimStageMetaData.ElementCountBinding.IsNone())
					{
						FNiagaraParameterStore& BoundParamStore = EmitterInstance->GetRendererBoundVariables();
						if (const uint8* ParameterData = BoundParamStore.GetParameterData(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), SimStageMetaData.ElementCountBinding)))
						{
							UserElementCount = FMath::Max(*reinterpret_cast<const int32*>(ParameterData), 0);
						}
					}
				}
				else
				{
					NumIterations = 0;
				}

				InstanceData->TotalDispatches += NumIterations;
				InstanceData->PerStageInfo.Emplace(NumIterations, UserElementCount);
			}
			TotalDispatches += InstanceData->TotalDispatches;
		}
	}

	check(InSystemInstance->ActiveGPUEmitterCount == InstanceIndex);
	InstanceCount = InstanceIndex;
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	InstanceDataDebuggingOnly = GetInstances();
#endif
}

void FNiagaraGPUSystemTick::Destroy()
{
	for ( FNiagaraComputeInstanceData& Instance : GetInstances() )
	{
		Instance.Context->ParticleCountWriteFence = Instance.ParticleCountFence;
		Instance.~FNiagaraComputeInstanceData();
	}

	FMemory::Free(InstanceData_ParamData_Packed);
	if (DIInstanceData)
	{
		FMemory::Free(DIInstanceData->PerInstanceDataForRT);
		delete DIInstanceData;
	}
}

FUniformBufferRHIRef FNiagaraGPUSystemTick::GetUniformBuffer(EUniformBufferType Type, const FNiagaraComputeInstanceData* Instance, bool Current) const
{
	const int32 InterpOffset = Current
		? 0
		: (UBT_NumSystemTypes + InstanceCount * UBT_NumInstanceTypes);

	if (Instance)
	{
		check(Type >= UBT_FirstInstanceType);
		check(Type < UBT_NumTypes);

		const int32 InstanceTypeIndex = Type - UBT_FirstInstanceType;

		const int32 InstanceIndex = (Instance - GetInstances().GetData());
		return UniformBuffers[InterpOffset + UBT_NumSystemTypes + InstanceCount * InstanceTypeIndex + InstanceIndex];
	}

	check(Type >= UBT_FirstSystemType);
	check(Type < UBT_FirstInstanceType);

	return UniformBuffers[InterpOffset + Type];
}

const uint8* FNiagaraGPUSystemTick::GetUniformBufferSource(EUniformBufferType Type, const FNiagaraComputeInstanceData* Instance, bool Current) const
{
	check(Type >= UBT_FirstSystemType);
	check(Type < UBT_NumTypes);

	switch (Type)
	{
		case UBT_Global:
			return GlobalParamData + (Current ? 0 : sizeof(FNiagaraGlobalParameters));
		case UBT_System:
			return SystemParamData + (Current ? 0 : sizeof(FNiagaraSystemParameters));
		case UBT_Owner:
			return OwnerParamData + (Current ? 0 : sizeof(FNiagaraOwnerParameters));
		case UBT_Emitter:
		{
			check(Instance);
			return Instance->EmitterParamData + (Current ? 0 : sizeof(FNiagaraEmitterParameters));
		}
		case UBT_External:
		{
			// External parameters are pushed from the combined parameters store, split into two where first half is current second is previous
			check(Instance && Instance->Context);
			check((Current ? 0 : Instance->Context->ExternalCBufferLayout->ConstantBufferSize) + Instance->Context->ExternalCBufferLayout->ConstantBufferSize <= Instance->ExternalParamDataSize);

			return Instance->ExternalParamData + (Current ? 0 : Instance->Context->ExternalCBufferLayout->ConstantBufferSize);
		}
	}

	return nullptr;
}
