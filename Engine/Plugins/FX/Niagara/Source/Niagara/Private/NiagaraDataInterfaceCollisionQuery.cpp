// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceCollisionQuery.h"
#include "NiagaraTypes.h"
#include "NiagaraWorldManager.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterInstanceBatcher.h"

#include "GlobalDistanceFieldParameters.h"
#include "ShaderParameterUtils.h"
#include "ShaderCompilerCore.h"
#include "Shader.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceCollisionQuery"

namespace NDICollisionQueryLocal
{
	static const TCHAR* CommonShaderFile = TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceCollisionQuery.ush");
	static const TCHAR* TemplateShaderFile = TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceCollisionQueryTemplate.ush");

	static const FName SceneDepthName(TEXT("QuerySceneDepthGPU"));
	static const FName CustomDepthName(TEXT("QueryCustomDepthGPU"));
	static const FName DistanceFieldName(TEXT("QueryMeshDistanceFieldGPU"));
	static const FName SyncTraceName(TEXT("PerformCollisionQuerySyncCPU"));
	static const FName AsyncTraceName(TEXT("PerformCollisionQueryAsyncCPU"));
}

FCriticalSection UNiagaraDataInterfaceCollisionQuery::CriticalSection;

struct FNiagaraCollisionDIFunctionVersion
{
	enum Type
	{
		InitialVersion = 0,
		AddedTraceSkip = 1,
		AddedCustomDepthCollision = 2,
		ReturnCollisionMaterialIdx = 3,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
};

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
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(TraceChannelEnum), Flags);
	}
}

