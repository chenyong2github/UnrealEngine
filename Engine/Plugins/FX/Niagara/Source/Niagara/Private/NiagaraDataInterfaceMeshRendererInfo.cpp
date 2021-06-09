// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceMeshRendererInfo.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraMeshRendererProperties.h"
#include "ShaderParameterUtils.h"
#include "NiagaraStats.h"
#include "NiagaraRenderer.h"
#include "Engine/StaticMesh.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceMeshRendererInfo"

class FNDIMeshRendererInfoGPUData;
using FNDIMeshRendererInfoGPUDataRef = TSharedPtr<FNDIMeshRendererInfoGPUData, ESPMode::ThreadSafe>;
using FNDIMeshRendererInfoGPUDataPtr = TSharedPtr<FNDIMeshRendererInfoGPUData, ESPMode::ThreadSafe>;

namespace NDIMeshRendererInfoInternal
{
	const FName GetNumMeshesName("GetNumMeshes");
	const FName GetMeshLocalBoundsName("GetMeshLocalBounds");

	const FString NumMeshesPrefix = TEXT("NumMeshes_");
	const FString MeshDataBufferPrefix = TEXT("MeshDataBuffer_");
}

enum class ENDIMeshRendererInfoVersion : uint32
{
	InitialVersion = 0,
	AddSizeToMeshLocalBounds,

	VersionPlusOne,
	LatestVersion = VersionPlusOne - 1
};

/** Holds information (for both CPU and GPU) accessed by this data interface for a given renderer */
class FNDIMeshRendererInfo
{
public:
	struct FMeshData
	{
		FVector MinLocalBounds = FVector(ForceInitToZero);
		FVector MaxLocalBounds = FVector(ForceInitToZero);
	};

	using FMeshDataArray = TArray<FMeshData>;

	void AddRef() { ++RefCount; }
	const FMeshDataArray& GetMeshData() { return MeshData; }
	FNDIMeshRendererInfoGPUDataRef GetOrCreateGPUData();

	static FNDIMeshRendererInfoRef Acquire(UNiagaraMeshRendererProperties& Renderer);
	static void Release(UNiagaraMeshRendererProperties& Renderer, FNDIMeshRendererInfoPtr& Info);

private:
	FMeshDataArray MeshData;
	FNDIMeshRendererInfoGPUDataPtr GPUData;
	uint32 RefCount = 0;
#if WITH_EDITOR
	FDelegateHandle OnChangedHandle;
#endif

	using FCacheMap = TMap<UNiagaraMeshRendererProperties*, FNDIMeshRendererInfoRef>;
	static FCacheMap CachedData;

	static void ResetMeshData(const UNiagaraMeshRendererProperties& Renderer, FMeshDataArray& OutArray);
};

/** This is a resource that holds the static GPU buffer data of the info for a given renderer */
class FNDIMeshRendererInfoGPUData : public FRenderResource
{
public:
	FNDIMeshRendererInfoGPUData(const FNDIMeshRendererInfo::FMeshDataArray& InMeshData)
		: MeshData(InMeshData)
	{}

	uint32 GetNumMeshes() { return MeshData.Num(); }
	FVertexBufferRHIRef GetMeshDataBufferRHI() const { return BufferMeshDataRHI; }
	FShaderResourceViewRHIRef GetMeshDataBufferSRV() const { return BufferMeshDataSRV; }

	virtual void InitRHI() override final
	{
		FRHIResourceCreateInfo CreateInfo;
		void* BufferData = nullptr;
		uint32 SizeByte = MeshData.Num() * sizeof(FVector) * 2;
		
		if (SizeByte > 0)
		{
			BufferMeshDataRHI = RHICreateAndLockVertexBuffer(SizeByte, BUF_Static | BUF_ShaderResource, CreateInfo, BufferData);		
			FVector* BufferDataVector = reinterpret_cast<FVector*>(BufferData);
			for (const auto& Mesh : MeshData)
			{
				*BufferDataVector++ = Mesh.MinLocalBounds;
				*BufferDataVector++ = Mesh.MaxLocalBounds;
			}
			RHIUnlockVertexBuffer(BufferMeshDataRHI);

			BufferMeshDataSRV = RHICreateShaderResourceView(BufferMeshDataRHI, sizeof(float), PF_R32_FLOAT);
		}

#if STATS
		GPUMemory = SizeByte;
		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, SizeByte);
#endif
	}

	virtual void ReleaseRHI() override final
	{
		BufferMeshDataSRV.SafeRelease();
		BufferMeshDataRHI.SafeRelease();
		
#if STATS
		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, GPUMemory);
		GPUMemory = 0;
