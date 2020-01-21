// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceOcclusion.h"
#include "NiagaraTypes.h"
#include "NiagaraWorldManager.h"
#include "ShaderParameterUtils.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraSystemInstance.h"

const FName UNiagaraDataInterfaceOcclusion::GetCameraOcclusionRectangleName(TEXT("QueryOcclusionFactorWithRectangleGPU"));
const FName UNiagaraDataInterfaceOcclusion::GetCameraOcclusionCircleName(TEXT("QueryOcclusionFactorWithCircleGPU"));

UNiagaraDataInterfaceOcclusion::UNiagaraDataInterfaceOcclusion(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy = MakeShared<FNiagaraDataIntefaceProxyOcclusionQuery, ESPMode::ThreadSafe>();
}

void UNiagaraDataInterfaceOcclusion::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

void UNiagaraDataInterfaceOcclusion::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature Sig;
	Sig.Name = GetCameraOcclusionRectangleName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetCameraOcclusionRectFunctionDescription", "This function returns the occlusion factor of a sprite. It samples the depth buffer in a rectangular grid around the given world position and compares each sample with the camera distance.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Center World Position")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Window Width World")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Window Height World")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Steps Per Line")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Visibility Fraction")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Fraction")));
	OutFunctions.Add(Sig);

	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetCameraOcclusionCircleName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetCameraOcclusionCircleFunctionDescription", "This function returns the occlusion factor of a sprite. It samples the depth buffer in concentric rings around the given world position and compares each sample with the camera distance.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Center World Position")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Window Diameter World")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Samples per ring")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Number of sample rings")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Visibility Fraction")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Fraction")));
	OutFunctions.Add(Sig);
}

bool UNiagaraDataInterfaceOcclusion::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> ArgsSample;
	ArgsSample.Add(TEXT("FunctionName"), FunctionInfo.InstanceName);

	if (FunctionInfo.DefinitionName == GetCameraOcclusionRectangleName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(in float3 In_SampleCenterWorldPos, in float In_SampleWindowWidthWorld, in float In_SampleWindowHeightWorld, in float In_SampleSteps, out float Out_VisibilityFraction, out float Out_SampleFraction)
			{
				float CameraDistance = length(In_SampleCenterWorldPos.xyz - View.WorldViewOrigin.xyz);
				float4 SamplePosition = float4(In_SampleCenterWorldPos + View.PreViewTranslation, 1);
				float4 ClipPosition = mul(SamplePosition, View.TranslatedWorldToClip);
				float2 ScreenPosition = ClipPosition.xy / ClipPosition.w;
				float2 ScreenUV = ScreenPosition * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;

				float Steps = In_SampleSteps <= 1 ? 0 : In_SampleSteps;
				float TotalSamples = 0;
				float OccludedSamples = 0;

				float4 SampleWidthClip = mul(float4(View.ViewRight * In_SampleWindowWidthWorld, 0) + SamplePosition, View.TranslatedWorldToClip);
				float4 SampleHeightClip = mul(float4(View.ViewUp * In_SampleWindowHeightWorld, 0) + SamplePosition, View.TranslatedWorldToClip);
				
				float2 SampleWidthUV = SampleWidthClip.xy / SampleWidthClip.w * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;
				float2 SampleHeightUV = SampleHeightClip.xy / SampleHeightClip.w * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;
				
				float SampleWidth = ScreenUV.x > 1 ? 0 : SampleWidthUV.x - ScreenUV.x;
				float SampleHeight = ScreenUV.y > 1 ? 0 : SampleHeightUV.y - ScreenUV.y;

				if (Steps > 0) 
				{
					for (int ys = 0; ys < Steps; ys++)
					{
						float SampleY = ScreenUV.y - 0.5 * SampleHeight + ys * SampleHeight / (Steps - 1);
						if (SampleY > 1 || SampleY < 0)
						{
							continue;
						}
						for (int xs = 0; xs < Steps; xs++)
						{
							float SampleX = ScreenUV.x - 0.5 * SampleWidth + xs * SampleWidth / (Steps - 1);
							if (SampleX > 1 || SampleX < 0)
							{
								continue;
							}
			
							float Depth = CalcSceneDepth(float2(SampleX, SampleY));
							if (Depth < CameraDistance) 
							{
								OccludedSamples++;
							}
							TotalSamples++;
						} 
					}
				}
				Out_VisibilityFraction = TotalSamples > 0 ? 1 - OccludedSamples / TotalSamples : 0;
				Out_SampleFraction = Steps == 0 ? 0 : (TotalSamples / (Steps * Steps));
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (FunctionInfo.DefinitionName == GetCameraOcclusionCircleName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(in float3 In_SampleCenterWorldPos, in float In_SampleWindowDiameterWorld, in float In_SampleRays, in float In_SampleStepsPerRay, out float Out_VisibilityFraction, out float Out_SampleFraction)
			{
				const float PI = 3.14159265;
				const float SPIRAL_TURN = 2 * PI * 0.61803399; // use golden ratio to rotate sample pattern each ring so we get a spiral
				float CameraDistance = length(In_SampleCenterWorldPos.xyz - View.WorldViewOrigin.xyz);
				float4 SamplePosition = float4(In_SampleCenterWorldPos + View.PreViewTranslation, 1);
				float4 ClipPosition = mul(SamplePosition, View.TranslatedWorldToClip);
				float2 ScreenPosition = ClipPosition.xy / ClipPosition.w;
				float2 ScreenUV = ScreenPosition * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;

				float Rays = In_SampleRays <= 1 ? 0 : In_SampleRays;
				float Steps = In_SampleStepsPerRay < 1 ? 0 : In_SampleStepsPerRay;
				float TotalSamples = 0;
				float OccludedSamples = 0;

				if (ScreenUV.x <= 1 && ScreenUV.x >= 0 && ScreenUV.y <= 1 && ScreenUV.y >= 0)
				{
					float Depth = CalcSceneDepth(ScreenUV);
					if (Depth < CameraDistance) 
					{
						OccludedSamples++;
					}
					TotalSamples++;
				}
				if (Steps > 0) 
				{
					float Degrees = 0;
					for (int Step = 1; Step <= Steps; Step++)
					{
						float LerpFactor = Step / Steps;
						Degrees += SPIRAL_TURN;
						for (int ray = 0; ray < Rays; ray++)
						{
							// calc ray direction vector
							float3 RayDirection = cos(Degrees) * View.ViewUp + sin(Degrees) * View.ViewRight;
							float4 RayClip = mul(float4(RayDirection * In_SampleWindowDiameterWorld / 2, 0) + SamplePosition, View.TranslatedWorldToClip);
							float2 RayUV = RayClip.xy / RayClip.w * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;

							if ((ScreenUV.x > 1 && RayUV.x < 0) || (ScreenUV.y > 1 && RayUV.y < 0) || (ScreenUV.x < 0 && RayUV.x > 1) || (ScreenUV.y < 0 && RayUV.y > 1))
							{
								continue;
							}
						
							float2 SampleUV = lerp(ScreenUV, RayUV, float2(LerpFactor, LerpFactor));
							
							if (SampleUV.x > 1 || SampleUV.x < 0 || SampleUV.y > 1 || SampleUV.y < 0)
							{
								continue;
							}
			
							float Depth = CalcSceneDepth(SampleUV);
							if (Depth < CameraDistance) 
							{
								OccludedSamples++;
							}
							TotalSamples++;
							Degrees += 2 * PI / Rays;
						}						
					}
				}
				Out_VisibilityFraction = TotalSamples > 0 ? 1 - OccludedSamples / TotalSamples : 0;
				Out_SampleFraction = Steps == 0 ? 0 : (TotalSamples / (Rays * Steps + 1));
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	return false;
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceOcclusion, QueryOcclusionFactorGPU);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceOcclusion, QueryOcclusionFactorCircleGPU);
void UNiagaraDataInterfaceOcclusion::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == GetCameraOcclusionRectangleName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceOcclusion, QueryOcclusionFactorGPU)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetCameraOcclusionCircleName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceOcclusion, QueryOcclusionFactorCircleGPU)::Bind(this, OutFunc);
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Could not find data interface external function. Received Name: %s"), *BindingInfo.Name.ToString());
	}
}