void UNiagaraDataInterfaceCollisionQuery::GetAssetTagsForContext(const UObject* InAsset, const TArray<const UNiagaraDataInterface*>& InProperties, TMap<FName, uint32>& NumericKeys, TMap<FName, FString>& StringKeys) const
{
#if WITH_EDITOR
	const UNiagaraSystem* System = Cast<UNiagaraSystem>(InAsset);
	const UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(InAsset);

	// We need to check if the DI is used to access collisions in a cpu context so that artists can surface potential perf problems
	// through the content browser.

	TArray<const UNiagaraScript*> Scripts;
	if (System)
	{
		Scripts.Add(System->GetSystemSpawnScript());
		Scripts.Add(System->GetSystemUpdateScript());
		for (auto&& EmitterHandle : System->GetEmitterHandles())
		{
			const UNiagaraEmitter* HandleEmitter = EmitterHandle.GetInstance();
			if (HandleEmitter)
			{
				if (HandleEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
				{
					// Ignore gpu emitters
					continue;
				}
				TArray<UNiagaraScript*> OutScripts;
				HandleEmitter->GetScripts(OutScripts, false);
				Scripts.Append(OutScripts);
			}
		}
	}
	if (Emitter)
	{
		if (Emitter->SimTarget != ENiagaraSimTarget::GPUComputeSim)
		{
			TArray<UNiagaraScript*> OutScripts;
			Emitter->GetScripts(OutScripts, false);
			Scripts.Append(OutScripts);
		}
	}

	// Check if any CPU script uses Collsion query CPU functions
	//TODO: This is the same as in the skel mesh DI for GetFeedback, it doesn't guarantee that the DI used by these functions are THIS DI.
	// Has a possibility of false positives
	bool bHaCPUQueriesWarning = [this, &Scripts]()
	{
		for (const auto Script : Scripts)
		{
			if (Script)
			{
				for (const auto& Info : Script->GetVMExecutableData().DataInterfaceInfo)
				{
					if (Info.MatchesClass(GetClass()))
					{
						for (const auto& Func : Info.RegisteredFunctions)
						{
							if (Func.Name == NDICollisionQueryLocal::SyncTraceName || Func.Name == NDICollisionQueryLocal::AsyncTraceName)
							{
								return true;
							}
						}
					}
				}
			}
		}
		return false;
	}();

	// Note that in order for these tags to be registered, we always have to put them in place for the CDO of the object, but 
	// for readability's sake, we leave them out of non-CDO assets.
	if (bHaCPUQueriesWarning || (InAsset && InAsset->HasAnyFlags(EObjectFlags::RF_ClassDefaultObject)))
	{
		StringKeys.Add("CPUCollision") = bHaCPUQueriesWarning ? TEXT("True") : TEXT("False");

	}

#endif
	
	// Make sure and get the base implementation tags
	Super::GetAssetTagsForContext(InAsset, InProperties, NumericKeys, StringKeys);
	
}

void UNiagaraDataInterfaceCollisionQuery::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature SigDepth;
	SigDepth.Name = NDICollisionQueryLocal::SceneDepthName;
	SigDepth.bMemberFunction = true;
	SigDepth.bRequiresContext = false;
	SigDepth.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
	SigDepth.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
	SigDepth.Description = LOCTEXT("SceneDepthSignatureDescription", "Projects a given world position to view space and then queries the depth buffer with that position.");
#endif
	const FText DepthSamplePosWorldDescription = LOCTEXT("DepthSamplePosWorldDescription", "The world position where the depth should be queried. The position gets automatically transformed to view space to query the depth buffer.");
	const FText SceneDepthDescription = LOCTEXT("SceneDepthDescription", "If the query was successful this returns the scene depth, otherwise -1.");
	const FText CameraPosWorldDescription = LOCTEXT("CameraPosWorldDescription", "Returns the current camera position in world space.");
	const FText IsInsideViewDescription = LOCTEXT("IsInsideViewDescription", "Returns true if the query position could be projected to valid screen coordinates.");
	const FText SamplePosWorldDescription = LOCTEXT("SamplePosWorldDescription", "If the query was successful, this returns the world position that was recalculated from the scene depth. Otherwise returns (0, 0, 0).");
	const FText SampleWorldNormalDescription = LOCTEXT("SampleWorldNormalDescription", "If the query was successful, this returns the world normal at the sample point. Otherwise returns (0, 0, 1).");

	SigDepth.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
	SigDepth.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("DepthSamplePosWorld")), DepthSamplePosWorldDescription);
	SigDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SceneDepth")), SceneDepthDescription);
	SigDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CameraPosWorld")), CameraPosWorldDescription);
	SigDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsInsideView")), IsInsideViewDescription);
	SigDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SamplePosWorld")), SamplePosWorldDescription);
	SigDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SampleWorldNormal")), SampleWorldNormalDescription);
	
	OutFunctions.Add(SigDepth);

	FNiagaraFunctionSignature SigCustomDepth;
	SigCustomDepth.Name = NDICollisionQueryLocal::CustomDepthName;
	SigCustomDepth.bMemberFunction = true;
	SigCustomDepth.bRequiresContext = false;
	SigCustomDepth.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
	SigCustomDepth.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
	SigCustomDepth.Description = LOCTEXT("CustomDepthDescription", "Projects a given world position to view space and then queries the custom depth buffer with that position.");
#endif
	SigCustomDepth.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
	SigCustomDepth.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("DepthSamplePosWorld")), DepthSamplePosWorldDescription);
	SigCustomDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SceneDepth")), SceneDepthDescription);
	SigCustomDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CameraPosWorld")), CameraPosWorldDescription);
	SigCustomDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsInsideView")), IsInsideViewDescription);
	SigCustomDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SamplePosWorld")), SamplePosWorldDescription);
	SigCustomDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SampleWorldNormal")), SampleWorldNormalDescription);
	OutFunctions.Add(SigCustomDepth);

	FNiagaraFunctionSignature SigMeshField;
	SigMeshField.Name = NDICollisionQueryLocal::DistanceFieldName;
	SigMeshField.bMemberFunction = true;
	SigMeshField.bRequiresContext = false;
	SigMeshField.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
	SigMeshField.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
	SigMeshField.Description = LOCTEXT("DistanceFieldDescription", "Queries the global distance field for a given world position.\nPlease note that the distance field resolution gets lower the farther away the queried position is from the camera.");
