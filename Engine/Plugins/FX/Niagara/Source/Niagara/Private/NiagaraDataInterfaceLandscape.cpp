// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceLandscape.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraComponent.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "Landscape.h"
#include "LandscapeProxy.h"

#define LOCTEXT_NAMESPACE "UNiagaraDataInterfaceLandscape"

const FName UNiagaraDataInterfaceLandscape::GetHeightName(TEXT("GetHeight"));
const FName UNiagaraDataInterfaceLandscape::GetNumCellsName(TEXT("GetNumCells"));

const FString UNiagaraDataInterfaceLandscape::LandscapeTextureName(TEXT("LandscapeTexture_"));
const FString UNiagaraDataInterfaceLandscape::SamplerName(TEXT("Sampler_"));
const FString UNiagaraDataInterfaceLandscape::NumCellsBaseName(TEXT("NumCells_"));
const FString UNiagaraDataInterfaceLandscape::WorldToActorBaseName(TEXT("WorldToActor_"));

UNiagaraDataInterfaceLandscape::UNiagaraDataInterfaceLandscape(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)		
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyLandscape());
}

void UNiagaraDataInterfaceLandscape::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}	
}

#if WITH_EDITOR

void UNiagaraDataInterfaceLandscape::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);	
}

#endif

bool UNiagaraDataInterfaceLandscape::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceLandscape* DestinationLandscape = CastChecked<UNiagaraDataInterfaceLandscape>(Destination);
	DestinationLandscape->SourceLandscape = SourceLandscape;	

	return true;
}

bool UNiagaraDataInterfaceLandscape::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceLandscape* OtherLandscape = CastChecked<const UNiagaraDataInterfaceLandscape>(Other);
	return OtherLandscape->SourceLandscape == SourceLandscape;
}

void UNiagaraDataInterfaceLandscape::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumCellsName;
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Landscape")));		
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("NumCells")));
		//Sig.Owner = *GetName();

		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetHeightName;
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Landscape")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("WorldPos")));		
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));
		//Sig.Owner = *GetName();

		OutFunctions.Add(Sig);
	}
}

// #todo(dmp): this is gpu only for now
void UNiagaraDataInterfaceLandscape::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == GetNumCellsName)
	{
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceLandscape::EmptyVMFunction);
	}
	else if (BindingInfo.Name == GetHeightName)
	{
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceLandscape::EmptyVMFunction);
	}
}

bool UNiagaraDataInterfaceLandscape::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if (FunctionInfo.DefinitionName == GetNumCellsName)
	{
		FString NumCellsVar = NumCellsBaseName + ParamInfo.DataInterfaceHLSLSymbol;
		OutHLSL += TEXT("void ") + FunctionInfo.InstanceName + TEXT("(out int2 Out_Value) \n{\n");
		OutHLSL += TEXT("\t Out_Value = ") + NumCellsVar + TEXT(";\n");
		OutHLSL += TEXT("\n}\n");
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetHeightName)
	{
		static const TCHAR* FormatBounds = TEXT(R"(
			void {FunctionName}(float3 In_WorldPos, out float Out_Val)
			{
				float3 ActorPos = mul(float4(In_WorldPos, 1.0), {WorldToActorTransform}).xyz;				
				float2 UV = (ActorPos.xy + .5) / {NumCells};
				Out_Val = {Grid}.SampleLevel({SamplerName}, UV, 0);
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Grid"), LandscapeTextureName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("SamplerName"),    SamplerName + ParamInfo.DataInterfaceHLSLSymbol },			
			{TEXT("NumCells"),    NumCellsBaseName + ParamInfo.DataInterfaceHLSLSymbol },
			{TEXT("WorldToActorTransform"),    WorldToActorBaseName + ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	return false;
}

void UNiagaraDataInterfaceLandscape::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	FString HLSLLandscapeTextureName = LandscapeTextureName + ParamInfo.DataInterfaceHLSLSymbol;
	FString HLSLSamplerName = SamplerName + ParamInfo.DataInterfaceHLSLSymbol;
	OutHLSL += TEXT("Texture2D ") + HLSLLandscapeTextureName + TEXT(";\n");
	OutHLSL += TEXT("SamplerState ") + HLSLSamplerName + TEXT(";\n");
	OutHLSL += TEXT("int2 ") + NumCellsBaseName + ParamInfo.DataInterfaceHLSLSymbol + TEXT(";\n");
	OutHLSL += TEXT("float4x4 ") + WorldToActorBaseName + ParamInfo.DataInterfaceHLSLSymbol + TEXT(";\n");	
}

bool UNiagaraDataInterfaceLandscape::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	check(Proxy);

	// Creates empty instance data since the landscape info isn't available until after the first tick

	FNDILandscapeData_GameThread* InstanceData = new (PerInstanceData) FNDILandscapeData_GameThread();

	FNiagaraDataInterfaceProxyLandscape* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyLandscape>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[RT_Proxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
	{
		check(!RT_Proxy->SystemInstancesToProxyData_RT.Contains(InstanceID));
		FNDILandscapeData_RenderThread* TargetData = &RT_Proxy->SystemInstancesToProxyData_RT.Add(InstanceID);
	});

	return true;
}

void UNiagaraDataInterfaceLandscape::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDILandscapeData_GameThread* InstanceData = static_cast<FNDILandscapeData_GameThread*>(PerInstanceData);

	InstanceData->~FNDILandscapeData_GameThread();

	FNiagaraDataInterfaceProxyLandscape* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyLandscape>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[RT_Proxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
	{
		//check(ThisProxy->SystemInstancesToProxyData.Contains(InstanceID));
		RT_Proxy->SystemInstancesToProxyData_RT.Remove(InstanceID);
	}
	);
}

