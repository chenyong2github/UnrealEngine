// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceExport.h"
#include "NiagaraTypes.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraGpuReadbackManager.h"
#include "NiagaraSystemInstance.h"
#include "Internationalization/Internationalization.h"
#include "ShaderParameterUtils.h"
#include "ShaderCompilerCore.h"
#include "RHIGPUReadback.h"

namespace NDIExportLocal
{
	static const TCHAR*		TemplateShaderFile = TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceExportTemplate.ush");

	static const FName		StoreDataName_DEPRECATED(TEXT("StoreParticleData"));
	static const FName		ExportDataName(TEXT("ExportParticleData"));

	static const FString	WriteBufferSizeName(TEXT("WriteBufferSize_"));
	static const FString	RWWriteBufferName(TEXT("RWWriteBuffer_"));
	static const FString	WriteBufferName(TEXT("WriteBuffer_"));

	static constexpr uint32 NumFloatsPerInstance = 7;

	static int GGPUMaxReadbackCount = 1000;
	static FAutoConsoleVariableRef CVarGPUMaxReadbackCount(
		TEXT("fx.Niagara.NDIExport.GPUMaxReadbackCount"),
		GGPUMaxReadbackCount,
		TEXT("Maximum buffer instance count for the GPU readback when in PerParticleMode, where <= 0 means ignore."),
		ECVF_Default
	);
}


/**
Async task to call the blueprint callback on the game thread and isolate the Niagara tick from the blueprint
*/
class FNiagaraExportCallbackAsyncTask
{
	TWeakObjectPtr<UObject> WeakCallbackHandler;
	TArray<FBasicParticleData> Data;
	TWeakObjectPtr<UNiagaraSystem> WeakSystem;

public:
	FNiagaraExportCallbackAsyncTask(TWeakObjectPtr<UObject> InCallbackHandler, TArray<FBasicParticleData> Data, TWeakObjectPtr<UNiagaraSystem> InSystem)
		: WeakCallbackHandler(InCallbackHandler)
		, Data(Data)
		, WeakSystem(InSystem)
	{
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraExportCallbackAsyncTask, STATGROUP_TaskGraphTasks); }
	static FORCEINLINE ENamedThreads::Type GetDesiredThread() { return ENamedThreads::GameThread; }
	static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::FireAndForget; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		UNiagaraSystem* System = WeakSystem.Get();
		if (System == nullptr)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Invalid system handle in export data interface callback, skipping"));
			return;
		}

		UObject* CallbackHandler = WeakCallbackHandler.Get();
		if (CallbackHandler == nullptr)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Invalid CallbackHandler in export data interface callback, skipping"));
			return;
		}

		INiagaraParticleCallbackHandler::Execute_ReceiveParticleData(CallbackHandler, Data, System);
	}
};

//////////////////////////////////////////////////////////////////////////
// Instance and Proxy Data

struct FNDIExportInstanceData_GameThread
{
	TWeakObjectPtr<UObject> CallbackHandler;

	/** We use a lock-free queue here because multiple threads might try to push data to it at the same time. */
	TQueue<FBasicParticleData, EQueueMode::Mpsc> GatheredData;

	/** A binding to the user ptr we're reading the handler from. */
	FNiagaraParameterDirectBinding<UObject*> UserParamBinding;
};

struct FNDIExportInstanceData_RenderThread
{
	ENDIExport_GPUAllocationMode	AllocationMode = ENDIExport_GPUAllocationMode::FixedSize;
	int								AllocationFixedSize = 0;
	float							AllocationPerParticleSize = 0.0f;

	TWeakObjectPtr<UObject>			WeakCallbackHandler;
	TWeakObjectPtr<UNiagaraSystem>	WeakSystem;

	bool							bWriteBufferUsed = false;
	uint32							WriteBufferInstanceCount = 0;
	FRWBuffer						WriteBuffer;
};

