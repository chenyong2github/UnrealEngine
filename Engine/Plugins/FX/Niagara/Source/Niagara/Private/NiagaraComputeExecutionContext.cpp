// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraComputeExecutionContext.h"
#include "NiagaraStats.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraDataInterface.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemGpuComputeProxy.h"
#include "NiagaraWorldManager.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraGPUInstanceCountManager.h"
#include "NiagaraGPUSystemTick.h"
#include "NiagaraDataInterfaceRW.h"

FNiagaraComputeExecutionContext::FNiagaraComputeExecutionContext()
	: MainDataSet(nullptr)
	, GPUScript(nullptr)
	, GPUScript_RT(nullptr)
{
	ExternalCBufferLayout = new FNiagaraRHIUniformBufferLayout(TEXT("Niagara GPU External CBuffer"));
}

FNiagaraComputeExecutionContext::~FNiagaraComputeExecutionContext()
{
	// EmitterInstanceReadback.GPUCountOffset should be INDEX_NONE at this point to ensure the index is reused.
	// When the batcher is being destroyed though, we don't free the index, but this would not be leaking.
	// check(EmitterInstanceReadback.GPUCountOffset == INDEX_NONE);
	SetDataToRender(nullptr);

	ExternalCBufferLayout = nullptr;
}

void FNiagaraComputeExecutionContext::Reset(NiagaraEmitterInstanceBatcher* Batcher)
{
	NiagaraEmitterInstanceBatcher* RT_Batcher = Batcher && !Batcher->IsPendingKill() ? Batcher : nullptr;
	ENQUEUE_RENDER_COMMAND(ResetRT)(
		[RT_Batcher, RT_Context=this](FRHICommandListImmediate& RHICmdList)
		{
			RT_Context->ResetInternal(RT_Batcher);
		}
	);
}