// Physics data for landscape that we use to fill the texture with is only available on tick, so we fill it once at runtime here
bool UNiagaraDataInterfaceLandscape::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FNDILandscapeData_GameThread* InstanceData = static_cast<FNDILandscapeData_GameThread*>(PerInstanceData);

	FNiagaraDataInterfaceProxyLandscape* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyLandscape>();

	ALandscape* TheLandscape = static_cast<ALandscape*>(SourceLandscape);

	if (InstanceData->IsSet || !TheLandscape || !TheLandscape->CollisionComponents[0]->HeightfieldRef.IsValid() || TheLandscape->CollisionComponents[0]->HeightfieldRef->Heightfield == nullptr)
	{
		return false;
	}

	int32 SizeX = 0;
	int32 SizeY = 0;
	TArray<float> HeightValues;
	TheLandscape->GetHeightValues(SizeX, SizeY, HeightValues);

	if (HeightValues.Num() == 0)
	{
		return  false;
	}

	// check for max resolution and fail if too high
	// #todo(dmp): downsample to some resolution
	int32 MaxDim = 16384;
	if (SizeX > MaxDim || SizeY > MaxDim)
	{
		UE_LOG(LogNiagara, Error, TEXT("Landscape grid dimensions execeed maximum of 16384 %s"), *FNiagaraUtilities::SystemInstanceIDToString(SystemInstance->GetId()));
		return false;
	}

	FMatrix WorldToLocal = TheLandscape->GetTransform().ToMatrixWithScale().Inverse();

	InstanceData->IsSet = true;
	InstanceData->NumCells = FIntPoint(SizeX, SizeY);
	InstanceData->WorldToActorTransform = WorldToLocal;

	ENQUEUE_RENDER_COMMAND(FPushDILandscapeToRT) (
		[RT_Proxy, SizeX, SizeY, HeightValues, WorldToLocal, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
		{
			FNDILandscapeData_RenderThread* ProxyData = RT_Proxy->SystemInstancesToProxyData_RT.Find(InstanceID);

			ProxyData->IsSet = HeightValues.Num() > 0;

			// allocate the new texture based on the size of the landscape
			ProxyData->LandscapeTextureBuffer = TUniquePtr<FTextureReadBuffer2D>(new FTextureReadBuffer2D());
			ProxyData->LandscapeTextureBuffer->Initialize(4, SizeX, SizeY, EPixelFormat::PF_R32_FLOAT);

			// fill texture will landscape data derived outside of here
			uint32 DestStride = sizeof(float);
			float* DestArray = (float*)RHILockTexture2D(ProxyData->LandscapeTextureBuffer->Buffer, 0, RLM_WriteOnly, DestStride, true);

			memcpy(DestArray, HeightValues.GetData(), HeightValues.Num() * sizeof(float));

			RHIUnlockTexture2D(ProxyData->LandscapeTextureBuffer->Buffer, 0, true);

			ProxyData->NumCells.X = SizeX;
			ProxyData->NumCells.Y = SizeY;
			ProxyData->WorldToActorTransform = WorldToLocal;
		}
	);

	return false;
}

