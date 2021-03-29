// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceGBuffer.h"
#include "NiagaraTypes.h"
#include "NiagaraWorldManager.h"
#include "ShaderParameterUtils.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraSystemInstance.h"

//////////////////////////////////////////////////////////////////////////

namespace NiagaraDataInterfaceGBufferLocal
{
	struct FGBufferAttribute
	{
		FGBufferAttribute(const TCHAR* InAttributeName, const TCHAR* InAttributeType, FNiagaraTypeDefinition InTypeDef, FText InDescription)
			: AttributeName(InAttributeName)
			, AttributeType(InAttributeType)
			, TypeDef(InTypeDef)
			, Description(InDescription)
		{
			FString TempName;
			TempName = TEXT("Decode");
			TempName += AttributeName;
			ScreenUVFunctionName = FName(TempName);
		}

		const TCHAR*			AttributeName;
		const TCHAR*			AttributeType;
		FName					ScreenUVFunctionName;
		FNiagaraTypeDefinition	TypeDef;
		FText					Description;
	};

	static FText GetDescription_ScreenVelocity()
	{
#if WITH_EDITORONLY_DATA
		return NSLOCTEXT("Niagara", "GBuffer_ScreenVelocity", "Get the screen space velocity in UV space.  This is a per frame value, to get per second you must divide by delta time.");
#else
		return FText::GetEmpty();
#endif
	}

	static FText GetDescription_WorldVelocity()
	{
#if WITH_EDITORONLY_DATA
		return NSLOCTEXT("Niagara", "GBuffer_WorldVelocity", "Get the world space velocity estimate (not accurate due to reconstrucion).  This is a per frame value, to get per second you must divide by delta time.");
#else
		return FText::GetEmpty();
#endif
	}

	static FText GetDescription_SceneColor()
	{
#if WITH_EDITORONLY_DATA
		return NSLOCTEXT("Niagara", "GBuffer_SceneColor", "Gets the current frames scene color buffer, this will not include translucency since we run PostOpaque.");
#else
		return FText::GetEmpty();
#endif
	}

	static TConstArrayView<FGBufferAttribute> GetGBufferAttributes()
	{
		static const TArray<FGBufferAttribute> GBufferAttributes =
		{
			FGBufferAttribute(TEXT("DiffuseColor"),		TEXT("float3"),	FNiagaraTypeDefinition::GetVec3Def(), FText::GetEmpty()),
			FGBufferAttribute(TEXT("WorldNormal"),		TEXT("float3"),	FNiagaraTypeDefinition::GetVec3Def(), FText::GetEmpty()),
			FGBufferAttribute(TEXT("ScreenVelocity"),	TEXT("float3"),	FNiagaraTypeDefinition::GetVec3Def(), GetDescription_ScreenVelocity()),
			FGBufferAttribute(TEXT("WorldVelocity"),	TEXT("float3"),	FNiagaraTypeDefinition::GetVec3Def(), GetDescription_WorldVelocity()),
			FGBufferAttribute(TEXT("BaseColor"),		TEXT("float3"),	FNiagaraTypeDefinition::GetVec3Def(), FText::GetEmpty()),
			//FGBufferAttribute(TEXT("SpecularColor"),	TEXT("float3"),	FNiagaraTypeDefinition::GetVec3Def(), FText::GetEmpty()),
			FGBufferAttribute(TEXT("Metallic"),			TEXT("float"),	FNiagaraTypeDefinition::GetFloatDef(), FText::GetEmpty()),
			FGBufferAttribute(TEXT("Specular"),			TEXT("float"),	FNiagaraTypeDefinition::GetFloatDef(), FText::GetEmpty()),
			FGBufferAttribute(TEXT("Roughness"),		TEXT("float"),	FNiagaraTypeDefinition::GetFloatDef(), FText::GetEmpty()),
			FGBufferAttribute(TEXT("Depth"),			TEXT("float"),	FNiagaraTypeDefinition::GetFloatDef(), FText::GetEmpty()),

			FGBufferAttribute(TEXT("CustomDepth"),		TEXT("float"),	FNiagaraTypeDefinition::GetFloatDef(), FText::GetEmpty()),
			// CustomStencil appears broken currently across the board so not exposing until that's working
			//FGBufferAttribute(TEXT("CustomStencil"),	TEXT("int"),	FNiagaraTypeDefinition::GetIntDef(), FText::GetEmpty()),

			FGBufferAttribute(TEXT("SceneColor"),		TEXT("float4"),	FNiagaraTypeDefinition::GetVec4Def(), GetDescription_SceneColor()),
		};

		return MakeArrayView(GBufferAttributes);
	}
}