#endif
	}

private:
	FVertexBufferRHIRef BufferMeshDataRHI = nullptr;
	FShaderResourceViewRHIRef BufferMeshDataSRV = nullptr;
	const FNDIMeshRendererInfo::FMeshDataArray& MeshData; // cached from FNDIMeshRendererInfo, which is guaranteed to live longer than we are
#if STATS
	uint32 GPUMemory = 0;
#endif
};

/** The render thread proxy of the data interface */
struct FNDIMeshRendererInfoProxy : public FNiagaraDataInterfaceProxy
{
	FNDIMeshRendererInfoGPUDataPtr GPUData;

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }
};

/** The parameters used by the data interface in GPU emitters */
struct FNiagaraDataInterfaceParametersCS_MeshRendererInfo : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_MeshRendererInfo, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		NumMeshesParam.Bind(ParameterMap, *(NDIMeshRendererInfoInternal::NumMeshesPrefix + ParameterInfo.DataInterfaceHLSLSymbol));
		MeshDataBuffer.Bind(ParameterMap, *(NDIMeshRendererInfoInternal::MeshDataBufferPrefix + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

		auto Proxy = static_cast<FNDIMeshRendererInfoProxy*>(Context.DataInterface);
		FShaderResourceViewRHIRef MeshDataBufferSRV;
		uint32 NumMeshes = 0;
		if (Proxy->GPUData.IsValid())
		{
			NumMeshes = Proxy->GPUData->GetNumMeshes();
			MeshDataBufferSRV = Proxy->GPUData->GetMeshDataBufferSRV();
		}

		if (!MeshDataBufferSRV.IsValid())
		{
			MeshDataBufferSRV = FNiagaraRenderer::GetDummyFloatBuffer();
		}
		
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumMeshesParam, NumMeshes);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshDataBuffer, MeshDataBufferSRV);
	}

private:
	LAYOUT_FIELD(FShaderParameter, NumMeshesParam);
	LAYOUT_FIELD(FShaderResourceParameter, MeshDataBuffer);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_MeshRendererInfo);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceMeshRendererInfo, FNiagaraDataInterfaceParametersCS_MeshRendererInfo);

///////////////////////////////////////////////////////////////////////////////////////////////

FNDIMeshRendererInfo::FCacheMap FNDIMeshRendererInfo::CachedData;

FNDIMeshRendererInfoGPUDataRef FNDIMeshRendererInfo::GetOrCreateGPUData()
{
	if (!GPUData.IsValid())
	{
		GPUData = MakeShared<FNDIMeshRendererInfoGPUData, ESPMode::ThreadSafe>(MeshData);
		BeginInitResource(GPUData.Get());
	}
	return GPUData.ToSharedRef();
}

FNDIMeshRendererInfoRef FNDIMeshRendererInfo::Acquire(UNiagaraMeshRendererProperties& Renderer)
{
	FNDIMeshRendererInfoRef* RefPtr = CachedData.Find(&Renderer);
	if (RefPtr)
	{
		++(*RefPtr)->RefCount;
		return *RefPtr;
	}

	FNDIMeshRendererInfoRef Info = MakeShared<FNDIMeshRendererInfo, ESPMode::ThreadSafe>();
	++Info->RefCount;

	ResetMeshData(Renderer, Info->MeshData);

#if WITH_EDITOR
	Info->OnChangedHandle = Renderer.OnChanged().AddLambda(
		[&Renderer, Info]()
		{
			if (Info->GPUData.IsValid())
			{
				// The render thread could be accessing the mesh data, so we have to update it in a render cmd
				FMeshDataArray TempArray;
				ResetMeshData(Renderer, TempArray);
				ENQUEUE_RENDER_COMMAND(FDIMeshRendererUpdateMeshDataBuffer)
				(
					[Info, MeshData = MoveTemp(TempArray)](FRHICommandList& RHICmdList)
					{
						Info->MeshData = MeshData;
						if (Info->GPUData.IsValid())
						{
							// Re-create the buffers
							Info->GPUData->ReleaseRHI();
							Info->GPUData->InitRHI();
						}
					}
				);
			}
			else
			{
				// We've never pushed our data to the RT so we're safe to stomp this data without worry of data race
				ResetMeshData(Renderer, Info->MeshData);
			}
		}
	);
#endif
	
	CachedData.Add(&Renderer, Info);
	return Info;
}