void FNiagaraComputeExecutionContext::InitParams(UNiagaraScript* InGPUComputeScript, ENiagaraSimTarget InSimTarget, const uint32 InDefaultSimulationStageIndex, const int32 InMaxUpdateIterations, const TSet<uint32> InSpawnStages)
{
	GPUScript = InGPUComputeScript;
	CombinedParamStore.InitFromOwningContext(InGPUComputeScript, InSimTarget, true);
	DefaultSimulationStageIndex = InDefaultSimulationStageIndex;
	MaxUpdateIterations = InMaxUpdateIterations;
	SpawnStages.Empty();

	SpawnStages.Append(InSpawnStages);
	
	HasInterpolationParameters = GPUScript && GPUScript->GetComputedVMCompilationId().HasInterpolatedParameters();

	if (InGPUComputeScript)
	{
		FNiagaraVMExecutableData& VMData = InGPUComputeScript->GetVMExecutableData();
		if (VMData.IsValid() && VMData.SimulationStageMetaData.Num() > 0)
		{
			SimStageInfo = VMData.SimulationStageMetaData;

			int32 FoundMaxUpdateIterations = SimStageInfo[SimStageInfo.Num() - 1].MaxStage;

			// Some useful debugging code should we need to look up differences between old and new
			const bool bDebugSimStages = false;
			if (bDebugSimStages)
			{
				UE_LOG(LogNiagara, Log, TEXT("Stored vs:"));
				bool bPass = FoundMaxUpdateIterations == MaxUpdateIterations;
				UE_LOG(LogNiagara, Log, TEXT("MaxUpdateIterations: %d vs %d %s"), FoundMaxUpdateIterations, MaxUpdateIterations, bPass ? TEXT("Pass") : TEXT("FAIL!!!!!!!!"));

				int32 NumSpawnFound = 0;
				bool bMatchesFound = true;
				for (int32 i = 0; i < SimStageInfo.Num(); i++)
				{
					if (SimStageInfo[i].bSpawnOnly)
					{
						NumSpawnFound++;

						if (!SpawnStages.Contains(SimStageInfo[i].MinStage))
						{
							bMatchesFound = false;
							UE_LOG(LogNiagara, Log, TEXT("Missing spawn stage: %d FAIL!!!!!!!!!"), SimStageInfo[i].MinStage);
						}
					}
				}

				bPass = SpawnStages.Num() == NumSpawnFound;
				UE_LOG(LogNiagara, Log, TEXT("SpawnStages.Num(): %d vs %d %s"), NumSpawnFound, SpawnStages.Num(), bPass ? TEXT("Pass") : TEXT("FAIL!!!!!!!!"));

				TArray<FNiagaraVariable> Params;
				CombinedParamStore.GetParameters(Params);
				for (FNiagaraVariable& Var : Params)
				{
					if (!Var.IsDataInterface())
						continue;

					UNiagaraDataInterface* DI = CombinedParamStore.GetDataInterface(Var);
					UNiagaraDataInterfaceRWBase* DIRW = Cast<UNiagaraDataInterfaceRWBase>(DI);
					if (DIRW)
					{
						for (int32 i = 0; i < SimStageInfo.Num(); i++)
						{
							if (SimStageInfo[i].IterationSource == Var.GetName())
							{
								if (!DIRW->IterationShaderStages.Contains(SimStageInfo[i].MinStage))
								{
									UE_LOG(LogNiagara, Log, TEXT("Missing iteration stage for %s: %d FAIL!!!!!!!!!"), *Var.GetName().ToString(), SimStageInfo[i].MinStage);
								}
							}

							if (SimStageInfo[i].OutputDestinations.Contains(Var.GetName()))
							{
								if (!DIRW->OutputShaderStages.Contains(SimStageInfo[i].MinStage))
								{
									UE_LOG(LogNiagara, Log, TEXT("Missing output stage for %s: %d FAIL!!!!!!!!!"), *Var.GetName().ToString(), SimStageInfo[i].MinStage);
								}
							}
						}
					}

				}
			}


			// Set the values that we are using from compiled data instead...
			MaxUpdateIterations = SimStageInfo[SimStageInfo.Num() - 1].MaxStage;
			SpawnStages.Empty();

			for (int32 i = 0; i < SimStageInfo.Num(); i++)
			{
				if (SimStageInfo[i].bSpawnOnly)
				{
					SpawnStages.Add(SimStageInfo[i].MinStage);
				}
			}

			
		}
	}

	
#if DO_CHECK
	// DI Parameters are the same between all shader permutations so we can just get the first one
	FNiagaraShaderRef Shader = InGPUComputeScript->GetRenderThreadScript()->GetShaderGameThread(0);
	if (Shader.IsValid())
	{
		DIClassNames.Empty(Shader->GetDIParameters().Num());
		for (const FNiagaraDataInterfaceParamRef& DIParams : Shader->GetDIParameters())
		{
			DIClassNames.Add(DIParams.DIType.Get(Shader.GetPointerTable().DITypes)->GetClass()->GetName());
		}
	}
	else
	{
		DIClassNames.Empty(InGPUComputeScript->GetRenderThreadScript()->GetDataInterfaceParamInfo().Num());
		for (const FNiagaraDataInterfaceGPUParamInfo& DIParams : InGPUComputeScript->GetRenderThreadScript()->GetDataInterfaceParamInfo())
		{
			DIClassNames.Add(DIParams.DIClassName);
		}
	}
#endif
}


const FSimulationStageMetaData* FNiagaraComputeExecutionContext::GetSimStageMetaData(uint32 SimulationStageIndex) const
{
	if (SimStageInfo.Num() > 0)
	{
		for (int32 i = 0; i < SimStageInfo.Num(); i++)
		{
			if (SimulationStageIndex >= (uint32)SimStageInfo[i].MinStage  && SimulationStageIndex < (uint32)SimStageInfo[i].MaxStage)
			{
				return &SimStageInfo[i];
			}
		}
	}
	return nullptr;
}

bool FNiagaraComputeExecutionContext::IsOutputStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const
{
	const FSimulationStageMetaData* MetaData = GetSimStageMetaData(CurrentStage);
	if (MetaData && DIProxy && !DIProxy->SourceDIName.IsNone())
	{
		if (MetaData->OutputDestinations.Contains(DIProxy->SourceDIName))
			return true;
	}
	else if (DIProxy && SimStageInfo.Num() == 0)
	{
		return DIProxy->IsOutputStage_DEPRECATED(CurrentStage);
	}
	return false;
}

