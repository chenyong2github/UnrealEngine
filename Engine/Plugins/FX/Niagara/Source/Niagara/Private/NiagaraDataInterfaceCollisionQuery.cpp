// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceCollisionQuery.h"
#include "NiagaraTypes.h"
#include "NiagaraWorldManager.h"
#include "ShaderParameterUtils.h"
#include "GlobalDistanceFieldParameters.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "Shader.h"

FCriticalSection UNiagaraDataInterfaceCollisionQuery::CriticalSection;

struct FNiagaraCollisionDIFunctionVersion
{
	enum Type
	{
		InitialVersion = 0,
		AddedTraceSkip = 1,
		AddedCustomDepthCollision = 2,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
};

const FName UNiagaraDataInterfaceCollisionQuery::SceneDepthName(TEXT("QuerySceneDepthGPU"));
const FName UNiagaraDataInterfaceCollisionQuery::CustomDepthName(TEXT("QueryCustomDepthGPU"));
const FName UNiagaraDataInterfaceCollisionQuery::DistanceFieldName(TEXT("QueryMeshDistanceFieldGPU"));
const FName UNiagaraDataInterfaceCollisionQuery::SyncTraceName(TEXT("PerformCollisionQuerySyncCPU"));
const FName UNiagaraDataInterfaceCollisionQuery::AsyncTraceName(TEXT("PerformCollisionQueryAsyncCPU"));

UNiagaraDataInterfaceCollisionQuery::UNiagaraDataInterfaceCollisionQuery(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TraceChannelEnum = StaticEnum<ECollisionChannel>();
	SystemInstance = nullptr;

    Proxy.Reset(new FNiagaraDataIntefaceProxyCollisionQuery());
}

bool UNiagaraDataInterfaceCollisionQuery::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance)
{
	CQDIPerInstanceData *PIData = new (PerInstanceData) CQDIPerInstanceData;
	PIData->SystemInstance = InSystemInstance;
	if (InSystemInstance)
	{
		PIData->CollisionBatch.Init(InSystemInstance->GetId(), InSystemInstance->GetWorld());
	}
	return true;
}

void UNiagaraDataInterfaceCollisionQuery::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance)
{
	CQDIPerInstanceData* InstData = (CQDIPerInstanceData*)PerInstanceData;
	InstData->~CQDIPerInstanceData();
}

void UNiagaraDataInterfaceCollisionQuery::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(TraceChannelEnum), true, false, false);
	}
}

void UNiagaraDataInterfaceCollisionQuery::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature SigDepth;
	SigDepth.Name = UNiagaraDataInterfaceCollisionQuery::SceneDepthName;
	SigDepth.bMemberFunction = true;
	SigDepth.bRequiresContext = false;
	SigDepth.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
	SigDepth.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
#endif
	SigDepth.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
	SigDepth.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("DepthSamplePosWorld")));
	SigDepth.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SceneDepth")));
	SigDepth.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CameraPosWorld")));
	SigDepth.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsInsideView")));
	SigDepth.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SamplePosWorld")));
	SigDepth.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SampleWorldNormal")));
	OutFunctions.Add(SigDepth);

	FNiagaraFunctionSignature SigCustomDepth;
	SigCustomDepth.Name = UNiagaraDataInterfaceCollisionQuery::CustomDepthName;
	SigCustomDepth.bMemberFunction = true;
	SigCustomDepth.bRequiresContext = false;
	SigCustomDepth.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
	SigCustomDepth.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
#endif
	SigCustomDepth.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
	SigCustomDepth.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("DepthSamplePosWorld")));
	SigCustomDepth.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SceneDepth")));
	SigCustomDepth.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CameraPosWorld")));
	SigCustomDepth.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsInsideView")));
	SigCustomDepth.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SamplePosWorld")));
	SigCustomDepth.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SampleWorldNormal")));
	OutFunctions.Add(SigCustomDepth);

	FNiagaraFunctionSignature SigMeshField;
	SigMeshField.Name = UNiagaraDataInterfaceCollisionQuery::DistanceFieldName;
	SigMeshField.bMemberFunction = true;
	SigMeshField.bRequiresContext = false;
	SigMeshField.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
	SigMeshField.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