void FNDIMeshRendererInfo::Release(UNiagaraMeshRendererProperties& Renderer, FNDIMeshRendererInfoPtr& Info)
{
	if (!Info.IsValid())
	{
		return;
	}

	check(Info->RefCount > 0);
	if (--Info->RefCount == 0)
	{
		// Last reference is out, we can stop caching it and release it
		CachedData.Remove(&Renderer);

#if WITH_EDITOR
		Renderer.OnChanged().Remove(Info->OnChangedHandle);
		Info->OnChangedHandle.Reset();
#endif

		if (Info->GPUData.IsValid())
		{
			BeginReleaseResource(Info->GPUData.Get());

			// We have to release this on the render thread because a render command could be holding references
			ENQUEUE_RENDER_COMMAND(FDIMeshRendererInfoReleaseInfo)
			(
				[Info_RT = MoveTemp(Info)](FRHICommandList& RHICmdList) mutable
				{
					Info_RT->GPUData = nullptr;
					Info_RT = nullptr;
				}
			);
		}
	}

	Info = nullptr;
}

void FNDIMeshRendererInfo::ResetMeshData(const UNiagaraMeshRendererProperties& Renderer, FMeshDataArray& OutMeshData)
{
	OutMeshData.Reset(Renderer.Meshes.Num());
	for (const auto& MeshSlot : Renderer.Meshes)
	{
		FMeshData& NewMeshData = OutMeshData.AddDefaulted_GetRef();
		NewMeshData.MinLocalBounds = FVector(EForceInit::ForceInitToZero);
		NewMeshData.MaxLocalBounds = FVector(EForceInit::ForceInitToZero);

		if (MeshSlot.Mesh)
		{
			const FBox LocalBounds = MeshSlot.Mesh->GetExtendedBounds().GetBox();
			if (LocalBounds.IsValid)
			{
				// Scale the local bounds if there's a scale on this slot
				// TODO: Should we also apply the pivot offset if it's in mesh space? Seems like that might be strange
				NewMeshData.MinLocalBounds = LocalBounds.Min * MeshSlot.Scale;
				NewMeshData.MaxLocalBounds = LocalBounds.Max * MeshSlot.Scale;
			}
		}
	}	
}

////////////////////////////////////////////////////////////////////////////////////////////////

UNiagaraDataInterfaceMeshRendererInfo::UNiagaraDataInterfaceMeshRendererInfo(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNDIMeshRendererInfoProxy);
	MarkRenderDataDirty();
}

void UNiagaraDataInterfaceMeshRendererInfo::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		
		// We can't allow user variables of this type because it will cause components to have external reference (the renderer)
		Flags &= ~ENiagaraTypeRegistryFlags::AllowUserVariable;

		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}	
}

void UNiagaraDataInterfaceMeshRendererInfo::PostLoad()
{
	Super::PostLoad();

	if (MeshRenderer)
	{
		MeshRenderer->ConditionalPostLoad();

		Info = FNDIMeshRendererInfo::Acquire(*MeshRenderer);
		MarkRenderDataDirty();
	}
}

void UNiagaraDataInterfaceMeshRendererInfo::BeginDestroy()
{
	Super::BeginDestroy();

	if (MeshRenderer)
	{
		FNDIMeshRendererInfo::Release(*MeshRenderer, Info);
	}
}

#if WITH_EDITOR

void UNiagaraDataInterfaceMeshRendererInfo::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (MeshRenderer && PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceMeshRendererInfo, MeshRenderer))
	{		
		FNDIMeshRendererInfo::Release(*MeshRenderer, Info);
	}
}

void UNiagaraDataInterfaceMeshRendererInfo::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// If coming from undo, property will be nullptr and since we copy the info, we need to reacquire if new.
	if (PropertyChangedEvent.Property == nullptr || (PropertyChangedEvent.Property &&
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceMeshRendererInfo, MeshRenderer)))
	{
		if (MeshRenderer)
		{
			Info = FNDIMeshRendererInfo::Acquire(*MeshRenderer);
		}
		MarkRenderDataDirty();
	}
}

#endif // WITH_EDITOR

void UNiagaraDataInterfaceMeshRendererInfo::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature& Signature = OutFunctions.AddDefaulted_GetRef();
		Signature.Name = NDIMeshRendererInfoInternal::GetNumMeshesName;
#if WITH_EDITORONLY_DATA
		Signature.FunctionVersion = (uint32)ENDIMeshRendererInfoVersion::LatestVersion;
		Signature.Description = LOCTEXT("GetNumMeshesInRendererDesc", "Retrieves the number of meshes on the mesh renderer by index, or -1 if the index is invalid.");