bool FNiagaraComputeExecutionContext::IsIterationStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const
{
	const FSimulationStageMetaData* MetaData = GetSimStageMetaData(CurrentStage);
	if (MetaData && DIProxy && !DIProxy->SourceDIName.IsNone())
	{
		if (MetaData->IterationSource.IsNone()) // Per particle iteration...
			return false;

		if (MetaData->IterationSource == DIProxy->SourceDIName)
			return true;
	}
	else if (DIProxy && SimStageInfo.Num() == 0)
	{
		return DIProxy->IsIterationStage_DEPRECATED(CurrentStage);
	}
	return false;
}

FNiagaraDataInterfaceProxyRW* FNiagaraComputeExecutionContext::FindIterationInterface(const TArray<FNiagaraDataInterfaceProxyRW*>& InProxies, uint32 CurrentStage) const
{
	const FSimulationStageMetaData* MetaData = GetSimStageMetaData(CurrentStage);
	if (MetaData)
	{
		if (MetaData->IterationSource.IsNone()) // Per particle iteration...
			return nullptr;

		for (FNiagaraDataInterfaceProxyRW* Proxy : InProxies)
		{
			if (Proxy->SourceDIName == MetaData->IterationSource)
				return Proxy;
		}

		UE_LOG(LogNiagara, Verbose, TEXT("FNiagaraComputeExecutionContext::FindIterationInterface could not find IterationInterface %s"), *MetaData->IterationSource.ToString());

		return nullptr;
	}
	else if (SimStageInfo.Num() == 0)
	{
		// Fallback to old shader stages
		for (FNiagaraDataInterfaceProxyRW* Proxy : InProxies)
		{
			if (Proxy->IsIterationStage_DEPRECATED(CurrentStage))
				return Proxy;
		}
	}

	return nullptr;
}

void FNiagaraComputeExecutionContext::DirtyDataInterfaces()
{
	CombinedParamStore.MarkInterfacesDirty();
}

bool FNiagaraComputeExecutionContext::Tick(FNiagaraSystemInstance* ParentSystemInstance)
{
	if (CombinedParamStore.GetInterfacesDirty())
	{
#if DO_CHECK
		const TArray<UNiagaraDataInterface*> &DataInterfaces = CombinedParamStore.GetDataInterfaces();
		// We must make sure that the data interfaces match up between the original script values and our overrides...
		if (DIClassNames.Num() != DataInterfaces.Num())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Mismatch between Niagara GPU Execution Context data interfaces and those in its script!"));
			return false;
		}

		for (int32 i = 0; i < DIClassNames.Num(); ++i)
		{
			FString UsedClassName = DataInterfaces[i]->GetClass()->GetName();
			if (DIClassNames[i] != UsedClassName)
			{
				UE_LOG(LogNiagara, Warning, TEXT("Mismatched class between Niagara GPU Execution Context data interfaces and those in its script!\nIndex:%d\nShader:%s\nScript:%s")
					, i, *DIClassNames[i], *UsedClassName);
			}
		}
#endif
		CombinedParamStore.Tick();
	}

	return true;
}

bool FNiagaraComputeExecutionContext::OptionalContexInit(FNiagaraSystemInstance* ParentSystemInstance)
{
	if (GPUScript)
	{
		FNiagaraVMExecutableData& VMData = GPUScript->GetVMExecutableData();

		if (VMData.IsValid() && VMData.bNeedsGPUContextInit)
		{
			const TArray<UNiagaraDataInterface*>& DataInterfaces = CombinedParamStore.GetDataInterfaces();
			for (int32 i = 0; i < DataInterfaces.Num(); i++)
			{
				UNiagaraDataInterface* Interface = DataInterfaces[i];

				int32 UserPtrIdx = VMData.DataInterfaceInfo[i].UserPtrIdx;
				if (UserPtrIdx != INDEX_NONE)
				{
					void* InstData = ParentSystemInstance->FindDataInterfaceInstanceData(Interface);
					if (Interface->NeedsGPUContextInit())
					{
						Interface->GPUContextInit(VMData.DataInterfaceInfo[i], InstData, ParentSystemInstance);
					}
				}
			}
		}
	}
	return true;
}

