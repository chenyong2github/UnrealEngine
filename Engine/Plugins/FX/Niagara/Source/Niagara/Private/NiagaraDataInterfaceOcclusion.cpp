// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceOcclusion.h"
#include "NiagaraTypes.h"
#include "NiagaraWorldManager.h"
#include "ShaderParameterUtils.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraSystemInstance.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceOcclusion"

const FName UNiagaraDataInterfaceOcclusion::GetCameraOcclusionRectangleName(TEXT("QueryOcclusionFactorWithRectangleGPU"));
const FName UNiagaraDataInterfaceOcclusion::GetCameraOcclusionCircleName(TEXT("QueryOcclusionFactorWithCircleGPU"));

UNiagaraDataInterfaceOcclusion::UNiagaraDataInterfaceOcclusion(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataIntefaceProxyOcclusionQuery());
}

void UNiagaraDataInterfaceOcclusion::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

void UNiagaraDataInterfaceOcclusion::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature Sig;
	Sig.Name = GetCameraOcclusionRectangleName;
#if WITH_EDITORONLY_DATA
	Sig.Description = LOCTEXT("GetCameraOcclusionRectFunctionDescription", "This function returns the occlusion factor of a sprite. It samples the depth buffer in a rectangular grid around the given world position and compares each sample with the camera distance.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	FText VisibilityFractionDescription = LOCTEXT("VisibilityFractionDescription", "Returns a value 0..1 depending on how many of the samples on the screen were occluded.\nFor example, a value of 0.3 means that 70% of visible samples were occluded.\nIf the sample fraction is 0 then this also returns 0.");
	FText SampleFractionDescription = LOCTEXT("SampleFractionDescription", "Returns a value 0..1 depending on how many samples were inside the viewport or outside of it.\nFor example, a value of 0.3 means that 70% of samples were outside the current viewport and therefore not visible.");
	Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Occlusion interface")));
	Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Center World Position")), LOCTEXT("RectCenterPosDescription", "This world space position where the center of the sample rectangle should be."));
	Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Window Width World")), LOCTEXT("SampleWindowWidthWorldDescription", "The total width of the sample rectangle in world space.\nIf the particle is a camera-aligned sprite then this is the sprite width."));
	Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Window Height World")), LOCTEXT("SampleWindowHeightWorldDescription", "The total height of the sample rectangle in world space.\nIf the particle is a camera-aligned sprite then this is the sprite height."));
	Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Steps Per Line")), LOCTEXT("StepsPerLineDescription", "The number of samples to take horizontally. The total number of samples is this value squared."));
	Sig.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Visibility Fraction")), VisibilityFractionDescription);
	Sig.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Fraction")), SampleFractionDescription);
	OutFunctions.Add(Sig);

	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetCameraOcclusionCircleName;
#if WITH_EDITORONLY_DATA
	Sig.Description = LOCTEXT("GetCameraOcclusionCircleFunctionDescription", "This function returns the occlusion factor of a sprite. It samples the depth buffer in concentric rings around the given world position and compares each sample with the camera distance.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Occlusion interface")));
	Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Center World Position")), LOCTEXT("CircleCenterPosDescription", "This world space position where the center of the sample circle should be."));
	Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Window Diameter World")), LOCTEXT("SampleWindowDiameterDescription", "The world space diameter of the circle to sample.\nIf the particle is a spherical sprite then this is the sprite size."));
	Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Samples per ring")),LOCTEXT("SamplesPerRingDescription", "The number of samples for each ring inside the circle.\nThe total number of samples is NumRings * SamplesPerRing."));
	Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Number of sample rings")), LOCTEXT("NumberOfSampleRingsDescription", "This number of concentric rings to sample inside the circle.\nThe total number of samples is NumRings * SamplesPerRing."));
	Sig.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Visibility Fraction")), VisibilityFractionDescription);
	Sig.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Fraction")), SampleFractionDescription);
	OutFunctions.Add(Sig);
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceOcclusion::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!Super::AppendCompileHash(InVisitor))
		return false;

	FSHAHash Hash = GetShaderFileHash((TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceOcclusion.ush")), EShaderPlatform::SP_PCD3D_SM5);
	InVisitor->UpdateString(TEXT("NiagaraDataInterfaceOcclusionHLSLSource"), Hash.ToString());
	return true;
}

void UNiagaraDataInterfaceOcclusion::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Plugin/FX/Niagara/Private/NiagaraDataInterfaceOcclusion.ush\"\n");
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
				DIOcclusion_Rectangle(In_SampleCenterWorldPos, In_SampleWindowWidthWorld, In_SampleWindowHeightWorld, In_SampleSteps, Out_VisibilityFraction, Out_SampleFraction);
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
				DIOcclusion_Circle(In_SampleCenterWorldPos, In_SampleWindowDiameterWorld, In_SampleRays, In_SampleStepsPerRay, Out_VisibilityFraction, Out_SampleFraction);
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	return false;
}
#endif

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

#undef LOCTEXT_NAMESPACE
