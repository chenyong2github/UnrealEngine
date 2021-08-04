// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceFrostySensorGrid.h"

#include "FrostySensorGridShaders.h"
#include "GlobalDistanceFieldParameters.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraRayTracingHelper.h"
#include "NiagaraTypes.h"
#include "NiagaraWorldManager.h"
#include "RayTracingInstanceUtils.h"
#include "RenderResource.h"
#include "Shader.h"
#include "ShaderCore.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterUtils.h"

// c++ mirror of the struct defined in NiagaraFrostySensorGridCommon.ush
struct alignas(16) FSensorInfo
{
	FVector4 LocationAndDistance;
	FIntVector HitIndex;
};

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceFrostySensorGrid"

namespace NDIFrostySensorGridLocal
{
	static const TCHAR* CommonShaderFile = TEXT("/FrostySensorGrid/NiagaraDataInterfaceFrostySensorGrid.ush");
	static const TCHAR* TemplateShaderFile = TEXT("/FrostySensorGrid/NiagaraDataInterfaceFrostySensorGridTemplate.ush");

	static const FName UpdateSensorName(TEXT("UpdateSensorGpu"));
	static const FName FindNearestName(TEXT("FindNearestGpu"));

	static const FString BoundsHierarchyParamName(TEXT("BoundsHierarchy_"));
	static const FString SensorInfoParamName(TEXT("SensorInfo_"));
	static const FString SensorGridDimensionsParamName(TEXT("SensorGridDimensions_"));
	static const FString SensorGridWriteIndexParamName(TEXT("SensorGridWriteIndex_"));
	static const FString SensorGridReadIndexParamName(TEXT("SensorGridReadIndex_"));

	enum NodeVersion : int32
	{
		NodeVersion_Initial = 0,
		Nodeversion_Reworked_Output,

		VersionPlusOne,
		NodeVersion_Latest = VersionPlusOne - 1,
	};

	const FText SensorXDescription = IF_WITH_EDITORONLY_DATA(LOCTEXT("SensorXDescription", "?."), FText());
	const FText SensorYDescription = IF_WITH_EDITORONLY_DATA(LOCTEXT("SensorYDescription", "??."), FText());
	const FText LocationDescription = IF_WITH_EDITORONLY_DATA(LOCTEXT("LocationDescription", "????."), FText());
	const FText SensorRangeDescription = IF_WITH_EDITORONLY_DATA(LOCTEXT("SensorRangeDescription", "????."), FText());
	const FText SensorValidDescription = IF_WITH_EDITORONLY_DATA(LOCTEXT("SensorValidDescription", "????."), FText());

	const FText OutputLocationDescription = IF_WITH_EDITORONLY_DATA(LOCTEXT("OutputLocationDescription", "?."), FText());
	const FText OutputDistanceDescription = IF_WITH_EDITORONLY_DATA(LOCTEXT("OutputDistanceDescription", "?."), FText());
	const FText OutputIsValidDescription = IF_WITH_EDITORONLY_DATA(LOCTEXT("OutputIsValidDescription", "?."), FText());

}

struct FSensorGridNetworkProxy
{
	// ByteAddress buffer used to store the bounds hierarchy for all sensors in the network
	//	-the leaves of the trees are populated by the simulation pass of the system instances
	//	-global shader used to construct the higher level tiers of the tree
	TUniquePtr<FRWByteAddressBuffer> BoundsHierarchy;

	// StructuredBuffer storing the results of a global query for finding the closest sensor
	//	-populated by global shader
	//	-read by simulation pass of the system instances
	TUniquePtr<FRWBufferStructured> SensorInfo;

	TUniquePtr<FRWBufferStructured> EmptyResults;

	// mapping between the system instance ID and the subnetwork within the buffers
	TMap<FNiagaraSystemInstanceID, int32> InstanceOwnerReadIndexMap;
	TMap<FNiagaraSystemInstanceID, int32> InstanceOwnerWriteIndexMap;

	TSet<FNiagaraSystemInstanceID> RegisteredInstances;

	int32 QueuedOwnerCount = 0;
	int32 AllocatedOwnerCount = 0;
	int32 ResultsOwnerCount = 0;
	uint32 ProcessedFrameNumber = INDEX_NONE;

	bool CurrentlySimulating = false;

	// XY is the grid of sensors
	const FIntPoint SensorGridDimensions;

