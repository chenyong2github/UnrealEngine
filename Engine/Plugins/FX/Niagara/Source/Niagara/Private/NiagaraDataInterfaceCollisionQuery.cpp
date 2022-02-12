// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceCollisionQuery.h"

#include "GlobalDistanceFieldParameters.h"
#include "NiagaraComponent.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraGpuComputeDispatch.h"
#include "NiagaraRayTracingHelper.h"
#include "NiagaraSimStageData.h"
#include "NiagaraStats.h"
#include "NiagaraTypes.h"
#include "NiagaraWorldManager.h"
#include "RenderResource.h"
#include "Shader.h"
#include "ShaderCore.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterUtils.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceCollisionQuery"

void OnHWRTCollisionsEnabledChanged(IConsoleVariable* CVar)
{
	//Force a reinit of everything just to be safe.
	FNiagaraSystemUpdateContext Context;
	Context.AddAll(true);
}

int32 GEnableGPUHWRTCollisions = 1;
static FAutoConsoleVariableRef CVarEnableGPUHWRTCollisions(
	TEXT("fx.Niagara.Collision.EnableGPURayTracedCollisions"),
	GEnableGPUHWRTCollisions,
	TEXT("If greater than zero, GPU hardware ray trace collisions are enabled."),
	FConsoleVariableDelegate::CreateStatic(&OnHWRTCollisionsEnabledChanged),
	ECVF_Default
);

namespace NDICollisionQueryLocal
{
	static const TCHAR* CommonShaderFile = TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceCollisionQuery.ush");
	static const TCHAR* TemplateShaderFile = TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceCollisionQueryTemplate.ush");

	static const FName SceneDepthName(TEXT("QuerySceneDepthGPU"));
	static const FName CustomDepthName(TEXT("QueryCustomDepthGPU"));
	static const FName DistanceFieldName(TEXT("QueryMeshDistanceFieldGPU"));
	static const FName SyncTraceName(TEXT("PerformCollisionQuerySyncCPU"));
	static const FName AsyncTraceName(TEXT("PerformCollisionQueryAsyncCPU"));
	static const FName IssueAsyncRayTraceName(TEXT("IssueAsyncRayTraceGpu"));
	static const FName CreateAsyncRayTraceName(TEXT("CreateAsyncRayTraceGpu"));
	static const FName ReserveAsyncRayTraceName(TEXT("ReserveAsyncRayTraceGpu"));
	static const FName ReadAsyncRayTraceName(TEXT("ReadAsyncRayTraceGpu"));

	static const FString RayTracingEnabledParamName(TEXT("RayTracingEnabled_"));
	static const FString MaxRayTraceCountParamName(TEXT("MaxRayTraceCount_"));
	static const FString RayRequestsParamName(TEXT("RayRequests_"));
	static const FString RayRequestsOffsetParamName(TEXT("RayRequestsOffset_"));
	static const FString IntersectionResultsParamName(TEXT("IntersectionResults_"));
	static const FString IntersectionResultsOffsetParamName(TEXT("IntersectionResultsOffset_"));
	static const FString RayTraceCountsParamName(TEXT("RayTraceCounts_"));
	static const FString RayTraceCountsOffsetParamName(TEXT("RayTraceCountsOffset_"));
	static const FString SystemLWCTileName(TEXT("SystemLWCTile_"));
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
		LargeWorldCoordinates = 4,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
};
struct FNiagaraDataIntefaceProxyCollisionQuery : public FNiagaraDataInterfaceProxy
{
#if RHI_RAYTRACING
	int32 MaxTracesPerParticle = 0;
	uint32 MaxRetraces = 0;
#endif

	virtual ~FNiagaraDataIntefaceProxyCollisionQuery()
	{
	}

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}

	virtual void PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context) override
	{
		FNiagaraDataInterfaceProxy::PreStage(RHICmdList, Context);

#if RHI_RAYTRACING
		if (IsRayTracingEnabled() && GEnableGPUHWRTCollisions && MaxTracesPerParticle > 0)
		{
			//Accumulate the total ray requests for this DI for all dispatches in the stage.
			int32 RayRequests = MaxTracesPerParticle * Context.SimStageData->DestinationNumInstances;
			FNiagaraRayTracingHelper& RTHelper = Context.ComputeDispatchInterface->GetRayTracingHelper();
			RTHelper.AddToDispatch(this, RayRequests, MaxRetraces);
		}
#endif
	}