struct FNiagaraDataInterfaceParametersCS_Landscape : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Landscape, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FString TexName = UNiagaraDataInterfaceLandscape::LandscapeTextureName + ParameterInfo.DataInterfaceHLSLSymbol;
		FString SampleName = (UNiagaraDataInterfaceLandscape::SamplerName + ParameterInfo.DataInterfaceHLSLSymbol);
		LandscapeTextureParam.Bind(ParameterMap, *TexName);
		SamplerParam.Bind(ParameterMap, *SampleName);
		
		if (!LandscapeTextureParam.IsBound())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Binding failed for FNiagaraDataInterfaceParametersCS_Landscape Landscape Texture %s. Was it optimized out?"), *TexName)
		}

		if (!SamplerParam.IsBound())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Binding failed for FNiagaraDataInterfaceParametersCS_Landscape Sampler %s. Was it optimized out?"), *SampleName)
		}

		NumCells.Bind(ParameterMap, *(UNiagaraDataInterfaceLandscape::NumCellsBaseName + ParameterInfo.DataInterfaceHLSLSymbol));

		WorldToActorTransform.Bind(ParameterMap, *(UNiagaraDataInterfaceLandscape::WorldToActorBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();

		FNiagaraDataInterfaceProxyLandscape* RT_Proxy = static_cast<FNiagaraDataInterfaceProxyLandscape*>(Context.DataInterface);

		if (RT_Proxy && RT_Proxy->SystemInstancesToProxyData_RT.Find(Context.SystemInstance))
		{
			FNDILandscapeData_RenderThread* ProxyData = RT_Proxy->SystemInstancesToProxyData_RT.Find(Context.SystemInstance);
			
			if (ProxyData->LandscapeTextureBuffer)
			{
				FRHISamplerState* SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				SetSamplerParameter(RHICmdList, ComputeShaderRHI, SamplerParam, SamplerState);

				SetSRVParameter(RHICmdList, Context.Shader.GetComputeShader(), LandscapeTextureParam, ProxyData->LandscapeTextureBuffer->SRV);
				SetShaderValue(RHICmdList, ComputeShaderRHI, NumCells, ProxyData->NumCells);
				SetShaderValue(RHICmdList, ComputeShaderRHI, WorldToActorTransform, ProxyData->WorldToActorTransform);
			}
			else
			{
				SetSamplerParameter(RHICmdList, ComputeShaderRHI, SamplerParam, GBlackTextureWithSRV->SamplerStateRHI);
				SetSRVParameter(RHICmdList, Context.Shader.GetComputeShader(), LandscapeTextureParam, GBlackTextureWithSRV->ShaderResourceViewRHI);
				SetShaderValue(RHICmdList, ComputeShaderRHI, NumCells, FIntPoint(0, 0));
				SetShaderValue(RHICmdList, ComputeShaderRHI, WorldToActorTransform, FMatrix::Identity);
			}
		}
		else
		{			
			SetSamplerParameter(RHICmdList, ComputeShaderRHI, SamplerParam, GBlackTextureWithSRV->SamplerStateRHI);
			SetSRVParameter(RHICmdList, Context.Shader.GetComputeShader(), LandscapeTextureParam, GBlackTextureWithSRV->ShaderResourceViewRHI);
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumCells, FIntPoint(0,0));
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldToActorTransform, FMatrix::Identity);
		}
	}
private:

	LAYOUT_FIELD(FShaderResourceParameter, LandscapeTextureParam);
	LAYOUT_FIELD(FShaderResourceParameter, SamplerParam);
	LAYOUT_FIELD(FShaderParameter, NumCells);
	LAYOUT_FIELD(FShaderParameter, WorldToActorTransform);
};


IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Landscape);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceLandscape, FNiagaraDataInterfaceParametersCS_Landscape);
#undef LOCTEXT_NAMESPACE