struct FNDIExportProxy : public FNiagaraDataInterfaceProxy
{
	FNDIExportProxy() {}
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override {}
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override
	{
		FNDIExportInstanceData_RenderThread* InstanceData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
		check(InstanceData);

		// Ensure our buffer if big enough to hold all the data
		const int32 AllocationRounding = 64;
		const int32 DataBufferNumInstances = Context.ComputeInstanceData->Context->CurrentMaxInstances_RT;
		int32 NumInstances;
		if (InstanceData->AllocationMode == ENDIExport_GPUAllocationMode::PerParticle)
		{
			NumInstances = int32(float(DataBufferNumInstances) * InstanceData->AllocationPerParticleSize);
			NumInstances = FMath::Max(NumInstances, 0);
			if (NDIExportLocal::GGPUMaxReadbackCount > 0)
			{
				NumInstances = FMath::Min(NumInstances, NDIExportLocal::GGPUMaxReadbackCount);
			}
		}
		else
		{
			NumInstances = InstanceData->AllocationFixedSize;
		}

		const int32 AllocationNumInstances = FMath::DivideAndRoundUp(NumInstances, AllocationRounding) * AllocationRounding;
		const int32 RequiredElements = 1 + (AllocationNumInstances * NDIExportLocal::NumFloatsPerInstance);
		const int32 RequiredBytes = sizeof(float) * RequiredElements;

		InstanceData->WriteBufferInstanceCount = NumInstances;
		if (InstanceData->WriteBuffer.NumBytes != RequiredBytes)
		{
			InstanceData->WriteBuffer.Release();
			InstanceData->WriteBuffer.Initialize(sizeof(float), RequiredElements, PF_R32_UINT, BUF_SourceCopy);

			// Clear counter when we initialize the buffer
			ClearCounter(RHICmdList, InstanceData);
		}

		RHICmdList.Transition(FRHITransitionInfo(InstanceData->WriteBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
	}

	virtual void PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override
	{
		FNDIExportInstanceData_RenderThread* InstanceData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
		check(InstanceData);

		// If we bound the buffer as a UAV the stage may have written to it
		if (InstanceData->bWriteBufferUsed)
		{
			InstanceData->bWriteBufferUsed = false;

			RHICmdList.Transition(FRHITransitionInfo(InstanceData->WriteBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::CopySrc));

			FNiagaraGpuReadbackManager* ReadbackManager = Context.Batcher->GetGpuReadbackManager();
			ReadbackManager->EnqueueReadback(
				RHICmdList,
				InstanceData->WriteBuffer.Buffer,
				[MaxInstances=InstanceData->WriteBufferInstanceCount, WeakCallbackHandler=InstanceData->WeakCallbackHandler, WeakSystem=InstanceData->WeakSystem](TConstArrayView<TPair<void*, uint32>> Buffers)
				{
					uint32 ReadbackInstanceCount = *reinterpret_cast<uint32*>(Buffers[0].Key);
					if (ReadbackInstanceCount > 0)
					{
						ReadbackInstanceCount = FMath::Min(ReadbackInstanceCount, MaxInstances);

						// Translate float data into Export Data
						TArray<FBasicParticleData> ExportParticleData;
						ExportParticleData.AddUninitialized(ReadbackInstanceCount);

						const float* FloatData = reinterpret_cast<float*>(Buffers[0].Key) + 1;
						for (uint32 i = 0; i < ReadbackInstanceCount; ++i)
						{
							ExportParticleData[i].Position.X = FloatData[0];
							ExportParticleData[i].Position.Y = FloatData[1];
							ExportParticleData[i].Position.Z = FloatData[2];
							ExportParticleData[i].Size = FloatData[3];
							ExportParticleData[i].Velocity.X = FloatData[4];
							ExportParticleData[i].Velocity.Y = FloatData[5];
							ExportParticleData[i].Velocity.Z = FloatData[6];

							FloatData += NDIExportLocal::NumFloatsPerInstance;
						}

						TGraphTask<FNiagaraExportCallbackAsyncTask>::CreateTask().ConstructAndDispatchWhenReady(WeakCallbackHandler, MoveTemp(ExportParticleData), WeakSystem);
					}
				}
			);

			RHICmdList.Transition(FRHITransitionInfo(InstanceData->WriteBuffer.UAV, ERHIAccess::CopySrc, ERHIAccess::UAVCompute));

			// Ensure counter is clear for the next user, they will perform the transition on the buffer
			ClearCounter(RHICmdList, InstanceData);
		}
	}

	void ClearCounter(FRHICommandList& RHICmdList, FNDIExportInstanceData_RenderThread* InstanceData)
	{
		check(InstanceData);
		check(InstanceData->WriteBuffer.NumBytes > 0);

		//-OPT: We only need to reset the first 4 bytes for the count
		RHICmdList.ClearUAVUint(InstanceData->WriteBuffer.UAV, FUintVector4(0, 0, 0, 0));
	}

	TMap<FNiagaraSystemInstanceID, FNDIExportInstanceData_RenderThread> SystemInstancesToProxyData_RT;
};

//////////////////////////////////////////////////////////////////////////
// Compute Shader Binding

struct FNDIExportCS : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNDIExportCS, NonVirtual);

public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		WriteBufferSizeParam.Bind(ParameterMap, *(NDIExportLocal::WriteBufferSizeName + ParameterInfo.DataInterfaceHLSLSymbol));
		WriteBufferParam.Bind(ParameterMap, *(NDIExportLocal::WriteBufferName + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		uint32 WriteBufferSize = 0;

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		if (WriteBufferParam.IsUAVBound())
		{
			// Get proxy data
			FNDIExportProxy* DIProxy = static_cast<FNDIExportProxy*>(Context.DataInterface);
			FNDIExportInstanceData_RenderThread* InstanceData = DIProxy->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
			check(InstanceData);

			if (InstanceData->WeakCallbackHandler.IsExplicitlyNull())
			{
				WriteBufferSize = 0;
				RHICmdList.SetUAVParameter(Context.Shader.GetComputeShader(), WriteBufferParam.GetUAVIndex(), Context.Batcher->GetEmptyUAVFromPool(RHICmdList, PF_R32_UINT, ENiagaraEmptyUAVType::Buffer));
			}
			else
			{
				InstanceData->bWriteBufferUsed = true;
				WriteBufferSize = InstanceData->WriteBufferInstanceCount;
				RHICmdList.SetUAVParameter(Context.Shader.GetComputeShader(), WriteBufferParam.GetUAVIndex(), InstanceData->WriteBuffer.UAV);
			}
		}
		SetShaderValue(RHICmdList, ComputeShaderRHI, WriteBufferSizeParam, WriteBufferSize);
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		WriteBufferParam.UnsetUAV(RHICmdList, Context.Shader.GetComputeShader());
	}

private:
	LAYOUT_FIELD(FShaderParameter, WriteBufferSizeParam);
	LAYOUT_FIELD(FRWShaderParameter, WriteBufferParam);
};

IMPLEMENT_TYPE_LAYOUT(FNDIExportCS);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceExport, FNDIExportCS);