#if RHI_RAYTRACING
	virtual bool RequiresPreStageFinalize() const override
	{
		return true;
	}

	virtual void FinalizePreStage(FRHICommandList& RHICmdList, const FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface) override
	{
		FNiagaraRayTracingHelper& RTHelper = ComputeDispatchInterface->GetRayTracingHelper();
		if (IsRayTracingEnabled() && GEnableGPUHWRTCollisions && MaxTracesPerParticle > 0)
		{
			RTHelper.BuildDispatch(RHICmdList, this);
		}
		else
		{
			RTHelper.BuildDummyDispatch(RHICmdList);
		}
	}
#endif

	void RenderThreadInitialize(int32 InMaxTracesPerParticle, uint32 InMaxRetraces)
	{
#if RHI_RAYTRACING
		MaxRetraces = InMaxRetraces;
		MaxTracesPerParticle = InMaxTracesPerParticle;
#endif
	}
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

void UNiagaraDataInterfaceCollisionQuery::PostLoad()
{
	Super::PostLoad();

	if (MaxTracesPerParticle)
	{
		MarkRenderDataDirty();
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
	{
		FNiagaraFunctionSignature SigDepth;
		SigDepth.Name = NDICollisionQueryLocal::SceneDepthName;
		SigDepth.bMemberFunction = true;
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
		SigDepth.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("DepthSamplePosWorld")), DepthSamplePosWorldDescription);
		SigDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SceneDepth")), SceneDepthDescription);
		SigDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("CameraPosWorld")), CameraPosWorldDescription);
		SigDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsInsideView")), IsInsideViewDescription);
		SigDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("SamplePosWorld")), SamplePosWorldDescription);
		SigDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SampleWorldNormal")), SampleWorldNormalDescription);
	
		OutFunctions.Add(SigDepth);

		
		FNiagaraFunctionSignature SigCustomDepth;
		SigCustomDepth.Name = NDICollisionQueryLocal::CustomDepthName;
		SigCustomDepth.bMemberFunction = true;
		SigCustomDepth.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
		SigCustomDepth.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
		SigCustomDepth.Description = LOCTEXT("CustomDepthDescription", "Projects a given world position to view space and then queries the custom depth buffer with that position.");
#endif
		SigCustomDepth.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
		SigCustomDepth.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("DepthSamplePosWorld")), DepthSamplePosWorldDescription);
		SigCustomDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SceneDepth")), SceneDepthDescription);
		SigCustomDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("CameraPosWorld")), CameraPosWorldDescription);
		SigCustomDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsInsideView")), IsInsideViewDescription);
		SigCustomDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("SamplePosWorld")), SamplePosWorldDescription);
		SigCustomDepth.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SampleWorldNormal")), SampleWorldNormalDescription);
		OutFunctions.Add(SigCustomDepth);
	}

	{
		FNiagaraFunctionSignature SigMeshField;
		SigMeshField.Name = NDICollisionQueryLocal::DistanceFieldName;
		SigMeshField.bMemberFunction = true;
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
		SigMeshField.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("FieldSamplePosWorld")), FieldSamplePosWorldDescription);
		SigMeshField.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("DistanceToNearestSurface")), DistanceToNearestSurfaceDescription);
		SigMeshField.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("FieldGradient")), FieldGradientDescription);
		SigMeshField.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsDistanceFieldValid")), IsDistanceFieldValidDescription);
		OutFunctions.Add(SigMeshField);
	}

	
	const FText TraceStartWorldDescription = LOCTEXT("TraceStartWorldDescription", "Ray starting point in world space");
	const FText TraceEndWorldDescription = LOCTEXT("TraceEndWorldDescription", "Ray end point in world space");
	{
		const FText AsyncTraceChannelDescription = LOCTEXT("TraceChannelDescription", "Currently unused, will represent the trace channels for which geometry the trace should test against");
		const FText QueryIDDescription = LOCTEXT("QueryIDDescription", "Unique (for this frame) index of the query being enqueued (used in subsequent frames to retrieve results).  Must be less than MaxRayTraceCount");
		const FText IsQueryValidDescription = LOCTEXT("IsQueryValidDescription", "Returns true if the query was enqueued");
		
		FNiagaraFunctionSignature& IssueRayTrace = OutFunctions.AddDefaulted_GetRef();
		IssueRayTrace.Name = NDICollisionQueryLocal::IssueAsyncRayTraceName;
		IssueRayTrace.bRequiresExecPin = true;
		IssueRayTrace.bMemberFunction = true;
		IssueRayTrace.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
		IssueRayTrace.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
		IssueRayTrace.Description = LOCTEXT("IssueAsync_RayTraceDescription", "Enqueues a GPU raytrace with the result being available the following frame");
#endif
		IssueRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
		IssueRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("QueryID")), QueryIDDescription);
		IssueRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("TraceStartWorld")), TraceStartWorldDescription);
		IssueRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("TraceEndWorld")), TraceEndWorldDescription);
		IssueRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("TraceChannel")), AsyncTraceChannelDescription);
		IssueRayTrace.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsQueryValid")), IsQueryValidDescription);
		
		FNiagaraFunctionSignature& CreateRayTrace = OutFunctions.AddDefaulted_GetRef();
		CreateRayTrace.Name = NDICollisionQueryLocal::CreateAsyncRayTraceName;
		CreateRayTrace.bRequiresExecPin = true;
		CreateRayTrace.bMemberFunction = true;
		CreateRayTrace.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
		CreateRayTrace.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
		CreateRayTrace.Description = LOCTEXT("CreateAsync_RayTraceDescription", "Creates a GPU raytrace with the result being available the following frame (index is returned)");