	FSensorGridNetworkProxy() = delete;
	FSensorGridNetworkProxy(int32 SensorGridWidth)
		: SensorGridDimensions(SensorGridWidth, SensorGridWidth)
	{
		// initialize with a dummy SensorInfo structured buffer which will be used for the frames where we don't have any results
		EmptyResults = MakeUnique<FRWBufferStructured>();
		EmptyResults->Initialize(
			TEXT("FrostySensorGridEmptyResults"),
			sizeof(FSensorInfo),
			1,
			BUF_Static,
			false /*bUseUavCounter*/,
			false /*bAppendBuffer*/,
			ERHIAccess::SRVCompute);
	}

	~FSensorGridNetworkProxy()
	{
		if (auto* BoundsHierarchyRef = BoundsHierarchy.Release())
		{
			BoundsHierarchyRef->Release();
			delete BoundsHierarchyRef;
		}

		if (auto* SensorInfoRef = SensorInfo.Release())
		{
			SensorInfoRef->Release();
			delete SensorInfoRef;
		}

		if (auto* EmptyResultsRef = EmptyResults.Release())
		{
			EmptyResultsRef->Release();
			delete EmptyResultsRef;
		}
	}

	void BeginSimulation(FRHICommandList& RHICmdList)
	{
		if (QueuedOwnerCount)
		{
			if (QueuedOwnerCount != AllocatedOwnerCount)
			{
				if (FRWByteAddressBuffer* ReleasedBuffer = BoundsHierarchy.Release())
				{
					ReleasedBuffer->Release();
					delete ReleasedBuffer;
				}

				int32 HierarchySensorCount = 0;
				for (int32 SensorIt = 1; SensorIt <= SensorGridDimensions.X; SensorIt *= 2)
				{
					HierarchySensorCount += SensorIt * SensorIt * QueuedOwnerCount;
				}

				BoundsHierarchy = MakeUnique<FRWByteAddressBuffer>();
				BoundsHierarchy->Initialize(
					TEXT("FrostySensorGridBoundsHierarchy"),
					HierarchySensorCount * sizeof(FVector4),
					BUF_Static);

				RHICmdList.Transition(FRHITransitionInfo(BoundsHierarchy->UAV, ERHIAccess::SRVMask, ERHIAccess::UAVCompute));
			}

			AllocatedOwnerCount = QueuedOwnerCount;
			QueuedOwnerCount = 0;

			RHICmdList.BeginUAVOverlap(BoundsHierarchy->UAV);
			CurrentlySimulating = true;
		}
	}

	void EndSimulation(FRHICommandList& RHICmdList)
	{
		if (AllocatedOwnerCount && CurrentlySimulating)
		{
			RHICmdList.EndUAVOverlap(BoundsHierarchy->UAV);
			CurrentlySimulating = false;
		}
	}

	void PrepareResultsBuffer()
	{
		if (AllocatedOwnerCount != ResultsOwnerCount)
		{
			if (FRWBufferStructured* ReleasedBuffer = SensorInfo.Release())
			{
				ReleasedBuffer->Release();
				delete ReleasedBuffer;
			}

			ResultsOwnerCount = AllocatedOwnerCount;

			SensorInfo = MakeUnique<FRWBufferStructured>();
			SensorInfo->Initialize(
				TEXT("FrostySensorGridSensorInfo"),
				sizeof(FSensorInfo),
				SensorGridDimensions.X * SensorGridDimensions.Y * ResultsOwnerCount,
				BUF_Static,
				false /*bUseUavCounter*/,
				false /*bAppendBuffer*/,
				ERHIAccess::SRVCompute);
		}
	}