#endif
	const FText FieldSamplePosWorldDescription = LOCTEXT("FieldSamplePosWorldDescription", "The world position where the distance field should be queried.");
	const FText DistanceToNearestSurfaceDescription = LOCTEXT("DistanceToNearestSurfaceDescription", "If the query was successful this returns the distance to the nearest surface, otherwise returns 0.");
	const FText FieldGradientDescription = LOCTEXT("FieldGradientDescription", "If the query was successful this returns the non-normalized direction to the nearest surface, otherwise returns (0, 0, 0).");
	const FText IsDistanceFieldValidDescription = LOCTEXT("IsDistanceFieldValidDescription", "Returns true if the global distance field is available and there was a valid value retrieved for the given sample position.");
	
	SigMeshField.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
	SigMeshField.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("FieldSamplePosWorld")), FieldSamplePosWorldDescription);
	SigMeshField.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("DistanceToNearestSurface")), DistanceToNearestSurfaceDescription);
	SigMeshField.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("FieldGradient")), FieldGradientDescription);
	SigMeshField.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsDistanceFieldValid")), IsDistanceFieldValidDescription);
    OutFunctions.Add(SigMeshField);

	FNiagaraFunctionSignature SigCpuSync;
	SigCpuSync.Name = NDICollisionQueryLocal::SyncTraceName;
	SigCpuSync.bMemberFunction = true;
	SigCpuSync.bRequiresContext = false;
	SigCpuSync.bSupportsGPU = false;
#if WITH_EDITORONLY_DATA
	SigCpuSync.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
	SigCpuSync.Description = LOCTEXT("SigCpuSyncDescription", "Traces a ray against the world using a specific channel and return the first blocking hit.");
#endif
	const FText TraceStartWorldDescription = LOCTEXT("TraceStartWorldDescription", "The world position where the line trace should start.");
	const FText TraceEndWorldDescription = LOCTEXT("TraceEndWorldDescription", "The world position where the line trace should end.");
	const FText TraceChannelDescription = LOCTEXT("TraceChannelDescription", "The trace channel to collide against. Trace channels can be configured in the project settings.");
	const FText SkipTraceDescription = LOCTEXT("SkipTraceDescription", "If true then the trace will be skipped completely.\nThis can be used as a performance optimization, as branch nodes in the graph still execute every path.");
	const FText CollisionValidDescription = LOCTEXT("CollisionValidDescription", "Returns true if the trace was not skipped and the trace was blocked by some world geometry.");
	const FText IsTraceInsideMeshDescription = LOCTEXT("IsTraceInsideMeshDescription", "If true then the trace started in penetration, i.e. with an initial blocking overlap.");
	const FText CollisionPosWorldDescription = LOCTEXT("CollisionPosWorldDescription", "If the collision is valid, this returns the location of the blocking hit.");
	const FText CollisionNormalDescription = LOCTEXT("CollisionNormalDescription", "If the collision is valid, this returns the normal at the position of the blocking hit.");
	const FText CollisionMaterialFrictionDescription = LOCTEXT("CollisionMaterialFrictionDescription", "Friction value of surface, controls how easily things can slide on this surface (0 is frictionless, higher values increase the amount of friction).");
	const FText CollisionMaterialRestitutionDescription = LOCTEXT("CollisionMaterialRestitutionDescription", "Restitution or 'bounciness' of this surface, between 0 (no bounce) and 1 (outgoing velocity is same as incoming)");
	const FText CollisionMaterialIndexDescription = LOCTEXT("CollisionMaterialIndexDescription", "Returns the index of the surface as defined in the ProjectSettings/Physics/PhysicalSurface section");
	
	SigCpuSync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
	SigCpuSync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TraceStartWorld")), TraceStartWorldDescription);
	SigCpuSync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TraceEndWorld")), TraceEndWorldDescription);
	SigCpuSync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(TraceChannelEnum), TEXT("TraceChannel")), TraceChannelDescription);
	SigCpuSync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SkipTrace")), SkipTraceDescription);
	SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("CollisionValid")), CollisionValidDescription);
	SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsTraceInsideMesh")), IsTraceInsideMeshDescription);
	SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionPosWorld")), CollisionPosWorldDescription);
	SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionNormal")), CollisionNormalDescription);
	SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialFriction")), CollisionMaterialFrictionDescription);
	SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialRestitution")), CollisionMaterialRestitutionDescription);
	SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("CollisionMaterialIndex")), CollisionMaterialIndexDescription);
	OutFunctions.Add(SigCpuSync);

	FNiagaraFunctionSignature SigCpuAsync;
	SigCpuAsync.Name = NDICollisionQueryLocal::AsyncTraceName;
	SigCpuAsync.bMemberFunction = true;
	SigCpuAsync.bRequiresContext = false;
	SigCpuAsync.bSupportsGPU = false;