#endif
		CreateRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
		CreateRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("TraceStartWorld")), TraceStartWorldDescription);
		CreateRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("TraceEndWorld")), TraceEndWorldDescription);
		CreateRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("TraceChannel")), AsyncTraceChannelDescription);
		CreateRayTrace.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("QueryID")), QueryIDDescription);
		CreateRayTrace.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsQueryValid")), IsQueryValidDescription);
	}

	{
		FNiagaraFunctionSignature& IssueRayTrace = OutFunctions.AddDefaulted_GetRef();
		IssueRayTrace.Name = NDICollisionQueryLocal::ReserveAsyncRayTraceName;
		IssueRayTrace.bRequiresExecPin = true;
		IssueRayTrace.bMemberFunction = true;
		IssueRayTrace.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
		IssueRayTrace.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
		IssueRayTrace.Description = LOCTEXT("ReserveAsync_RayTraceDescription", "Reserves a number of ray trace request slots");
#endif
		IssueRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
		IssueRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("TraceCount")), LOCTEXT("ReserveAsync_QueryIDDescription", "Number of async raytrace requests to be reserved"));
		IssueRayTrace.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("FirstQueryID")), LOCTEXT("ReserveAsync_TraceChannelDescription", "The first index in the block reserved through this call"));
		IssueRayTrace.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsQueryValid")), LOCTEXT("ReserveAsync_IsQueryValidDescription", "Returns true if the requested indices were reserved"));
	}

	{
		FNiagaraFunctionSignature& ReadRayTrace = OutFunctions.AddDefaulted_GetRef();
		ReadRayTrace.Name = NDICollisionQueryLocal::ReadAsyncRayTraceName;
		ReadRayTrace.bMemberFunction = true;
		ReadRayTrace.bSupportsCPU = false;
#if WITH_EDITORONLY_DATA
		ReadRayTrace.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
		ReadRayTrace.Description = LOCTEXT("ReadAsync_RayTraceDescription", "Reads the results of a previously enqueued GPU ray trace");
#endif
		ReadRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
		ReadRayTrace.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("PreviousFrameQueryID")), LOCTEXT("ReadAsync_PreviousFrameQueryIDDescription", "The index of the results being retrieved"));
		ReadRayTrace.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("CollisionValid")), LOCTEXT("ReadAsync_CollisionValidDescription", "Returns true if a Hit was encountered"));
		ReadRayTrace.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionDistance")), LOCTEXT("ReadAsync_CollisionDistanceDescription", "The distance in world space from the ray starting point to the intersection"));
		ReadRayTrace.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("CollisionPosWorld")), LOCTEXT("ReadAsync_CollisionPosWorldDescription", "The point in world space where the intersection occured"));
		ReadRayTrace.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionNormal")), LOCTEXT("ReadAsync_CollisionNormalDescription", "The surface normal of the world geometry at the point of intersection"));
	}

	{
		const FText TraceChannelDescription = LOCTEXT("TraceChannelDescription", "The trace channel to collide against. Trace channels can be configured in the project settings.");
		const FText SkipTraceDescription = LOCTEXT("SkipTraceDescription", "If true then the trace will be skipped completely.\nThis can be used as a performance optimization, as branch nodes in the graph still execute every path.");
		const FText CollisionValidDescription = LOCTEXT("CollisionValidDescription", "Returns true if the trace was not skipped and the trace was blocked by some world geometry.");
		const FText IsTraceInsideMeshDescription = LOCTEXT("IsTraceInsideMeshDescription", "If true then the trace started in penetration, i.e. with an initial blocking overlap.");
		const FText CollisionPosWorldDescription = LOCTEXT("CollisionPosWorldDescription", "If the collision is valid, this returns the location of the blocking hit.");
		const FText CollisionNormalDescription = LOCTEXT("CollisionNormalDescription", "If the collision is valid, this returns the normal at the position of the blocking hit.");
		const FText CollisionMaterialFrictionDescription = LOCTEXT("CollisionMaterialFrictionDescription", "Friction value of surface, controls how easily things can slide on this surface (0 is frictionless, higher values increase the amount of friction).");
		const FText CollisionMaterialRestitutionDescription = LOCTEXT("CollisionMaterialRestitutionDescription", "Restitution or 'bounciness' of this surface, between 0 (no bounce) and 1 (outgoing velocity is same as incoming)");
		const FText CollisionMaterialIndexDescription = LOCTEXT("CollisionMaterialIndexDescription", "Returns the index of the surface as defined in the ProjectSettings/Physics/PhysicalSurface section");
		
		FNiagaraFunctionSignature SigCpuSync;
		SigCpuSync.Name = NDICollisionQueryLocal::SyncTraceName;
		SigCpuSync.bMemberFunction = true;
		SigCpuSync.bSupportsGPU = false;
#if WITH_EDITORONLY_DATA
		SigCpuSync.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
		SigCpuSync.Description = LOCTEXT("SigCpuSyncDescription", "Traces a ray against the world using a specific channel and return the first blocking hit.");
#endif
	
		SigCpuSync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
		SigCpuSync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("TraceStartWorld")), TraceStartWorldDescription);
		SigCpuSync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("TraceEndWorld")), TraceEndWorldDescription);
		SigCpuSync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(TraceChannelEnum), TEXT("TraceChannel")), TraceChannelDescription);
		SigCpuSync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SkipTrace")), SkipTraceDescription);
		SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("CollisionValid")), CollisionValidDescription);
		SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsTraceInsideMesh")), IsTraceInsideMeshDescription);
		SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("CollisionPosWorld")), CollisionPosWorldDescription);
		SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionNormal")), CollisionNormalDescription);
		SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialFriction")), CollisionMaterialFrictionDescription);
		SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialRestitution")), CollisionMaterialRestitutionDescription);
		SigCpuSync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("CollisionMaterialIndex")), CollisionMaterialIndexDescription);
		OutFunctions.Add(SigCpuSync);
		
		FNiagaraFunctionSignature SigCpuAsync;
		SigCpuAsync.Name = NDICollisionQueryLocal::AsyncTraceName;
		SigCpuAsync.bMemberFunction = true;
		SigCpuAsync.bSupportsGPU = false;