//////////////////////////////////////////////////////////////////////////
// Data Interface

UNiagaraDataInterfaceExport::UNiagaraDataInterfaceExport(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNDIExportProxy());

	FNiagaraTypeDefinition Def(UObject::StaticClass());
	CallbackHandlerParameter.Parameter.SetType(Def);
}

void UNiagaraDataInterfaceExport::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

bool UNiagaraDataInterfaceExport::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIExportInstanceData_GameThread* PIData = new (PerInstanceData) FNDIExportInstanceData_GameThread;

	ENQUEUE_RENDER_COMMAND(FNDIExport_AddProxy)
	(
		[RT_Proxy=GetProxyAs<FNDIExportProxy>(), RT_InstanceID=SystemInstance->GetId(), RT_AllocationMode=GPUAllocationMode, RT_AllocationFixedSize=GPUAllocationFixedSize, RT_AllocationPerParticleSize=GPUAllocationPerParticleSize](FRHICommandListImmediate& RHICmdList)
		{
			check(!RT_Proxy->SystemInstancesToProxyData_RT.Contains(RT_InstanceID));
			FNDIExportInstanceData_RenderThread* InstanceData = &RT_Proxy->SystemInstancesToProxyData_RT.Add(RT_InstanceID);
			check(InstanceData);

			InstanceData->AllocationMode = RT_AllocationMode;
			InstanceData->AllocationFixedSize = RT_AllocationFixedSize;
			InstanceData->AllocationPerParticleSize = RT_AllocationPerParticleSize;
		}
	);

	return true;
}

void UNiagaraDataInterfaceExport::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIExportInstanceData_GameThread* InstData = (FNDIExportInstanceData_GameThread*)PerInstanceData;
	InstData->~FNDIExportInstanceData_GameThread();

	ENQUEUE_RENDER_COMMAND(FNDIExport_RemoveProxy)
	(
		[RT_Proxy=GetProxyAs<FNDIExportProxy>(), InstanceID=SystemInstance->GetId(), Batcher=SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
			RT_Proxy->SystemInstancesToProxyData_RT.Remove(InstanceID);
		}
	);
}