#endif
	SigMeshField.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
	SigMeshField.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("FieldSamplePosWorld")));
	SigMeshField.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("DistanceToNearestSurface")));
	SigMeshField.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("FieldGradient")));
	SigMeshField.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsDistanceFieldValid")));
    OutFunctions.Add(SigMeshField);

	FNiagaraFunctionSignature SigCpuSync;
	SigCpuSync.Name = UNiagaraDataInterfaceCollisionQuery::SyncTraceName;
	SigCpuSync.bMemberFunction = true;
	SigCpuSync.bRequiresContext = false;
	SigCpuSync.bSupportsGPU = false;
#if WITH_EDITORONLY_DATA
	SigCpuSync.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
#endif
	SigCpuSync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
	SigCpuSync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TraceStartWorld")));
	SigCpuSync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TraceEndWorld")));
	SigCpuSync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TraceChannelEnum), TEXT("TraceChannel")));
	SigCpuSync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SkipTrace")));
	SigCpuSync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("CollisionValid")));
	SigCpuSync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsTraceInsideMesh")));
	SigCpuSync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionPosWorld")));
	SigCpuSync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionNormal")));
	SigCpuSync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialFriction")));
	SigCpuSync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialRestitution")));
	OutFunctions.Add(SigCpuSync);

	FNiagaraFunctionSignature SigCpuAsync;
	SigCpuAsync.Name = UNiagaraDataInterfaceCollisionQuery::AsyncTraceName;
	SigCpuAsync.bMemberFunction = true;
	SigCpuAsync.bRequiresContext = false;
	SigCpuAsync.bSupportsGPU = false;
#if WITH_EDITORONLY_DATA
	SigCpuAsync.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
#endif
	SigCpuAsync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
	SigCpuAsync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("PreviousFrameQueryID")));
	SigCpuAsync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TraceStartWorld")));
	SigCpuAsync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TraceEndWorld")));
	SigCpuAsync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(TraceChannelEnum), TEXT("TraceChannel")));
	SigCpuAsync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SkipTrace")));
	SigCpuAsync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NextFrameQueryID")));
	SigCpuAsync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("CollisionValid")));
	SigCpuAsync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsTraceInsideMesh")));
	SigCpuAsync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionPosWorld")));
	SigCpuAsync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionNormal")));
	SigCpuAsync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialFriction")));
	SigCpuAsync.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialRestitution")));
	OutFunctions.Add(SigCpuAsync);
}

// build the shader function HLSL; function name is passed in, as it's defined per-DI; that way, configuration could change
// the HLSL in the spirit of a static switch
// TODO: need a way to identify each specific function here