#if WITH_EDITORONLY_DATA
		SigCpuAsync.FunctionVersion = FNiagaraCollisionDIFunctionVersion::LatestVersion;
		SigCpuAsync.Description = LOCTEXT("SigCpuAsyncDescription", "Traces a ray against the world using a specific channel and return the first blocking hit the next frame.\nNote that this is the ASYNC version of the trace function, meaning it will not returns the result right away, but with one frame latency.");
#endif
	
		SigCpuAsync.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("CollisionQuery")));
		SigCpuAsync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("PreviousFrameQueryID")), LOCTEXT("TraceAsync_PreviousFrameQueryIDDescription", "The query ID returned from the last frame's async trace call.\nRegardless if it is a valid ID or not this function call with issue a new async line trace, but it will only return results with a valid ID."));
		SigCpuAsync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("TraceStartWorld")), TraceStartWorldDescription);
		SigCpuAsync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("TraceEndWorld")), TraceEndWorldDescription);
		SigCpuAsync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(TraceChannelEnum), TEXT("TraceChannel")), TraceChannelDescription);
		SigCpuAsync.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("SkipTrace")), SkipTraceDescription);
		SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NextFrameQueryID")), LOCTEXT("TraceAsync_NextFrameQueryIDDescription", "The query ID to save and use as input to this function in the next frame."));
		SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("CollisionValid")), CollisionValidDescription);
		SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsTraceInsideMesh")), IsTraceInsideMeshDescription);
		SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("CollisionPosWorld")), CollisionPosWorldDescription);
		SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionNormal")), CollisionNormalDescription);
		SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialFriction")), CollisionMaterialFrictionDescription);
		SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMaterialRestitution")), CollisionMaterialRestitutionDescription);
		SigCpuAsync.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("CollisionMaterialIndex")), CollisionMaterialIndexDescription);
		OutFunctions.Add(SigCpuAsync);
	}
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
		 (FunctionInfo.DefinitionName == NDICollisionQueryLocal::DistanceFieldName) ||
		 (FunctionInfo.DefinitionName == NDICollisionQueryLocal::IssueAsyncRayTraceName) ||
		 (FunctionInfo.DefinitionName == NDICollisionQueryLocal::CreateAsyncRayTraceName) ||
		 (FunctionInfo.DefinitionName == NDICollisionQueryLocal::ReserveAsyncRayTraceName) ||
		 (FunctionInfo.DefinitionName == NDICollisionQueryLocal::ReadAsyncRayTraceName) )
	{
		return true;
	}

	return false;
}