#if WITH_EDITORONLY_DATA
	SigCpuAsync.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
	SigCpuAsync.Description = LOCTEXT("SigCpuAsyncDescription", "Traces a ray against the world using a specific channel and return the first blocking hit the next frame.\nNote that this is the ASYNC version of the trace function, meaning it will not returns the result right away, but with one frame latency.");
#endif
	const FText PreviousFrameQueryIDDescription = LOCTEXT("PreviousFrameQueryIDDescription", "The query ID returned from the last frame's async trace call.\nRegardless if it is a valid ID or not this function call with issue a new async line trace, but it will only return results with a valid ID.");
	const FText NextFrameQueryIDDescription = LOCTEXT("NextFrameQueryIDDescription", "The query ID to save and use as input to this function in the next frame.");
	
	SigCpuAsync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
	SigCpuAsync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("PreviousFrameQueryID")), PreviousFrameQueryIDDescription);
	SigCpuAsync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TraceStartWorld")), TraceStartWorldDescription);
	SigCpuAsync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TraceEndWorld")), TraceEndWorldDescription);
	SigCpuAsync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(TraceChannelEnum), TEXT("TraceChannel")), TraceChannelDescription);
	SigCpuAsync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SkipTrace")), SkipTraceDescription);
	SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NextFrameQueryID")), NextFrameQueryIDDescription);
	SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("CollisionValid")), CollisionValidDescription);
	SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsTraceInsideMesh")), IsTraceInsideMeshDescription);
	SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionPosWorld")), CollisionPosWorldDescription);
	SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionNormal")), CollisionNormalDescription);
	SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialFriction")), CollisionMaterialFrictionDescription);
	SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialRestitution")), CollisionMaterialRestitutionDescription);
	SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("CollisionMaterialIndex")), CollisionMaterialIndexDescription);
	OutFunctions.Add(SigCpuAsync);
}

// build the shader function HLSL; function name is passed in, as it's defined per-DI; that way, configuration could change
// the HLSL in the spirit of a static switch
// TODO: need a way to identify each specific function here

// 
#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceCollisionQuery::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if ( (FunctionInfo.DefinitionName == NDICollisionQueryLocal::SceneDepthName) ||
		 (FunctionInfo.DefinitionName == NDICollisionQueryLocal::CustomDepthName) ||
		 (FunctionInfo.DefinitionName == NDICollisionQueryLocal::DistanceFieldName) )
	{
		return true;
	}

	return false;
}