int32 UNiagaraDataInterfaceExport::PerInstanceDataSize() const
{
	return sizeof(FNDIExportInstanceData_GameThread);
}


bool UNiagaraDataInterfaceExport::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	check(SystemInstance);
	FNDIExportInstanceData_GameThread* PIData = (FNDIExportInstanceData_GameThread*)PerInstanceData;
	if (!PIData)
	{
		return true;
	}

	TWeakObjectPtr<UObject> PreviousCallbackHandler = PIData->CallbackHandler;
	if (CallbackHandlerParameter.Parameter.IsValid() && SystemInstance)
	{
		//-OPT: Don't need to init this per frame, could be bound once and read per frame
		PIData->CallbackHandler = PIData->UserParamBinding.Init(SystemInstance->GetInstanceParameters(), CallbackHandlerParameter.Parameter);
	}
	else
	{
		PIData->CallbackHandler.Reset();
	}

	// If we switched modify the RT data
	if (PreviousCallbackHandler != PIData->CallbackHandler)
	{
		ENQUEUE_RENDER_COMMAND(FNDIExport_UpdateHandler)
		(
			[RT_Proxy=GetProxyAs<FNDIExportProxy>(), RT_InstanceID=SystemInstance->GetId(), RT_WeakHandler=PIData->CallbackHandler, RT_WeakSystem=SystemInstance->GetSystem()](FRHICommandListImmediate& RHICmdList)
			{
				FNDIExportInstanceData_RenderThread* InstanceData = RT_Proxy->SystemInstancesToProxyData_RT.Find(RT_InstanceID);
				if (ensure(InstanceData != nullptr))
				{
					InstanceData->WeakCallbackHandler = RT_WeakHandler;
					InstanceData->WeakSystem = RT_WeakSystem;
				}
			}
		);
	}

	return false;
}

bool UNiagaraDataInterfaceExport::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FNDIExportInstanceData_GameThread* PIData = (FNDIExportInstanceData_GameThread*) PerInstanceData;
	if ( !PIData->GatheredData.IsEmpty() )
	{
		UObject* CallbackHandler = PIData->CallbackHandler.Get();
		if ( CallbackHandler && CallbackHandler->GetClass()->ImplementsInterface(UNiagaraParticleCallbackHandler::StaticClass()) )
		{
			//Drain the queue into an array here
			TArray<FBasicParticleData> Data;
			FBasicParticleData Value;
			while (PIData->GatheredData.Dequeue(Value))
			{
				Data.Add(Value);
			}

			TGraphTask<FNiagaraExportCallbackAsyncTask>::CreateTask().ConstructAndDispatchWhenReady(PIData->CallbackHandler, MoveTemp(Data), SystemInstance->GetSystem());
		}
	}
	return false;
}

bool UNiagaraDataInterfaceExport::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceExport* OtherTyped = CastChecked<const UNiagaraDataInterfaceExport>(Other);

	return
		OtherTyped->CallbackHandlerParameter == CallbackHandlerParameter &&
		OtherTyped->GPUAllocationMode == GPUAllocationMode &&
		OtherTyped->GPUAllocationFixedSize == GPUAllocationFixedSize &&
		OtherTyped->GPUAllocationPerParticleSize == GPUAllocationPerParticleSize;
}

void UNiagaraDataInterfaceExport::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature SigOld;
	SigOld.Name = NDIExportLocal::StoreDataName_DEPRECATED;
#if WITH_EDITORONLY_DATA
	SigOld.Description = NSLOCTEXT("Niagara", "ExportDataFunctionDescription", "This function takes the particle data and stores it to be exported to the registered callback handler after the simulation has ticked.");
#endif
	SigOld.bMemberFunction = true;
	SigOld.bRequiresContext = false;
	SigOld.bSupportsGPU = true;
	SigOld.bSoftDeprecatedFunction = true;
	SigOld.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Export interface")));
	SigOld.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Store Data")));
	SigOld.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
	SigOld.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Size")));
	SigOld.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
	SigOld.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Success")));
	OutFunctions.Add(SigOld);

	FNiagaraFunctionSignature Sig;
	Sig.Name = NDIExportLocal::ExportDataName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "ExportDataFunctionDescription", "This function takes the particle data and stores it to be exported to the registered callback handler after the simulation has ticked.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsGPU = true;
	Sig.bRequiresExecPin = true;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Export interface")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Store Data")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Size")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("WasExported")));
	OutFunctions.Add(Sig);
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceExport::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{TEXT("ParameterName"),	ParamInfo.DataInterfaceHLSLSymbol},
	};

	FString TemplateFile;
	LoadShaderSourceFile(NDIExportLocal::TemplateShaderFile, EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}