bool UNiagaraDataInterfaceCollisionQuery::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	// always upgrade to the latest version
	if (FunctionSignature.FunctionVersion < FNiagaraCollisionDIFunctionVersion::LatestVersion)
	{
		TArray<FNiagaraFunctionSignature> AllFunctions;
		GetFunctions(AllFunctions);
		for (const FNiagaraFunctionSignature& Sig : AllFunctions)
		{
			if (FunctionSignature.Name == Sig.Name)
			{
				FunctionSignature = Sig;
				return true;
			}
		}
	}

	return false;
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
			OutValidationErrors.Add(NSLOCTEXT("NiagaraDataInterfaceCollisionQuery", "NiagaraDistanceFieldNotEnabledMsg", "The mesh distance field generation is currently not enabled, please check the project settings.\nNiagara cannot query the distance field otherwise."));
		}
	}
}
#endif

bool UNiagaraDataInterfaceCollisionQuery::RequiresRayTracingScene() const
{
	return IsRayTracingEnabled() && GEnableGPUHWRTCollisions && MaxTracesPerParticle > 0;
}

#if WITH_EDITORONLY_DATA
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
	else
	{
		UE_LOG(LogNiagara, Display, TEXT("Could not find data interface external function in %s. %s\n"),
			*GetPathNameSafe(this), *BindingInfo.Name.ToString());
	}
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceCollisionQuery::GetCommonHLSL(FString& OutHlsl)
{
	OutHlsl.Appendf(TEXT("#include \"%s\"\n"), NDICollisionQueryLocal::CommonShaderFile);
}

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

void UNiagaraDataInterfaceCollisionQuery::ModifyCompilationEnvironment(EShaderPlatform ShaderPlatform, FShaderCompilerEnvironment& OutEnvironment) const
{
	Super::ModifyCompilationEnvironment(ShaderPlatform, OutEnvironment);

	OutEnvironment.SetDefine(TEXT("NIAGARA_SUPPORTS_RAY_TRACING"), ShouldCompileRayTracingShadersForProject(ShaderPlatform) ? 1 : 0);
}

#endif

void UNiagaraDataInterfaceCollisionQuery::PerformQuerySyncCPU(FVectorVMExternalFunctionContext & Context)
{
	VectorVM::FUserPtrHandler<CQDIPerInstanceData> InstanceData(Context);

	FNDIInputParam<FNiagaraPosition> StartPosParam(Context);
	FNDIInputParam<FNiagaraPosition> EndPosParam(Context);
	FNDIInputParam<ECollisionChannel> TraceChannelParam(Context);
	FNDIInputParam<FNiagaraBool> IsSkipTrace(Context);
	
	FNDIOutputParam<FNiagaraBool> OutQueryValid(Context);
	FNDIOutputParam<FNiagaraBool> OutInsideMesh(Context);
	FNDIOutputParam<FNiagaraPosition> OutCollisionPos(Context);
	FNDIOutputParam<FVector3f> OutCollisionNormal(Context);
	FNDIOutputParam<float> OutFriction(Context);
	FNDIOutputParam<float> OutRestitution(Context);
	FNDIOutputParam<int32> OutPhysicalMaterialIdx(Context);

	FNiagaraLWCConverter LWCConverter = InstanceData->SystemInstance->GetLWCConverter();

	FScopeLock ScopeLock(&CriticalSection);
	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		FNiagaraPosition TraceStart = StartPosParam.GetAndAdvance();
		FNiagaraPosition TraceEnd = EndPosParam.GetAndAdvance();
		ECollisionChannel TraceChannel = TraceChannelParam.GetAndAdvance();
		bool Skip = IsSkipTrace.GetAndAdvance();
		ensure(!TraceStart.ContainsNaN());
		ensure(!TraceEnd.ContainsNaN());
		FNiagaraDICollsionQueryResult Res;

		if (!Skip && InstanceData->CollisionBatch.PerformQuery(LWCConverter.ConvertSimulationPositionToWorld(TraceStart), LWCConverter.ConvertSimulationPositionToWorld(TraceEnd), Res, TraceChannel))
		{
			OutQueryValid.SetAndAdvance(true);
			OutInsideMesh.SetAndAdvance(Res.IsInsideMesh);
			OutCollisionPos.SetAndAdvance(LWCConverter.ConvertWorldToSimulationPosition(Res.CollisionPos));
			OutCollisionNormal.SetAndAdvance((FVector3f)Res.CollisionNormal);
			OutFriction.SetAndAdvance(Res.Friction);
			OutRestitution.SetAndAdvance(Res.Restitution);
			OutPhysicalMaterialIdx.SetAndAdvance(Res.PhysicalMaterialIdx);
		}
		else
		{			
			OutQueryValid.SetAndAdvance(false);
			OutInsideMesh.SetAndAdvance(false);
			OutCollisionPos.SetAndAdvance(FVector3f::ZeroVector);
			OutCollisionNormal.SetAndAdvance(FVector3f::ZeroVector);
			OutFriction.SetAndAdvance(0);
			OutRestitution.SetAndAdvance(0);
			OutPhysicalMaterialIdx.SetAndAdvance(0);
		}
	}
}