	void Dispatch(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type InFeatureLevel, const FIntPoint& InSensorGridDimensions, const FVector2D& GlobalSensorRange)
	{
		if (ProcessedFrameNumber == GFrameNumberRenderThread)
		{
			return;
		}

		ProcessedFrameNumber = GFrameNumberRenderThread;

		if (AllocatedOwnerCount)
		{
			PrepareResultsBuffer();

			FFrostySensorGridHelper Helper(InFeatureLevel, FIntVector(SensorGridDimensions.X, SensorGridDimensions.Y, AllocatedOwnerCount));

			RHICmdList.Transition(FRHITransitionInfo(BoundsHierarchy->UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			Helper.BuildBounds(RHICmdList, BoundsHierarchy->UAV);

			RHICmdList.Transition(FRHITransitionInfo(BoundsHierarchy->UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
			RHICmdList.Transition(FRHITransitionInfo(SensorInfo->UAV, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute));
			Helper.FindNearestSensors(RHICmdList, BoundsHierarchy->SRV, GlobalSensorRange, SensorInfo->UAV);
			RHICmdList.Transition(FRHITransitionInfo(SensorInfo->UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));

			InstanceOwnerReadIndexMap = InstanceOwnerWriteIndexMap;
			InstanceOwnerWriteIndexMap.Reset();
		}
	}
};

struct FNiagaraDataIntefaceProxyFrostySensorGrid : public FNiagaraDataInterfaceProxy
{
	FRWLock NetworkLock;
	TMap<const NiagaraEmitterInstanceBatcher*, TUniquePtr<FSensorGridNetworkProxy>> NetworkProxies;

	int32 SensorGridSize = 0;
	FVector2D GlobalSensorRange = FVector2D(EForceInit::ForceInitToZero);

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}

	FSensorGridNetworkProxy* GetNetwork(const NiagaraEmitterInstanceBatcher* Batcher, bool CreateIfMissing = false)
	{
		FReadScopeLock _Scope(NetworkLock);
		return NetworkProxies.FindChecked(Batcher).Get();
	}

	void RegisterNetworkInstance(const NiagaraEmitterInstanceBatcher* Batcher, FNiagaraSystemInstanceID SystemInstanceID)
	{
		FWriteScopeLock _Scope(NetworkLock);
		TUniquePtr<FSensorGridNetworkProxy>& Network = NetworkProxies.FindOrAdd(Batcher, nullptr);
		if (!Network)
		{
			Network = MakeUnique<FSensorGridNetworkProxy>(SensorGridSize);
		}

		Network->RegisteredInstances.Add(SystemInstanceID);
	}

	void UnregisterNetworkInstance(const NiagaraEmitterInstanceBatcher* Batcher, FNiagaraSystemInstanceID SystemInstanceID)
	{
		FWriteScopeLock _Scope(NetworkLock);
		if (TUniquePtr<FSensorGridNetworkProxy>* NetworkPtr = NetworkProxies.Find(Batcher))
		{
			if (FSensorGridNetworkProxy* Network = NetworkPtr->Get())
			{
				Network->RegisteredInstances.Remove(SystemInstanceID);
				if (!Network->RegisteredInstances.Num())
				{
					NetworkProxies.Remove(Batcher);
				}
			}

		}
	}

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override
	{
		FNiagaraDataInterfaceProxy::PreStage(RHICmdList, Context);

		if (FSensorGridNetworkProxy* Network = GetNetwork(Context.Batcher, true))
		{
			int32& OwnerIndex = Network->InstanceOwnerWriteIndexMap.FindOrAdd(Context.SystemInstanceID, INDEX_NONE);
			if (OwnerIndex == INDEX_NONE)
			{
				OwnerIndex = Network->QueuedOwnerCount++;
			}
		}
	}

	virtual void PostSimulate(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context) override
	{
		FNiagaraDataInterfaceProxy::PostSimulate(RHICmdList, Context);

		if (FSensorGridNetworkProxy* Network = GetNetwork(Context.Batcher))
		{
			Network->Dispatch(RHICmdList, Context.Batcher->GetFeatureLevel(), Network->SensorGridDimensions, GlobalSensorRange);
		}
	}

	void RenderThreadInitialize(int32 InSensorCount, const FVector2D& InGlobalSensorRange)
	{
		// SensorCount needs to be a power of two
		SensorGridSize = FMath::RoundUpToPowerOfTwo(InSensorCount);
		GlobalSensorRange.X = FMath::Max(0.0f, InGlobalSensorRange.X);
		GlobalSensorRange.Y = FMath::Max(GlobalSensorRange.X, InGlobalSensorRange.Y);

		NetworkProxies.Reset();
	}
};


UNiagaraDataInterfaceFrostySensorGrid::UNiagaraDataInterfaceFrostySensorGrid(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
    Proxy.Reset(new FNiagaraDataIntefaceProxyFrostySensorGrid());
}

void UNiagaraDataInterfaceFrostySensorGrid::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

void UNiagaraDataInterfaceFrostySensorGrid::PostLoad()
{
	Super::PostLoad();

	if (SensorCountPerSide)
	{
		MarkRenderDataDirty();
	}
}

void UNiagaraDataInterfaceFrostySensorGrid::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature SigUpdateSensor;
	SigUpdateSensor.Name = NDIFrostySensorGridLocal::UpdateSensorName;
	SigUpdateSensor.bRequiresExecPin = true;
	SigUpdateSensor.bMemberFunction = true;
	SigUpdateSensor.bRequiresContext = false;
	SigUpdateSensor.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
	SigUpdateSensor.FunctionVersion = NDIFrostySensorGridLocal::NodeVersion_Latest;
	SigUpdateSensor.Description = LOCTEXT("SceneDepthSignatureDescription", "Projects a given world position to view space and then queries the depth buffer with that position.");
#endif

	SigUpdateSensor.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("FrostySensorGrid")));
	SigUpdateSensor.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Sensor_X")), NDIFrostySensorGridLocal::SensorXDescription);
	SigUpdateSensor.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Sensor_Y")), NDIFrostySensorGridLocal::SensorYDescription);
	SigUpdateSensor.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Location")), NDIFrostySensorGridLocal::LocationDescription);
	SigUpdateSensor.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SensorRange")), NDIFrostySensorGridLocal::SensorRangeDescription);
	SigUpdateSensor.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid")), NDIFrostySensorGridLocal::SensorValidDescription);
	OutFunctions.Add(SigUpdateSensor);