bool UNiagaraDataInterfaceExport::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	using namespace NDIExportLocal;

	if ( (FunctionInfo.DefinitionName == StoreDataName_DEPRECATED) || (FunctionInfo.DefinitionName == ExportDataName) )
	{
		return true;
	}

	// Invalid function
	return false;
}

bool UNiagaraDataInterfaceExport::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	bool bSuccess = Super::AppendCompileHash(InVisitor);
	FSHAHash Hash = GetShaderFileHash(NDIExportLocal::TemplateShaderFile, EShaderPlatform::SP_PCD3D_SM5);
	InVisitor->UpdateString(TEXT("NiagaraDataInterfaceExportTemplateHLSLSource"), Hash.ToString());
	return bSuccess;
}
#endif

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceExport, StoreData);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceExport, ExportData);
void UNiagaraDataInterfaceExport::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == NDIExportLocal::StoreDataName_DEPRECATED)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceExport, StoreData)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == NDIExportLocal::ExportDataName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceExport, ExportData)::Bind(this, OutFunc);
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Could not find data interface external function. Expected Name: %s  Actual Name: %s"), *NDIExportLocal::ExportDataName.ToString(), *BindingInfo.Name.ToString());
	}
}

void UNiagaraDataInterfaceExport::StoreData(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIExportInstanceData_GameThread> InstData(Context);

	FNDIInputParam<FNiagaraBool> StoreDataParam(Context);
	FNDIInputParam<FVector> PositionParam(Context);
	FNDIInputParam<float> SizeParam(Context);
	FNDIInputParam<FVector> VelocityParam(Context);

	FNDIOutputParam<FNiagaraBool> OutSample(Context);

	checkfSlow(InstData.Get(), TEXT("Export data interface has invalid instance data. %s"), *GetPathName());
	bool ValidHandlerData = InstData->UserParamBinding.BoundVariable.IsValid() && InstData->CallbackHandler.IsValid();

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const bool ShouldStore = StoreDataParam.GetAndAdvance();

		FBasicParticleData Data;
		Data.Position = PositionParam.GetAndAdvance();
		Data.Size = SizeParam.GetAndAdvance();
		Data.Velocity = VelocityParam.GetAndAdvance();

		bool Valid = false;
		if (ValidHandlerData && ShouldStore)
		{
			Valid = InstData->GatheredData.Enqueue(Data);
		}
		OutSample.SetAndAdvance(Valid);
	}
}

void UNiagaraDataInterfaceExport::ExportData(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIExportInstanceData_GameThread> InstData(Context);

	FNDIInputParam<FNiagaraBool> StoreDataParam(Context);
	FNDIInputParam<FVector> PositionParam(Context);
	FNDIInputParam<float> SizeParam(Context);
	FNDIInputParam<FVector> VelocityParam(Context);

	FNDIOutputParam<FNiagaraBool> OutWasExported(Context);

	checkfSlow(InstData.Get(), TEXT("Export data interface has invalid instance data. %s"), *GetPathName());
	bool ValidHandlerData = InstData->UserParamBinding.BoundVariable.IsValid() && InstData->CallbackHandler.IsValid();

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		const bool ShouldStore = StoreDataParam.GetAndAdvance();

		FBasicParticleData Data;
		Data.Position = PositionParam.GetAndAdvance();
		Data.Size = SizeParam.GetAndAdvance();
		Data.Velocity = VelocityParam.GetAndAdvance();

		bool Valid = false;
		if (ValidHandlerData && ShouldStore)
		{
			Valid = InstData->GatheredData.Enqueue(Data);
		}
		OutWasExported.SetAndAdvance(Valid);
	}
}

bool UNiagaraDataInterfaceExport::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceExport* OtherTyped = CastChecked<UNiagaraDataInterfaceExport>(Destination);
	OtherTyped->CallbackHandlerParameter = CallbackHandlerParameter;
	OtherTyped->GPUAllocationMode = GPUAllocationMode;
	OtherTyped->GPUAllocationFixedSize = GPUAllocationFixedSize;
	OtherTyped->GPUAllocationPerParticleSize = GPUAllocationPerParticleSize;
	return true;
}