// 
bool UNiagaraDataInterfaceCollisionQuery::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{

	if (FunctionInfo.DefinitionName == SceneDepthName || FunctionInfo.DefinitionName == CustomDepthName)
	{
		static const FString SceneDepthSampleExpr = TEXT("CalcSceneDepth(ScreenUV)");
		static const FString CustomDepthSampleExpr = TEXT("ConvertFromDeviceZ(Texture2DSampleLevel(SceneTexturesStruct.CustomDepthTexture, SceneTexturesStruct_SceneDepthTextureSampler, ScreenUV, 0).r)");
		const FStringFormatOrderedArguments Args = {
			FunctionInfo.InstanceName,
			FunctionInfo.DefinitionName == SceneDepthName ? SceneDepthSampleExpr : CustomDepthSampleExpr
		};

		OutHLSL += FString::Format(TEXT(R"(
			void {0}(in float3 In_SamplePos, out float Out_SceneDepth, out float3 Out_CameraPosWorld, out bool Out_IsInsideView, out float3 Out_WorldPos, out float3 Out_WorldNormal)
			{				
				Out_SceneDepth = -1;
				Out_WorldPos = float3(0.0, 0.0, 0.0);
				Out_WorldNormal = float3(0.0, 0.0, 1.0);
				Out_IsInsideView = true;
				Out_CameraPosWorld.xyz = View.WorldCameraOrigin.xyz;

			#if FEATURE_LEVEL >= FEATURE_LEVEL_SM5
				float4 SamplePosition = float4(In_SamplePos + View.PreViewTranslation, 1);
				float4 ClipPosition = mul(SamplePosition, View.TranslatedWorldToClip);
				float2 ScreenPosition = ClipPosition.xy / ClipPosition.w;
				// Check if the sample is inside the view.
				if (all(abs(ScreenPosition.xy) <= float2(1, 1)))
				{
					// Sample the depth buffer to get a world position near the sample position.
					float2 ScreenUV = ScreenPosition * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;
					float SceneDepth = {1};
					Out_SceneDepth = SceneDepth;
					// Reconstruct world position.
					Out_WorldPos = WorldPositionFromSceneDepth(ScreenPosition.xy, SceneDepth);
					// Sample the normal buffer
					Out_WorldNormal = Texture2DSampleLevel(SceneTexturesStruct.GBufferATexture, SceneTexturesStruct_GBufferATextureSampler, ScreenUV, 0).xyz * 2.0 - 1.0;
				}
				else
				{
					Out_IsInsideView = false;
				}
			#endif
			}
		)"), Args);
		return true;
	}
	else if (FunctionInfo.DefinitionName == DistanceFieldName)
	{
		OutHLSL += TEXT("void ") + FunctionInfo.InstanceName + TEXT("(in float3 In_SamplePos, out float Out_DistanceToNearestSurface, out float3 Out_FieldGradient, out bool Out_IsDistanceFieldValid) \n{\n");
		OutHLSL += TEXT("\
			#if PLATFORM_SUPPORTS_DISTANCE_FIELDS\n\
			Out_DistanceToNearestSurface = GetDistanceToNearestSurfaceGlobal(In_SamplePos);\n\
			Out_FieldGradient = GetDistanceFieldGradientGlobal(In_SamplePos);\n\
			Out_IsDistanceFieldValid = MaxGlobalDistance > 0;\n\
			#else\n\
			Out_DistanceToNearestSurface = 0;\n\
			Out_FieldGradient = (float3)0;\n\
			Out_IsDistanceFieldValid = false;\n\
			#endif\n\
			}\n\n");
		return true;
	}

	return false;
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceCollisionQuery::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	bool bWasChanged = false;

	// The distance field query got a new output at some point, but there exists no custom version for it
	if (FunctionSignature.Name == UNiagaraDataInterfaceCollisionQuery::DistanceFieldName && FunctionSignature.Outputs.Num() == 2)
	{
		FunctionSignature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsDistanceFieldValid")));
		bWasChanged = true;
	}

	// Early out for version matching
	if (FunctionSignature.FunctionVersion == FNiagaraCollisionDIFunctionVersion::LatestVersion)
	{
		return bWasChanged;
	}

	// Added the possibility to skip a line trace to increase performance when only a fraction of particles wants to do a line trace
	if (FunctionSignature.FunctionVersion < FNiagaraCollisionDIFunctionVersion::AddedTraceSkip)
	{
		if (FunctionSignature.Name == UNiagaraDataInterfaceCollisionQuery::SyncTraceName || FunctionSignature.Name == UNiagaraDataInterfaceCollisionQuery::AsyncTraceName)
		{
			FunctionSignature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SkipTrace")));
			bWasChanged = true;
		}
	}

	// Set latest version
	FunctionSignature.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;

	return bWasChanged;
}
#endif

bool IsDistanceFieldEnabled()
{
	static const auto* CVarGenerateMeshDistanceFields = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));
	return CVarGenerateMeshDistanceFields != nullptr && CVarGenerateMeshDistanceFields->GetValueOnAnyThread() > 0;
}

#if WITH_EDITOR
void UNiagaraDataInterfaceCollisionQuery::ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors)
{
	if (Function.Name == DistanceFieldName)
	{
		if (!IsDistanceFieldEnabled())
		{
			OutValidationErrors.Add(NSLOCTEXT("NiagaraDataInterfaceCollisionQuery", "NiagaraDistanceFieldNotEnabledMsg", "The mesh distance field generation is currently not enabled, please check the project settings.\nNiagara cannot query the distance field otherwise."));
		}
	}
}
#endif

void UNiagaraDataInterfaceCollisionQuery::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	// we don't need to add these to hlsl, as they're already in common.ush
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, PerformQuerySyncCPU);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, PerformQueryAsyncCPU);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, QuerySceneDepth);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, QueryMeshDistanceField);