void UNiagaraDataInterfaceCollisionQuery::PerformQueryAsyncCPU(FVectorVMExternalFunctionContext & Context)
{
	VectorVM::FUserPtrHandler<CQDIPerInstanceData> InstanceData(Context);

	FNDIInputParam<int32> InIDParam(Context);
	FNDIInputParam<FNiagaraPosition> StartPosParam(Context);
	FNDIInputParam<FNiagaraPosition> EndPosParam(Context);
	FNDIInputParam<ECollisionChannel> TraceChannelParam(Context);
	FNDIInputParam<FNiagaraBool> IsSkipTrace(Context);
	
	FNDIOutputParam<int32> OutQueryID(Context);
	FNDIOutputParam<FNiagaraBool> OutQueryValid(Context);
	FNDIOutputParam<FNiagaraBool> OutInsideMesh(Context);
	FNDIOutputParam<FNiagaraPosition> OutCollisionPos(Context);
	FNDIOutputParam<FVector3f> OutCollisionNormal(Context);
	FNDIOutputParam<float> OutFriction(Context);
	FNDIOutputParam<float> OutRestitution(Context);
	FNDIOutputParam<int32> OutPhysicalMaterialIdx(Context);

	FNiagaraLWCConverter LWCConverter = InstanceData->SystemInstance->GetLWCConverter();

	FScopeLock ScopeLock(&CriticalSection);
	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		FNiagaraPosition TraceStart = StartPosParam.GetAndAdvance();
		FNiagaraPosition TraceEnd = EndPosParam.GetAndAdvance();
		ECollisionChannel TraceChannel = TraceChannelParam.GetAndAdvance();
		bool Skip = IsSkipTrace.GetAndAdvance();
		ensure(!TraceStart.ContainsNaN());
		ensure(!TraceEnd.ContainsNaN());

		int QueryID = Skip ? INDEX_NONE : InstanceData->CollisionBatch.SubmitQuery(LWCConverter.ConvertSimulationPositionToWorld(TraceStart), LWCConverter.ConvertSimulationPositionToWorld(TraceEnd), TraceChannel);
		OutQueryID.SetAndAdvance(QueryID);

		// try to retrieve a query with the supplied query ID
		FNiagaraDICollsionQueryResult Res;
		int32 ID = InIDParam.GetAndAdvance();
		if (ID != INDEX_NONE && InstanceData->CollisionBatch.GetQueryResult(ID, Res))
		{
			OutQueryValid.SetAndAdvance(true);
			OutInsideMesh.SetAndAdvance(Res.IsInsideMesh);
			OutCollisionPos.SetAndAdvance(LWCConverter.ConvertWorldToSimulationPosition(Res.CollisionPos));
			OutCollisionNormal.SetAndAdvance((FVector3f)Res.CollisionNormal);
			OutFriction.SetAndAdvance(Res.Friction);
			OutRestitution.SetAndAdvance(Res.Restitution);
			OutPhysicalMaterialIdx.SetAndAdvance(Res.PhysicalMaterialIdx);
		}
		else
		{
			OutQueryValid.SetAndAdvance(false);
			OutInsideMesh.SetAndAdvance(false);
			OutCollisionPos.SetAndAdvance(FVector3f::ZeroVector);
			OutCollisionNormal.SetAndAdvance(FVector3f::ZeroVector);
			OutFriction.SetAndAdvance(0);
			OutRestitution.SetAndAdvance(0);
			OutPhysicalMaterialIdx.SetAndAdvance(0);
		}
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

bool UNiagaraDataInterfaceCollisionQuery::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceCollisionQuery* OtherTyped = CastChecked<const UNiagaraDataInterfaceCollisionQuery>(Other);
	return OtherTyped->MaxTracesPerParticle == MaxTracesPerParticle;
}