// ------- Dummy implementations for CPU execution ------------

void UNiagaraDataInterfaceOcclusion::QueryOcclusionFactorGPU(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<float> PosParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> PosParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> PosParamZ(Context);
	VectorVM::FExternalFuncInputHandler<float> WidthParam(Context);
	VectorVM::FExternalFuncInputHandler<float> HeightParam(Context);
	VectorVM::FExternalFuncInputHandler<float> StepsParam(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutOcclusion(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutFactor(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		PosParamX.GetAndAdvance();
		PosParamY.GetAndAdvance();
		PosParamZ.GetAndAdvance();
		WidthParam.GetAndAdvance();
		HeightParam.GetAndAdvance();
		StepsParam.GetAndAdvance();

		*OutOcclusion.GetDestAndAdvance() = 0;
		*OutFactor.GetDestAndAdvance() = 0;
	}
}

void UNiagaraDataInterfaceOcclusion::QueryOcclusionFactorCircleGPU(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<float> PosParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> PosParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> PosParamZ(Context);
	VectorVM::FExternalFuncInputHandler<float> RadiusParam(Context);
	VectorVM::FExternalFuncInputHandler<float> RaysParam(Context);
	VectorVM::FExternalFuncInputHandler<float> StepsParam(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutOcclusion(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutFactor(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		PosParamX.GetAndAdvance();
		PosParamY.GetAndAdvance();
		PosParamZ.GetAndAdvance();
		RadiusParam.GetAndAdvance();
		RaysParam.GetAndAdvance();
		StepsParam.GetAndAdvance();

		*OutOcclusion.GetDestAndAdvance() = 0;
		*OutFactor.GetDestAndAdvance() = 0;
	}
}

// ------------------------------------------------------------

struct FNiagaraDataInterfaceParametersCS_OcclusionQuery : public FNiagaraDataInterfaceParametersCS
{
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{
		PassUniformBuffer.Bind(ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Ar << PassUniformBuffer;
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());
		FRHIComputeShader* ComputeShaderRHI = Context.Shader->GetComputeShader();

		TUniformBufferRef<FSceneTexturesUniformParameters> SceneTextureUniformParams = GNiagaraViewDataManager.GetSceneTextureUniformParameters();
		SetUniformBufferParameter(RHICmdList, ComputeShaderRHI, PassUniformBuffer, SceneTextureUniformParams);
	}

private:

	/** The SceneDepthTexture parameter for depth buffer query. */
	FShaderUniformBufferParameter PassUniformBuffer;
};

FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceOcclusion::ConstructComputeParameters() const
{
	return new FNiagaraDataInterfaceParametersCS_OcclusionQuery();
}