void UNiagaraDataInterfaceCollisionQuery::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == UNiagaraDataInterfaceCollisionQuery::SyncTraceName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, PerformQuerySyncCPU)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UNiagaraDataInterfaceCollisionQuery::AsyncTraceName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, PerformQueryAsyncCPU)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UNiagaraDataInterfaceCollisionQuery::SceneDepthName ||
			 BindingInfo.Name == UNiagaraDataInterfaceCollisionQuery::CustomDepthName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, QuerySceneDepth)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UNiagaraDataInterfaceCollisionQuery::DistanceFieldName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, QueryMeshDistanceField)::Bind(this, OutFunc);
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Could not find data interface external function. %s\n"),
			*BindingInfo.Name.ToString());
	}
}

bool UNiagaraDataInterfaceCollisionQuery::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!Super::AppendCompileHash(InVisitor))
	{
		return false;
	}
	bool bDistanceFieldEnabled = IsDistanceFieldEnabled();
	InVisitor->UpdatePOD(TEXT("NiagaraCollisionDI_DistanceField"), bDistanceFieldEnabled);
	return true;
}

void UNiagaraDataInterfaceCollisionQuery::PerformQuerySyncCPU(FVectorVMContext & Context)
{
	VectorVM::FUserPtrHandler<CQDIPerInstanceData> InstanceData(Context);

	VectorVM::FExternalFuncInputHandler<float> StartPosParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> StartPosParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> StartPosParamZ(Context);

	VectorVM::FExternalFuncInputHandler<float> EndPosParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> EndPosParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> EndPosParamZ(Context);

	VectorVM::FExternalFuncInputHandler<ECollisionChannel> TraceChannelParam(Context);

	VectorVM::FExternalFuncInputHandler<FNiagaraBool> IsSkipTrace(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutQueryValid(Context);
	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutInsideMesh(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionPosZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionNormX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionNormY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionNormZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutFriction(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutRestitution(Context);

	FScopeLock ScopeLock(&CriticalSection);
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		FVector Pos(StartPosParamX.GetAndAdvance(), StartPosParamY.GetAndAdvance(), StartPosParamZ.GetAndAdvance());
		FVector Dir(EndPosParamX.GetAndAdvance(), EndPosParamY.GetAndAdvance(), EndPosParamZ.GetAndAdvance());
		ECollisionChannel TraceChannel = TraceChannelParam.GetAndAdvance();
		bool Skip = IsSkipTrace.GetAndAdvance().GetValue();
		ensure(!Pos.ContainsNaN());
		FNiagaraDICollsionQueryResult Res;

		if (!Skip && InstanceData->CollisionBatch.PerformQuery(Pos, Dir, Res, TraceChannel))
		{
			*OutQueryValid.GetDestAndAdvance() = FNiagaraBool(true);
			*OutInsideMesh.GetDestAndAdvance() = FNiagaraBool(Res.IsInsideMesh);
			*OutCollisionPosX.GetDestAndAdvance() = Res.CollisionPos.X;
			*OutCollisionPosY.GetDestAndAdvance() = Res.CollisionPos.Y;
			*OutCollisionPosZ.GetDestAndAdvance() = Res.CollisionPos.Z;
			*OutCollisionNormX.GetDestAndAdvance() = Res.CollisionNormal.X;
			*OutCollisionNormY.GetDestAndAdvance() = Res.CollisionNormal.Y;
			*OutCollisionNormZ.GetDestAndAdvance() = Res.CollisionNormal.Z;
			*OutFriction.GetDestAndAdvance() = Res.Friction;
			*OutRestitution.GetDestAndAdvance() = Res.Restitution;
		}
		else
		{
			*OutQueryValid.GetDestAndAdvance() = FNiagaraBool();
			*OutInsideMesh.GetDestAndAdvance() = FNiagaraBool();
			*OutCollisionPosX.GetDestAndAdvance() = 0.0f;
			*OutCollisionPosY.GetDestAndAdvance() = 0.0f;
			*OutCollisionPosZ.GetDestAndAdvance() = 0.0f;
			*OutCollisionNormX.GetDestAndAdvance() = 0.0f;
			*OutCollisionNormY.GetDestAndAdvance() = 0.0f;
			*OutCollisionNormZ.GetDestAndAdvance() = 0.0f;
			*OutFriction.GetDestAndAdvance() = 0.0f;
			*OutRestitution.GetDestAndAdvance() = 0.0f;
		}
	}
}

void UNiagaraDataInterfaceCollisionQuery::PerformQueryAsyncCPU(FVectorVMContext & Context)
{
	VectorVM::FUserPtrHandler<CQDIPerInstanceData> InstanceData(Context);

	VectorVM::FExternalFuncInputHandler<int32> InIDParam(Context);
	VectorVM::FExternalFuncInputHandler<float> StartPosParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> StartPosParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> StartPosParamZ(Context);

	VectorVM::FExternalFuncInputHandler<float> EndPosParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> EndPosParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> EndPosParamZ(Context);

	VectorVM::FExternalFuncInputHandler<ECollisionChannel> TraceChannelParam(Context);

	VectorVM::FExternalFuncInputHandler<FNiagaraBool> IsSkipTrace(Context);

	VectorVM::FExternalFuncRegisterHandler<int32> OutQueryID(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutQueryValid(Context);
	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutInsideMesh(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionPosZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionNormX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionNormY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionNormZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutFriction(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutRestitution(Context);

	FScopeLock ScopeLock(&CriticalSection);
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		FVector Pos(StartPosParamX.GetAndAdvance(), StartPosParamY.GetAndAdvance(), StartPosParamZ.GetAndAdvance());
		FVector End(EndPosParamX.GetAndAdvance(), EndPosParamY.GetAndAdvance(), EndPosParamZ.GetAndAdvance());
		ECollisionChannel TraceChannel = TraceChannelParam.GetAndAdvance();
		bool Skip = IsSkipTrace.GetAndAdvance().GetValue();
		ensure(!Pos.ContainsNaN());

		*OutQueryID.GetDestAndAdvance() = Skip ? INDEX_NONE : InstanceData->CollisionBatch.SubmitQuery(Pos, End, TraceChannel);

		// try to retrieve a query with the supplied query ID
		FNiagaraDICollsionQueryResult Res;
		int32 ID = InIDParam.GetAndAdvance();
		if (ID != INDEX_NONE && InstanceData->CollisionBatch.GetQueryResult(ID, Res))
		{
			*OutQueryValid.GetDestAndAdvance() = FNiagaraBool(true);
			*OutInsideMesh.GetDestAndAdvance() = FNiagaraBool(Res.IsInsideMesh);
			*OutCollisionPosX.GetDestAndAdvance() = Res.CollisionPos.X;
			*OutCollisionPosY.GetDestAndAdvance() = Res.CollisionPos.Y;
			*OutCollisionPosZ.GetDestAndAdvance() = Res.CollisionPos.Z;
			*OutCollisionNormX.GetDestAndAdvance() = Res.CollisionNormal.X;
			*OutCollisionNormY.GetDestAndAdvance() = Res.CollisionNormal.Y;
			*OutCollisionNormZ.GetDestAndAdvance() = Res.CollisionNormal.Z;
			*OutFriction.GetDestAndAdvance() = Res.Friction;
			*OutRestitution.GetDestAndAdvance() = Res.Restitution;
		}
		else
		{
			*OutQueryValid.GetDestAndAdvance() = FNiagaraBool();
			*OutInsideMesh.GetDestAndAdvance() = FNiagaraBool();
			*OutCollisionPosX.GetDestAndAdvance() = 0.0f;
			*OutCollisionPosY.GetDestAndAdvance() = 0.0f;
			*OutCollisionPosZ.GetDestAndAdvance() = 0.0f;
			*OutCollisionNormX.GetDestAndAdvance() = 0.0f;
			*OutCollisionNormY.GetDestAndAdvance() = 0.0f;
			*OutCollisionNormZ.GetDestAndAdvance() = 0.0f;
			*OutFriction.GetDestAndAdvance() = 0.0f;
			*OutRestitution.GetDestAndAdvance() = 0.0f;
		}
	}
}

void UNiagaraDataInterfaceCollisionQuery::QuerySceneDepth(FVectorVMContext & Context)
{
	UE_LOG(LogNiagara, Error, TEXT("GPU only function 'QuerySceneDepthGPU' called on CPU VM, check your module code to fix."));

	VectorVM::FUserPtrHandler<CQDIPerInstanceData> InstanceData(Context);

	VectorVM::FExternalFuncInputHandler<float> SamplePosParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> SamplePosParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> SamplePosParamZ(Context);
	
	VectorVM::FExternalFuncRegisterHandler<float> OutSceneDepth(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCameraPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCameraPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCameraPosZ(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutIsInsideView(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldPosZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldNormX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldNormY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldNormZ(Context);

	FScopeLock ScopeLock(&CriticalSection);
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutSceneDepth.GetDestAndAdvance() = -1;
		*OutIsInsideView.GetDestAndAdvance() = 0;
		*OutWorldPosX.GetDestAndAdvance() = 0.0f;
		*OutWorldPosY.GetDestAndAdvance() = 0.0f;
		*OutWorldPosZ.GetDestAndAdvance() = 0.0f;
		*OutWorldNormX.GetDestAndAdvance() = 0.0f;
		*OutWorldNormY.GetDestAndAdvance() = 0.0f;
		*OutWorldNormZ.GetDestAndAdvance() = 1.0f;
		*OutCameraPosX.GetDestAndAdvance() = 0.0f;
		*OutCameraPosY.GetDestAndAdvance() = 0.0f;
		*OutCameraPosZ.GetDestAndAdvance() = 0.0f;
	}
}

void UNiagaraDataInterfaceCollisionQuery::QueryMeshDistanceField(FVectorVMContext& Context)
{
	UE_LOG(LogNiagara, Error, TEXT("GPU only function 'QueryMeshDistanceFieldGPU' called on CPU VM, check your module code to fix."));

	VectorVM::FUserPtrHandler<CQDIPerInstanceData> InstanceData(Context);

	VectorVM::FExternalFuncInputHandler<float> SamplePosParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> SamplePosParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> SamplePosParamZ(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutSurfaceDistance(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutFieldGradientX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutFieldGradientY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutFieldGradientZ(Context);
	FNDIOutputParam<FNiagaraBool> OutIsFieldValid(Context);

	FScopeLock ScopeLock(&CriticalSection);
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutSurfaceDistance.GetDestAndAdvance() = -1;
		*OutFieldGradientX.GetDestAndAdvance() = 0.0f;
		*OutFieldGradientY.GetDestAndAdvance() = 0.0f;
		*OutFieldGradientZ.GetDestAndAdvance() = 1.0f;
		OutIsFieldValid.SetAndAdvance(FNiagaraBool());
	}
}

bool UNiagaraDataInterfaceCollisionQuery::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance, float DeltaSeconds)
{
	CQDIPerInstanceData* PIData = static_cast<CQDIPerInstanceData*>(PerInstanceData);
	PIData->CollisionBatch.CollectResults();

	return false;
}

bool UNiagaraDataInterfaceCollisionQuery::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance, float DeltaSeconds)
{
	CQDIPerInstanceData* PIData = static_cast<CQDIPerInstanceData*>(PerInstanceData);
	PIData->CollisionBatch.DispatchQueries();
	PIData->CollisionBatch.ClearWrite();
	return false;
}

//////////////////////////////////////////////////////////////////////////

struct FNiagaraDataInterfaceParametersCS_CollisionQuery : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_CollisionQuery, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		PassUniformBuffer.Bind(ParameterMap, FSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName());
		GlobalDistanceFieldParameters.Bind(ParameterMap);
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		
		TUniformBufferRef<FSceneTextureUniformParameters> SceneTextureUniformParams = GNiagaraViewDataManager.GetSceneTextureUniformParameters();
		SetUniformBufferParameter(RHICmdList, ComputeShaderRHI, PassUniformBuffer/*Shader->GetUniformBufferParameter(SceneTexturesUniformBufferStruct)*/, SceneTextureUniformParams);
		if (GlobalDistanceFieldParameters.IsBound() && Context.Batcher)
		{
			GlobalDistanceFieldParameters.Set(RHICmdList, ComputeShaderRHI, Context.Batcher->GetGlobalDistanceFieldParameters());
		}		
	}

private:
	/** The SceneDepthTexture parameter for depth buffer collision. */
	LAYOUT_FIELD(FShaderUniformBufferParameter, PassUniformBuffer);

	LAYOUT_FIELD(FGlobalDistanceFieldParameters, GlobalDistanceFieldParameters);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_CollisionQuery);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceCollisionQuery, FNiagaraDataInterfaceParametersCS_CollisionQuery);