	FNiagaraFunctionSignature SigFindNearest;
	SigFindNearest.Name = NDIFrostySensorGridLocal::FindNearestName;
	SigFindNearest.bMemberFunction = true;
	SigFindNearest.bRequiresContext = false;
	SigFindNearest.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
	SigFindNearest.FunctionVersion = NDIFrostySensorGridLocal::NodeVersion_Latest;
	SigFindNearest.Description = LOCTEXT("CustomDepthDescription", "Projects a given world position to view space and then queries the custom depth buffer with that position.");
#endif

	SigFindNearest.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("FrostySensorGrid")));
	SigFindNearest.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Sensor_X")), NDIFrostySensorGridLocal::SensorXDescription);
	SigFindNearest.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Sensor_Y")), NDIFrostySensorGridLocal::SensorYDescription);
	SigFindNearest.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Out_Location")), NDIFrostySensorGridLocal::OutputLocationDescription);
	SigFindNearest.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Out_Distance")), NDIFrostySensorGridLocal::OutputDistanceDescription);
	SigFindNearest.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Out_IsValid")), NDIFrostySensorGridLocal::OutputIsValidDescription);
	OutFunctions.Add(SigFindNearest);
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceFrostySensorGrid::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if ( (FunctionInfo.DefinitionName == NDIFrostySensorGridLocal::UpdateSensorName) ||
		 (FunctionInfo.DefinitionName == NDIFrostySensorGridLocal::FindNearestName) )
	{
		return true;
	}

	return false;
}

void UNiagaraDataInterfaceFrostySensorGrid::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{TEXT("ParameterName"),	ParamInfo.DataInterfaceHLSLSymbol},
	};

	FString TemplateFile;
	LoadShaderSourceFile(NDIFrostySensorGridLocal::TemplateShaderFile, EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}

void UNiagaraDataInterfaceFrostySensorGrid::GetCommonHLSL(FString& OutHlsl)
{
	OutHlsl.Appendf(TEXT("#include \"%s\"\n"), NDIFrostySensorGridLocal::CommonShaderFile);
}

bool UNiagaraDataInterfaceFrostySensorGrid::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!Super::AppendCompileHash(InVisitor))
	{
		return false;
	}

	InVisitor->UpdateString(TEXT("NDIFrostySensorGridCommonHLSLSource"), GetShaderFileHash(NDIFrostySensorGridLocal::CommonShaderFile, EShaderPlatform::SP_PCD3D_SM5).ToString());
	InVisitor->UpdateString(TEXT("NDIFrostySensorGridTemplateHLSLSource"), GetShaderFileHash(NDIFrostySensorGridLocal::TemplateShaderFile, EShaderPlatform::SP_PCD3D_SM5).ToString());

	return true;
}

void UNiagaraDataInterfaceFrostySensorGrid::ModifyCompilationEnvironment(EShaderPlatform ShaderPlatform, FShaderCompilerEnvironment& OutEnvironment) const
{
	Super::ModifyCompilationEnvironment(ShaderPlatform, OutEnvironment);
}