//////////////////////////////////////////////////////////////////////////

struct FNiagaraDataIntefaceProxyGBuffer : public FNiagaraDataInterfaceProxy
{
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }
};

struct FNiagaraDataInterfaceParametersCS_GBuffer : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_GBuffer, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		PassUniformBuffer.Bind(ParameterMap, FSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName());
		VelocityTextureParam.Bind(ParameterMap, TEXT("NDIGBuffer_VelocityTexture"));
		VelocityTextureSamplerParam.Bind(ParameterMap, TEXT("NDIGBuffer_VelocityTextureSampler"));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());
		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

		//-Note: Scene textures will not exist in the Mobile rendering path
		TUniformBufferRef<FSceneTextureUniformParameters> SceneTextureUniformParams = GNiagaraViewDataManager.GetSceneTextureUniformParameters();
		check(!PassUniformBuffer.IsBound() || SceneTextureUniformParams);
		SetUniformBufferParameter(RHICmdList, ComputeShaderRHI, PassUniformBuffer, SceneTextureUniformParams);

		FRHISamplerState* VelocitySamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		FRHITexture* VelocityTexture = GNiagaraViewDataManager.GetSceneVelocityTexture() ? GNiagaraViewDataManager.GetSceneVelocityTexture() : GBlackTexture->TextureRHI;
		SetTextureParameter(RHICmdList, ComputeShaderRHI, VelocityTextureParam, VelocityTextureSamplerParam, VelocitySamplerState, VelocityTexture);
	}

private:
	LAYOUT_FIELD(FShaderUniformBufferParameter, PassUniformBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, VelocityTextureParam);
	LAYOUT_FIELD(FShaderResourceParameter, VelocityTextureSamplerParam);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_GBuffer);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceGBuffer, FNiagaraDataInterfaceParametersCS_GBuffer);

//////////////////////////////////////////////////////////////////////////

UNiagaraDataInterfaceGBuffer::UNiagaraDataInterfaceGBuffer(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataIntefaceProxyGBuffer());
}

void UNiagaraDataInterfaceGBuffer::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

void UNiagaraDataInterfaceGBuffer::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	using namespace NiagaraDataInterfaceGBufferLocal;

	TConstArrayView<FGBufferAttribute> GBufferAttributes = GetGBufferAttributes();

	OutFunctions.Reserve(OutFunctions.Num() + (GBufferAttributes.Num()));

	for ( const FGBufferAttribute& Attribute : GBufferAttributes )
	{
		FNiagaraFunctionSignature& Signature = OutFunctions.AddDefaulted_GetRef();
		Signature.Name = Attribute.ScreenUVFunctionName;
#if WITH_EDITORONLY_DATA
		Signature.Description = Attribute.Description;
#endif
		Signature.bMemberFunction = true;
		Signature.bRequiresContext = false;
		Signature.bSupportsCPU = false;
		Signature.bExperimental = true;
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("GBufferInterface")));
		Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("ScreenUV")));
		Signature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid")));
		Signature.Outputs.Add(FNiagaraVariable(Attribute.TypeDef, Attribute.AttributeName));
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceGBuffer::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!Super::AppendCompileHash(InVisitor))
		return false;

	FSHAHash Hash = GetShaderFileHash((TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceGBuffer.ush")), EShaderPlatform::SP_PCD3D_SM5);
	InVisitor->UpdateString(TEXT("NiagaraDataInterfaceGBufferHLSLSource"), Hash.ToString());
	return true;
}

void UNiagaraDataInterfaceGBuffer::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Plugin/FX/Niagara/Private/NiagaraDataInterfaceGBuffer.ush\"\n");
}

bool UNiagaraDataInterfaceGBuffer::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	using namespace NiagaraDataInterfaceGBufferLocal;

	TMap<FString, FStringFormatArg> ArgsSample =
	{
		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
	};

	for ( const FGBufferAttribute& Attribute : GetGBufferAttributes() )
	{
		if (FunctionInfo.DefinitionName == Attribute.ScreenUVFunctionName)
		{
			ArgsSample.Emplace(TEXT("AttributeName"), Attribute.AttributeName);
			ArgsSample.Emplace(TEXT("AttributeType"), Attribute.AttributeType);

			static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(float2 ScreenUV, out bool IsValid, out {AttributeType} {AttributeName}) { DIGBuffer_Decode{AttributeName}(ScreenUV, IsValid, {AttributeName}); }\n");
			OutHLSL += FString::Format(FormatSample, ArgsSample);
			return true;
		}
	}

	return false;
}
#endif