void FNiagaraComputeExecutionContext::PostTick()
{
	//If we're for interpolated spawn, copy over the previous frame's parameters into the Prev parameters.
	if (HasInterpolationParameters)
	{
		CombinedParamStore.CopyCurrToPrev();
	}
}

void FNiagaraComputeExecutionContext::ReleaseReadbackCounter(FNiagaraGPUInstanceCountManager& GPUInstanceCountManager)
{
	if ( EmitterInstanceReadback.GPUCountOffset != INDEX_NONE )
	{
		check(EmitterInstanceReadback.GPUCountOffset != CountOffset_RT);

		GPUInstanceCountManager.FreeEntry(EmitterInstanceReadback.GPUCountOffset);
		EmitterInstanceReadback.GPUCountOffset = INDEX_NONE;
	}
}

void FNiagaraComputeExecutionContext::ResetInternal(NiagaraEmitterInstanceBatcher* Batcher)
{
	checkf(IsInRenderingThread(), TEXT("Can only reset the gpu context from the render thread"));

	if (Batcher)
	{
		ReleaseReadbackCounter(Batcher->GetGPUInstanceCounterManager());
		Batcher->GetGPUInstanceCounterManager().FreeEntry(CountOffset_RT);
	}

	CurrentNumInstances_RT = 0;
	CountOffset_RT = INDEX_NONE;
	EmitterInstanceReadback.GPUCountOffset = INDEX_NONE;

	SetDataToRender(nullptr);
}

void FNiagaraComputeExecutionContext::SetDataToRender(FNiagaraDataBuffer* InDataToRender)
{
	if (DataToRender)
	{
		DataToRender->ReleaseReadRef();
	}

	DataToRender = InDataToRender;

	if (DataToRender)
	{
		DataToRender->AddReadRef();
	}

	// This call the DataToRender should be equal to the TranslucentDataToRender so we can release the read ref
	if (TranslucentDataToRender)
	{
		ensure((DataToRender == nullptr) || (DataToRender == TranslucentDataToRender));
		TranslucentDataToRender->ReleaseReadRef();
		TranslucentDataToRender = nullptr;
	}
}

void FNiagaraComputeExecutionContext::SetTranslucentDataToRender(FNiagaraDataBuffer* InTranslucentDataToRender)
{
	if (TranslucentDataToRender)
	{
		TranslucentDataToRender->ReleaseReadRef();
	}

	TranslucentDataToRender = InTranslucentDataToRender;

	if (TranslucentDataToRender)
	{
		TranslucentDataToRender->AddReadRef();
	}
}

bool FNiagaraComputeInstanceData::IsOutputStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const
{
	if (bUsesOldShaderStages)
	{
		return DIProxy->IsOutputStage_DEPRECATED(CurrentStage);
	}
	else if (bUsesSimStages)
	{
		return Context->IsOutputStage(DIProxy, CurrentStage);
	}
	return false;
}

bool FNiagaraComputeInstanceData::IsIterationStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const
{

	if (bUsesOldShaderStages)
	{
		return DIProxy->IsIterationStage_DEPRECATED(CurrentStage);
	}
	else if (bUsesSimStages)
	{
		return Context->IsIterationStage(DIProxy, CurrentStage);
	}
	return false;
}

FNiagaraDataInterfaceProxyRW* FNiagaraComputeInstanceData::FindIterationInterface(uint32 SimulationStageIndex) const
{
	if (bUsesOldShaderStages)
	{
		FNiagaraDataInterfaceProxyRW* IterationInterface = nullptr;
		for (FNiagaraDataInterfaceProxyRW* Interface : IterationDataInterfaceProxies)
		{
			if (Interface->IsIterationStage_DEPRECATED(SimulationStageIndex))
			{
				if (IterationInterface)
				{
					UE_LOG(LogNiagara, Error, TEXT("Multiple output Data Interfaces found for current stage"));
				}
				else
				{
					IterationInterface = Interface;
				}
			}
		}

		return IterationInterface;
	}
	else if (bUsesSimStages)
	{
		return Context->FindIterationInterface(IterationDataInterfaceProxies, SimulationStageIndex);
	}
	return nullptr;
}