bool UNiagaraDataInterfaceFrostySensorGrid::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	bool WasChanged = false;

	if (FunctionSignature.Name == NDIFrostySensorGridLocal::UpdateSensorName && FunctionSignature.FunctionVersion == NDIFrostySensorGridLocal::NodeVersion::NodeVersion_Initial)
	{
		FunctionSignature.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid")), NDIFrostySensorGridLocal::SensorValidDescription);
		FunctionSignature.FunctionVersion = NDIFrostySensorGridLocal::NodeVersion_Latest;

		WasChanged = true;
	}

	if (FunctionSignature.Name == NDIFrostySensorGridLocal::FindNearestName && FunctionSignature.FunctionVersion == NDIFrostySensorGridLocal::NodeVersion::NodeVersion_Initial)
	{
		FunctionSignature.Outputs.Reset();
		FunctionSignature.OutputDescriptions.Reset();

		FunctionSignature.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Out_Location")), NDIFrostySensorGridLocal::OutputLocationDescription);
		FunctionSignature.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Out_Distance")), NDIFrostySensorGridLocal::OutputDistanceDescription);
		FunctionSignature.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Out_IsValid")), NDIFrostySensorGridLocal::OutputIsValidDescription);
		FunctionSignature.FunctionVersion = NDIFrostySensorGridLocal::NodeVersion_Latest;

		WasChanged = true;
	}

	return WasChanged;
}

#endif

int32 UNiagaraDataInterfaceFrostySensorGrid::PerInstanceDataSize() const
{
	return 4;
}

bool UNiagaraDataInterfaceFrostySensorGrid::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	if (SystemInstance)
	{
		FNiagaraDataIntefaceProxyFrostySensorGrid* RT_Proxy = GetProxyAs<FNiagaraDataIntefaceProxyFrostySensorGrid>();

		// Push Updates to Proxy, first release any resources
		ENQUEUE_RENDER_COMMAND(FDestroyInstance)(
			[RT_Proxy, RT_Batcher = SystemInstance->GetBatcher(), RT_InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
		{
			RT_Proxy->RegisterNetworkInstance(RT_Batcher, RT_InstanceID);
		});
	}

	return true;
}

void UNiagaraDataInterfaceFrostySensorGrid::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	if (SystemInstance)
	{
		FNiagaraDataIntefaceProxyFrostySensorGrid* RT_Proxy = GetProxyAs<FNiagaraDataIntefaceProxyFrostySensorGrid>();

		// Push Updates to Proxy, first release any resources
		ENQUEUE_RENDER_COMMAND(FDestroyInstance)(
			[RT_Proxy, RT_Batcher = SystemInstance->GetBatcher(), RT_InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
		{
			RT_Proxy->UnregisterNetworkInstance(RT_Batcher, RT_InstanceID);
		});
	}
}

bool UNiagaraDataInterfaceFrostySensorGrid::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceFrostySensorGrid* OtherTyped = CastChecked<const UNiagaraDataInterfaceFrostySensorGrid>(Other);
	return OtherTyped->SensorCountPerSide == SensorCountPerSide
		&& OtherTyped->GlobalSensorAccuracy == GlobalSensorAccuracy
		&& OtherTyped->GlobalSensorRange == GlobalSensorRange;
}

bool UNiagaraDataInterfaceFrostySensorGrid::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceFrostySensorGrid* OtherTyped = CastChecked<UNiagaraDataInterfaceFrostySensorGrid>(Destination);
	OtherTyped->SensorCountPerSide = SensorCountPerSide;
	OtherTyped->GlobalSensorAccuracy = GlobalSensorAccuracy;
	OtherTyped->GlobalSensorRange = GlobalSensorRange;
	OtherTyped->MarkRenderDataDirty();
	return true;
}

void UNiagaraDataInterfaceFrostySensorGrid::PushToRenderThreadImpl()
{
	FNiagaraDataIntefaceProxyFrostySensorGrid* RT_Proxy = GetProxyAs<FNiagaraDataIntefaceProxyFrostySensorGrid>();

	// Push Updates to Proxy, first release any resources
	ENQUEUE_RENDER_COMMAND(FUpdateDI)(
		[RT_Proxy, RT_SensorCount = SensorCountPerSide, RT_GlobalSensorRange = FVector2D(GlobalSensorAccuracy, GlobalSensorRange)](FRHICommandListImmediate& RHICmdList)
		{
			RT_Proxy->RenderThreadInitialize(RT_SensorCount, RT_GlobalSensorRange);
		});
}

#if WITH_EDITOR
void UNiagaraDataInterfaceFrostySensorGrid::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property &&
		(PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceFrostySensorGrid, SensorCountPerSide)
		|| PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceFrostySensorGrid, GlobalSensorAccuracy)
		|| PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceFrostySensorGrid, GlobalSensorRange)))
	{
		MarkRenderDataDirty();
	}
}
#endif