#endif
		Signature.bMemberFunction = true;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("MeshRendererInfo")));
		Signature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("OutNumMeshes")));
	}

	{
		FNiagaraFunctionSignature& Signature = OutFunctions.AddDefaulted_GetRef();
		Signature.Name = NDIMeshRendererInfoInternal::GetMeshLocalBoundsName;
#if WITH_EDITORONLY_DATA
		Signature.FunctionVersion = (uint32)ENDIMeshRendererInfoVersion::LatestVersion;
		Signature.Description = LOCTEXT("GetMeshLocalBoundsDesc", "Retrieves the local bounds of the specified mesh's vertices.");
#endif
		Signature.bMemberFunction = true;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("MeshRendererInfo")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("MeshIndex")));
		Signature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("OutMinBounds")));
		Signature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("OutMaxBounds")));
		Signature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("OutSize")));
	}
}

void UNiagaraDataInterfaceMeshRendererInfo::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	if (BindingInfo.Name == NDIMeshRendererInfoInternal::GetNumMeshesName)
	{
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceMeshRendererInfo::GetNumMeshes);
	}
	else if (BindingInfo.Name == NDIMeshRendererInfoInternal::GetMeshLocalBoundsName)
	{
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceMeshRendererInfo::GetMeshLocalBounds);
	}
}

bool UNiagaraDataInterfaceMeshRendererInfo::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	auto OtherTyped = CastChecked<const UNiagaraDataInterfaceMeshRendererInfo>(Other);
	return MeshRenderer == OtherTyped->MeshRenderer;
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceMeshRendererInfo::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	const FStringFormatOrderedArguments Args = {
		ParamInfo.DataInterfaceHLSLSymbol
	};
	OutHLSL += FString::Format(TEXT(R"(
		uint NumMeshes_{0};
		Buffer<float> MeshDataBuffer_{0};
	)"),
	Args);
}

bool UNiagaraDataInterfaceMeshRendererInfo::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	const FStringFormatNamedArguments Args = {
		{TEXT("FuncName"), FunctionInfo.InstanceName},
		{TEXT("DIName"), ParamInfo.DataInterfaceHLSLSymbol}
	};

	if (FunctionInfo.DefinitionName == NDIMeshRendererInfoInternal::GetNumMeshesName)
	{
		OutHLSL += FString::Format(TEXT(R"(
			void {FuncName}(out int OutNumMeshes)
			{
				OutNumMeshes = NumMeshes_{DIName};
			}
			)"),
			Args);

		return true;
	}
	else if (FunctionInfo.DefinitionName == NDIMeshRendererInfoInternal::GetMeshLocalBoundsName)
	{
		OutHLSL += FString::Format(TEXT(R"(
			void {FuncName}(in int MeshIndex, out float3 OutMinBounds, out float3 OutMaxBounds, out float3 OutSize)
			{
				OutMinBounds = (float3)0;
				OutMaxBounds = (float3)0;
				OutSize = (float3)0;
				if (NumMeshes_{DIName} > 0)
				{
					const uint MeshDataNumFloats = 6;
					const uint BufferOffs = clamp(MeshIndex, 0, int(NumMeshes_{DIName} - 1)) * MeshDataNumFloats;
					OutMinBounds = float3(
						MeshDataBuffer_{DIName}[BufferOffs + 0],
						MeshDataBuffer_{DIName}[BufferOffs + 1],
						MeshDataBuffer_{DIName}[BufferOffs + 2]
					);
					OutMaxBounds = float3(
						MeshDataBuffer_{DIName}[BufferOffs + 3],
						MeshDataBuffer_{DIName}[BufferOffs + 4],
						MeshDataBuffer_{DIName}[BufferOffs + 5]
					);
					OutSize = OutMaxBounds - OutMinBounds;
				}
			}
			)"),
			Args);

		return true;
	}

	return false;
}
#endif

#if WITH_EDITOR

bool UNiagaraDataInterfaceMeshRendererInfo::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	bool bWasChanged = false;
	if (FunctionSignature.Name == NDIMeshRendererInfoInternal::GetMeshLocalBoundsName && FunctionSignature.Outputs.Num() == 2)
	{
		FunctionSignature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("OutSize")));
		bWasChanged = true;
	}

	FunctionSignature.FunctionVersion = (uint32)ENDIMeshRendererInfoVersion::LatestVersion;

	return bWasChanged;
}