bool UNiagaraDataInterfaceCollisionQuery::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceCollisionQuery* OtherTyped = CastChecked<UNiagaraDataInterfaceCollisionQuery>(Destination);
	OtherTyped->MaxTracesPerParticle = MaxTracesPerParticle;
	OtherTyped->MaxRetraces = MaxRetraces;
	OtherTyped->MarkRenderDataDirty();
	return true;
}

void UNiagaraDataInterfaceCollisionQuery::PushToRenderThreadImpl()
{
	FNiagaraDataIntefaceProxyCollisionQuery* RT_Proxy = GetProxyAs<FNiagaraDataIntefaceProxyCollisionQuery>();

	// Push Updates to Proxy, first release any resources
	ENQUEUE_RENDER_COMMAND(FUpdateDI)(
		[RT_Proxy, RT_MaxTracesPerParticle = MaxTracesPerParticle, RT_MaxRetraces = MaxRetraces](FRHICommandListImmediate& RHICmdList)
		{
			RT_Proxy->RenderThreadInitialize(RT_MaxTracesPerParticle, RT_MaxRetraces);
		});
}

#if WITH_EDITOR
void UNiagaraDataInterfaceCollisionQuery::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceCollisionQuery, MaxTracesPerParticle))
	{
		MarkRenderDataDirty();
	}
}
#endif

//////////////////////////////////////////////////////////////////////////

struct FNiagaraDataInterfaceParametersCS_CollisionQuery : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_CollisionQuery, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		GlobalDistanceFieldParameters.Bind(ParameterMap);
		SystemLWCTileParam.Bind(ParameterMap, *(NDICollisionQueryLocal::SystemLWCTileName + ParameterInfo.DataInterfaceHLSLSymbol));
#if RHI_RAYTRACING
		RayTracingEnabledParam.Bind(ParameterMap, *(NDICollisionQueryLocal::RayTracingEnabledParamName + ParameterInfo.DataInterfaceHLSLSymbol));
		MaxRayTraceCountParam.Bind(ParameterMap, *(NDICollisionQueryLocal::MaxRayTraceCountParamName + ParameterInfo.DataInterfaceHLSLSymbol));
		RayRequestsParam.Bind(ParameterMap, *(NDICollisionQueryLocal::RayRequestsParamName + ParameterInfo.DataInterfaceHLSLSymbol));
		RayRequestOffsetParam.Bind(ParameterMap, *(NDICollisionQueryLocal::RayRequestsOffsetParamName + ParameterInfo.DataInterfaceHLSLSymbol));
		IntersectionResultsParam.Bind(ParameterMap, *(NDICollisionQueryLocal::IntersectionResultsParamName + ParameterInfo.DataInterfaceHLSLSymbol));
		IntersectionResultOffsetParam.Bind(ParameterMap, *(NDICollisionQueryLocal::IntersectionResultsOffsetParamName + ParameterInfo.DataInterfaceHLSLSymbol));
		RayTraceCountsParam.Bind(ParameterMap, *(NDICollisionQueryLocal::RayTraceCountsParamName + ParameterInfo.DataInterfaceHLSLSymbol));
		RayTraceCountsOffsetParam.Bind(ParameterMap, *(NDICollisionQueryLocal::RayTraceCountsOffsetParamName + ParameterInfo.DataInterfaceHLSLSymbol));
#endif
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FNiagaraDataIntefaceProxyCollisionQuery* QueryDI = (FNiagaraDataIntefaceProxyCollisionQuery*)Context.DataInterface;
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		SetShaderValue(RHICmdList, ComputeShaderRHI, SystemLWCTileParam, Context.SystemLWCTile);
		
		// Bind distance field parameters
		if (GlobalDistanceFieldParameters.IsBound())
		{
			check(Context.ComputeDispatchInterface);
			const FGlobalDistanceFieldParameterData* GlobalDistanceFieldParameterData = static_cast<const FNiagaraGpuComputeDispatch*>(Context.ComputeDispatchInterface)->GetGlobalDistanceFieldParameters();//-BATCHERTODO:

			if (GlobalDistanceFieldParameterData)
			{
				GlobalDistanceFieldParameters.Set(RHICmdList, ComputeShaderRHI, *GlobalDistanceFieldParameterData);
			}
			else
			{
				GlobalDistanceFieldParameters.Set(RHICmdList, ComputeShaderRHI, FGlobalDistanceFieldParameterData());
			}
			
		}