//////////////////////////////////////////////////////////////////////////

struct FNiagaraDataInterfaceParametersCS_FrostySesnorGrid : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_FrostySesnorGrid, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		BoundsHierarchyParam.Bind(ParameterMap, *(NDIFrostySensorGridLocal::BoundsHierarchyParamName + ParameterInfo.DataInterfaceHLSLSymbol));
		SensorInfoParam.Bind(ParameterMap, *(NDIFrostySensorGridLocal::SensorInfoParamName + ParameterInfo.DataInterfaceHLSLSymbol));
		SensorGridDimensionsParam.Bind(ParameterMap, *(NDIFrostySensorGridLocal::SensorGridDimensionsParamName + ParameterInfo.DataInterfaceHLSLSymbol));
		SensorGridWriteIndexParam.Bind(ParameterMap, *(NDIFrostySensorGridLocal::SensorGridWriteIndexParamName + ParameterInfo.DataInterfaceHLSLSymbol));
		SensorGridReadIndexParam.Bind(ParameterMap, *(NDIFrostySensorGridLocal::SensorGridReadIndexParamName + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FNiagaraDataIntefaceProxyFrostySensorGrid* Proxy = (FNiagaraDataIntefaceProxyFrostySensorGrid*)Context.DataInterface;
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();

		FSensorGridNetworkProxy* NetworkProxy = Proxy->GetNetwork(Context.Batcher);

		NetworkProxy->BeginSimulation(RHICmdList);

		if (BoundsHierarchyParam.IsBound())
		{
			RHICmdList.Transition(FRHITransitionInfo(NetworkProxy->BoundsHierarchy->UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			RHICmdList.SetUAVParameter(ComputeShaderRHI, BoundsHierarchyParam.GetUAVIndex(), NetworkProxy->BoundsHierarchy->UAV);
		}

		if (SensorInfoParam.IsBound())
		{
			FRHIShaderResourceView* SRV = NetworkProxy->EmptyResults->SRV;
			if (NetworkProxy->SensorInfo)
			{
				SRV = NetworkProxy->SensorInfo->SRV;
			}
			SetSRVParameter(RHICmdList, ComputeShaderRHI, SensorInfoParam, SRV);
		}

		SetShaderValue(RHICmdList, ComputeShaderRHI, SensorGridDimensionsParam, FIntVector(NetworkProxy->SensorGridDimensions.X, NetworkProxy->SensorGridDimensions.Y, NetworkProxy->AllocatedOwnerCount));

		{
			const int32* WriteIndex = NetworkProxy->InstanceOwnerWriteIndexMap.Find(Context.SystemInstanceID);
			SetShaderValue(RHICmdList, ComputeShaderRHI, SensorGridWriteIndexParam, WriteIndex ? *WriteIndex : INDEX_NONE);
		}

		{
			const int32* ReadIndex = NetworkProxy->InstanceOwnerReadIndexMap.Find(Context.SystemInstanceID);
			SetShaderValue(RHICmdList, ComputeShaderRHI, SensorGridReadIndexParam, ReadIndex ? *ReadIndex : INDEX_NONE);
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataIntefaceProxyFrostySensorGrid* Proxy = (FNiagaraDataIntefaceProxyFrostySensorGrid*)Context.DataInterface;

		if (BoundsHierarchyParam.IsUAVBound())
		{
			BoundsHierarchyParam.UnsetUAV(RHICmdList, ComputeShaderRHI);
		}

		if (FSensorGridNetworkProxy* NetworkProxy = Proxy->GetNetwork(Context.Batcher))
		{
			NetworkProxy->EndSimulation(RHICmdList);
		}
	}

private:
	LAYOUT_FIELD(FRWShaderParameter, BoundsHierarchyParam);
	LAYOUT_FIELD(FShaderResourceParameter, SensorInfoParam);
	LAYOUT_FIELD(FShaderParameter, SensorGridDimensionsParam);
	LAYOUT_FIELD(FShaderParameter, SensorGridWriteIndexParam);
	LAYOUT_FIELD(FShaderParameter, SensorGridReadIndexParam);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_FrostySesnorGrid);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceFrostySensorGrid, FNiagaraDataInterfaceParametersCS_FrostySesnorGrid);

#undef LOCTEXT_NAMESPACE