void UNiagaraDataInterfaceMeshRendererInfo::GetFeedback(UNiagaraSystem* InAsset, UNiagaraComponent* InComponent, TArray<FNiagaraDataInterfaceError>& OutErrors, TArray<FNiagaraDataInterfaceFeedback>& OutWarnings,
	TArray<FNiagaraDataInterfaceFeedback>& OutInfo)
{
	if (MeshRenderer == nullptr)
	{
		FNiagaraDataInterfaceFeedback NoMeshRendererSelectedWarning(
			LOCTEXT("NoRendererSelectedWarning", "A Mesh Renderer applied to an emitter in this system is expected to be selected here"),
			LOCTEXT("NoRendererSelectedWarningSummary", "No Mesh Renderer selected"),
			FNiagaraDataInterfaceFix()
		);
		OutWarnings.Add(NoMeshRendererSelectedWarning);
	}
	else 
	{
		if (!MeshRenderer->GetIsEnabled())
		{
			FNiagaraDataInterfaceFeedback MeshRendererDisabledWarning(
				LOCTEXT("RendererDisabledWarning", "The selected Mesh Renderer is disabled"),
				LOCTEXT("RendererDisabledWarningSummary", "Mesh Renderer is disabled"),
				FNiagaraDataInterfaceFix::CreateLambda(
					[this]()
					{
						MeshRenderer->SetIsEnabled(true);
						return true;
					}
				)
			);
			OutWarnings.Add(MeshRendererDisabledWarning);
		}
	}
}

#endif // WITH_EDITOR

bool UNiagaraDataInterfaceMeshRendererInfo::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}


	auto OtherTyped = CastChecked<UNiagaraDataInterfaceMeshRendererInfo>(Destination);
	
	if (OtherTyped->MeshRenderer)
	{
		FNDIMeshRendererInfo::Release(*OtherTyped->MeshRenderer, OtherTyped->Info);
	}
	
	OtherTyped->MeshRenderer = MeshRenderer;	
	OtherTyped->Info = Info;

	// Check to add a reference to the per-renderer data
	if (Info.IsValid())
	{
		Info->AddRef();
	}
	else if (MeshRenderer != nullptr && !EnumHasAnyFlags(OtherTyped->GetFlags(), RF_NeedPostLoad))
	{
		// This is currently necessary because, for some reason, it's possible that data interfaces that have not been post-loaded to be copied to
		// another data interface object that has, and was previously causing this data to never get acquired on the copy
		OtherTyped->Info = FNDIMeshRendererInfo::Acquire(*MeshRenderer);
	}
	return true;
}

void UNiagaraDataInterfaceMeshRendererInfo::PushToRenderThreadImpl()
{
	if (MeshRenderer && Info.IsValid())
	{
		auto TypedProxy = GetProxyAs<FNDIMeshRendererInfoProxy>();
		ENQUEUE_RENDER_COMMAND(FDIMeshRendererInfoPushToRT)
		(
			[TypedProxy, GPUData_RT=Info->GetOrCreateGPUData()](FRHICommandList& RHICmdList)
			{
				TypedProxy->GPUData = GPUData_RT;
			}
		);
	}
}

void UNiagaraDataInterfaceMeshRendererInfo::GetNumMeshes(FVectorVMContext& Context)
{
	FNDIOutputParam<int32> OutNum(Context);

	int32 NumMeshes = Info.IsValid() ? Info->GetMeshData().Num() : 0;
	for (int32 Instance = 0; Instance < Context.GetNumInstances(); ++Instance)
	{
		OutNum.SetAndAdvance(NumMeshes);
	}
}

void UNiagaraDataInterfaceMeshRendererInfo::GetMeshLocalBounds(FVectorVMContext& Context)
{
	FNDIInputParam<int32> InMeshIdx(Context);
	FNDIOutputParam<FVector> OutMinBounds(Context);
	FNDIOutputParam<FVector> OutMaxBounds(Context);
	FNDIOutputParam<FVector> OutSize(Context);

	for (int32 Instance = 0; Instance < Context.GetNumInstances(); ++Instance)
	{
		FVector MinLocalBounds(ForceInitToZero);
		FVector MaxLocalBounds(ForceInitToZero);
		if (Info.IsValid())
		{
			const FNDIMeshRendererInfo::FMeshDataArray& MeshData = Info->GetMeshData();
			if (MeshData.Num() > 0)
			{
				const int32 MeshIdx = FMath::Clamp(InMeshIdx.GetAndAdvance(), 0, MeshData.Num() - 1);			
				const FNDIMeshRendererInfo::FMeshData& Mesh = MeshData[MeshIdx];
				MinLocalBounds = Mesh.MinLocalBounds;
				MaxLocalBounds = Mesh.MaxLocalBounds;
			}
		}
		OutMinBounds.SetAndAdvance(MinLocalBounds);
		OutMaxBounds.SetAndAdvance(MaxLocalBounds);
		OutSize.SetAndAdvance(MaxLocalBounds - MinLocalBounds);
	}
}

#undef LOCTEXT_NAMESPACE