#if RHI_RAYTRACING
		const bool HasRayTracingParametersBound = RayRequestsParam.IsUAVBound()
			|| IntersectionResultsParam.IsBound()
			|| RayTraceCountsParam.IsBound();

		if ((IsRayTracingEnabled() && GEnableGPUHWRTCollisions) || HasRayTracingParametersBound)
		{
			FNiagaraRayTracingHelper& RTHelper = Context.ComputeDispatchInterface->GetRayTracingHelper();
			const FNiagaraRayTraceDispatchInfo* DispatchInfo = nullptr;
			if (IsRayTracingEnabled() && GEnableGPUHWRTCollisions && QueryDI->MaxTracesPerParticle > 0)
			{
				DispatchInfo = &RTHelper.GetDispatch(QueryDI);
			}
			else
			{
				DispatchInfo = &RTHelper.GetDummyDispatch();
			}

			SetShaderValue(RHICmdList, ComputeShaderRHI, RayTracingEnabledParam, IsRayTracingEnabled() && GEnableGPUHWRTCollisions ? 1 : 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MaxRayTraceCountParam, DispatchInfo->MaxRays);

			if (RayRequestsParam.IsUAVBound())
			{
				check(DispatchInfo->RayRequests.IsValid());
				RHICmdList.SetUAVParameter(ComputeShaderRHI, RayRequestsParam.GetUAVIndex(), DispatchInfo->RayRequests.Buffer->UAV);
				SetShaderValue(RHICmdList, ComputeShaderRHI, RayRequestOffsetParam, DispatchInfo->RayRequests.Offset);
			}

			if (IntersectionResultsParam.IsBound())
			{
				check(DispatchInfo->LastFrameRayTraceIntersections.IsValid());

				SetSRVParameter(RHICmdList, ComputeShaderRHI, IntersectionResultsParam, DispatchInfo->LastFrameRayTraceIntersections.Buffer->SRV);
				SetShaderValue(RHICmdList, ComputeShaderRHI, IntersectionResultOffsetParam, DispatchInfo->LastFrameRayTraceIntersections.Offset);
			}

			if (RayTraceCountsParam.IsUAVBound())
			{
				check(DispatchInfo->RayCounts.IsValid());

				RHICmdList.SetUAVParameter(ComputeShaderRHI, RayTraceCountsParam.GetUAVIndex(), DispatchInfo->RayCounts.Buffer->UAV);
				SetShaderValue(RHICmdList, ComputeShaderRHI, RayTraceCountsOffsetParam, DispatchInfo->RayCounts.Offset);
			}
		}
		else
		{
			SetShaderValue(RHICmdList, ComputeShaderRHI, RayTracingEnabledParam, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MaxRayTraceCountParam, 0);
		}
#endif
	}

#if RHI_RAYTRACING
	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		if (RayRequestsParam.IsUAVBound())
		{
			RayRequestsParam.UnsetUAV(RHICmdList, ComputeShaderRHI);
		}

		if (RayTraceCountsParam.IsUAVBound())
		{
			RayTraceCountsParam.UnsetUAV(RHICmdList, ComputeShaderRHI);
		}
	}
#endif

private:
	LAYOUT_FIELD(FGlobalDistanceFieldParameters, GlobalDistanceFieldParameters);
	LAYOUT_FIELD(FShaderParameter, SystemLWCTileParam);

#if RHI_RAYTRACING
	LAYOUT_FIELD(FShaderParameter, RayTracingEnabledParam);
	LAYOUT_FIELD(FShaderParameter, MaxRayTraceCountParam);
	LAYOUT_FIELD(FRWShaderParameter, RayRequestsParam);
	LAYOUT_FIELD(FShaderParameter, RayRequestOffsetParam);
	LAYOUT_FIELD(FShaderResourceParameter, IntersectionResultsParam);
	LAYOUT_FIELD(FShaderParameter, IntersectionResultOffsetParam);
	LAYOUT_FIELD(FRWShaderParameter, RayTraceCountsParam);
	LAYOUT_FIELD(FShaderParameter, RayTraceCountsOffsetParam);
#endif
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_CollisionQuery);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceCollisionQuery, FNiagaraDataInterfaceParametersCS_CollisionQuery);

#undef LOCTEXT_NAMESPACE