bool UNiagaraDataInterfaceCollisionQuery::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	bool bWasChanged = false;

	// The distance field query got a new output at some point, but there exists no custom version for it
	if (FunctionSignature.Name == NDICollisionQueryLocal::DistanceFieldName && FunctionSignature.Outputs.Num() == 2)
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
		if (FunctionSignature.Name == NDICollisionQueryLocal::SyncTraceName || FunctionSignature.Name == NDICollisionQueryLocal::AsyncTraceName)
		{
			FunctionSignature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SkipTrace")));
			bWasChanged = true;
		}
	}

	// Added the physical material ID as a result for line traces
	if (FunctionSignature.FunctionVersion < FNiagaraCollisionDIFunctionVersion::ReturnCollisionMaterialIdx)
	{
		if (FunctionSignature.Name == NDICollisionQueryLocal::SyncTraceName || FunctionSignature.Name == NDICollisionQueryLocal::AsyncTraceName)
		{
			FunctionSignature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("CollisionMaterialIndex")));
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
	if (Function.Name == NDICollisionQueryLocal::DistanceFieldName)
	{
		if (!IsDistanceFieldEnabled())
		{
			OutValidationErrors.Add(LOCTEXT("NiagaraDistanceFieldNotEnabledMsg", "The mesh distance field generation is currently not enabled, please check the project settings.\nNiagara cannot query the distance field otherwise."));
		}
	}
}
#endif

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceCollisionQuery::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL.Appendf(TEXT("#include \"%s\"\n"), NDICollisionQueryLocal::CommonShaderFile);
}

void UNiagaraDataInterfaceCollisionQuery::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{TEXT("ParameterName"),	ParamInfo.DataInterfaceHLSLSymbol},
	};

	FString TemplateFile;
	LoadShaderSourceFile(NDICollisionQueryLocal::TemplateShaderFile, EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}
#endif

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, PerformQuerySyncCPU);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, PerformQueryAsyncCPU);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, QuerySceneDepth);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, QueryMeshDistanceField);

void UNiagaraDataInterfaceCollisionQuery::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == NDICollisionQueryLocal::SyncTraceName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, PerformQuerySyncCPU)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == NDICollisionQueryLocal::AsyncTraceName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, PerformQueryAsyncCPU)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == NDICollisionQueryLocal::SceneDepthName ||
			 BindingInfo.Name == NDICollisionQueryLocal::CustomDepthName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, QuerySceneDepth)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == NDICollisionQueryLocal::DistanceFieldName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCollisionQuery, QueryMeshDistanceField)::Bind(this, OutFunc);
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Could not find data interface external function. %s\n"),
			*BindingInfo.Name.ToString());
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceCollisionQuery::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!Super::AppendCompileHash(InVisitor))
	{
		return false;
	}

	InVisitor->UpdatePOD(TEXT("NiagaraCollisionDI_DistanceField"), IsDistanceFieldEnabled());
	InVisitor->UpdateString(TEXT("NDICollisionQueryCommonHLSLSource"), GetShaderFileHash(NDICollisionQueryLocal::CommonShaderFile, EShaderPlatform::SP_PCD3D_SM5).ToString());
	InVisitor->UpdateString(TEXT("NDICollisionQueryTemplateHLSLSource"), GetShaderFileHash(NDICollisionQueryLocal::TemplateShaderFile, EShaderPlatform::SP_PCD3D_SM5).ToString());

	return true;
}
#endif

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
	VectorVM::FExternalFuncRegisterHandler<int32> OutPhysicalMaterialIdx(Context);

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
			*OutPhysicalMaterialIdx.GetDestAndAdvance() = Res.PhysicalMaterialIdx;
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
			*OutPhysicalMaterialIdx.GetDestAndAdvance() = 0;
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
	VectorVM::FExternalFuncRegisterHandler<int32> OutPhysicalMaterialIdx(Context);

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
			*OutPhysicalMaterialIdx.GetDestAndAdvance() = Res.PhysicalMaterialIdx;
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
			*OutPhysicalMaterialIdx.GetDestAndAdvance() = 0;
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
		GlobalDistanceFieldParameters.Bind(ParameterMap);
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		
		// Bind distance field parameters
		if (GlobalDistanceFieldParameters.IsBound())
		{
			check(Context.Batcher);
			GlobalDistanceFieldParameters.Set(RHICmdList, ComputeShaderRHI, Context.Batcher->GetGlobalDistanceFieldParameters());
		}		
	}

private:
	LAYOUT_FIELD(FGlobalDistanceFieldParameters, GlobalDistanceFieldParameters);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_CollisionQuery);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceCollisionQuery, FNiagaraDataInterfaceParametersCS_CollisionQuery);

#undef LOCTEXT_NAMESPACE