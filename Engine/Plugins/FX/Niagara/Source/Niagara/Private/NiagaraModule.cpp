// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraModule.h"
#include "Modules/ModuleManager.h"
#include "NiagaraTypes.h"
#include "NiagaraEvents.h"
#include "NiagaraSettings.h"
#include "NiagaraDataInterfaceCurlNoise.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "NiagaraWorldManager.h"
#include "VectorVM.h"
#include "NiagaraConstants.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraRenderer.h"
#include "Misc/CoreDelegates.h"
#include "NiagaraShaderModule.h"
#include "UObject/CoreRedirects.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "DeviceProfiles/DeviceProfile.h"

IMPLEMENT_MODULE(INiagaraModule, Niagara);

#define LOCTEXT_NAMESPACE "NiagaraModule"

float INiagaraModule::EngineGlobalSpawnCountScale = 1.0f;
float INiagaraModule::EngineGlobalSystemCountScale = 1.0f;
int32 INiagaraModule::EngineDetailLevel = 4;

int32 GEnableVerboseNiagaraChangeIdLogging = 0;
static FAutoConsoleVariableRef CVarEnableVerboseNiagaraChangeIdLogging(
	TEXT("fx.EnableVerboseNiagaraChangeIdLogging"),
	GEnableVerboseNiagaraChangeIdLogging,
	TEXT("If > 0 Verbose change id logging info will be printed. \n"),
	ECVF_Default
);

#if WITH_EDITORONLY_DATA
FNiagaraParameterStore INiagaraModule::FixedSystemInstanceParameters = FNiagaraParameterStore();
#endif

/**
Use Shader Stages CVar.
Enable the custom dispatch for multiple shader stages 
*/
static TAutoConsoleVariable<int32> CVarUseShaderStages(
	TEXT("fx.UseShaderStages"), 
	0, 
	TEXT("Enable or not the shader stages within Niagara (WIP feature only there for temporary testing)."),
	ECVF_Default);

/**
Detail Level CVar.
Effectively replaces the DetaiMode feature but allows for a rolling range of new hardware and emitters to target them.
TODO: Possible that this might be more broadly useful across the engine as a replacement for DetailMode so placing in "r." rather than "fx."
*/
static TAutoConsoleVariable<float> CVarDetailLevel(
	TEXT("r.DetailLevel"),
	4,
	TEXT("The detail level for use with Niagara.\n")
	TEXT("If this value does not fall within an Emitter's MinDetailLevel and MaxDetailLevel range, then it will be disabled. \n")
	TEXT("\n")
	TEXT("Default = 4"),
	ECVF_Scalability);

static TAutoConsoleVariable<float> CVarPruneEmittersOnCookByDetailLevel(
	TEXT("fx.NiagaraPruneEmittersOnCookByDetailLevel"),
	0,
	TEXT("Whether to eliminate all emitters that don't match the detail level.\n")
	TEXT("This will only work if scalability settings affecting detail level can not be changed at runtime (depends on platform).\n"),
	ECVF_ReadOnly);

static FAutoConsoleVariableRef CVarNiaraGlobalSpawnCountScale(
	TEXT("fx.NiagaraGlobalSpawnCountScale"),
	INiagaraModule::EngineGlobalSpawnCountScale,
	TEXT("A global scale on spawn counts in Niagara. \n"),
	ECVF_Scalability
);

static FAutoConsoleVariableRef CVarNiaraGlobalSystemCountScale(
	TEXT("fx.NiagaraGlobalSystemCountScale"),
	INiagaraModule::EngineGlobalSystemCountScale,
	TEXT("A global scale on system count thresholds for culling in Niagara. \n"),
	ECVF_Scalability
);

FNiagaraVariable INiagaraModule::Engine_DeltaTime;
FNiagaraVariable INiagaraModule::Engine_InvDeltaTime;
FNiagaraVariable INiagaraModule::Engine_Time;
FNiagaraVariable INiagaraModule::Engine_RealTime;

FNiagaraVariable INiagaraModule::Engine_Owner_Position;
FNiagaraVariable INiagaraModule::Engine_Owner_Velocity;
FNiagaraVariable INiagaraModule::Engine_Owner_XAxis;
FNiagaraVariable INiagaraModule::Engine_Owner_YAxis;
FNiagaraVariable INiagaraModule::Engine_Owner_ZAxis;
FNiagaraVariable INiagaraModule::Engine_Owner_Scale;
FNiagaraVariable INiagaraModule::Engine_Owner_Rotation;

FNiagaraVariable INiagaraModule::Engine_Owner_SystemLocalToWorld;
FNiagaraVariable INiagaraModule::Engine_Owner_SystemWorldToLocal;
FNiagaraVariable INiagaraModule::Engine_Owner_SystemLocalToWorldTransposed;
FNiagaraVariable INiagaraModule::Engine_Owner_SystemWorldToLocalTransposed;
FNiagaraVariable INiagaraModule::Engine_Owner_SystemLocalToWorldNoScale;
FNiagaraVariable INiagaraModule::Engine_Owner_SystemWorldToLocalNoScale;

FNiagaraVariable INiagaraModule::Engine_Owner_TimeSinceRendered;
FNiagaraVariable INiagaraModule::Engine_Owner_LODDistance;
FNiagaraVariable INiagaraModule::Engine_Owner_LODDistanceFraction;

FNiagaraVariable INiagaraModule::Engine_Owner_ExecutionState;

FNiagaraVariable INiagaraModule::Engine_ExecutionCount;
FNiagaraVariable INiagaraModule::Engine_Emitter_NumParticles;
FNiagaraVariable INiagaraModule::Engine_Emitter_TotalSpawnedParticles;
FNiagaraVariable INiagaraModule::Engine_Emitter_SpawnCountScale;
FNiagaraVariable INiagaraModule::Engine_System_TickCount;
FNiagaraVariable INiagaraModule::Engine_System_NumEmittersAlive;
FNiagaraVariable INiagaraModule::Engine_System_NumEmitters;
FNiagaraVariable INiagaraModule::Engine_NumSystemInstances;

FNiagaraVariable INiagaraModule::Engine_GlobalSpawnCountScale;
FNiagaraVariable INiagaraModule::Engine_GlobalSystemScale;

FNiagaraVariable INiagaraModule::Engine_System_Age;

FNiagaraVariable INiagaraModule::Emitter_Age;
FNiagaraVariable INiagaraModule::Emitter_LocalSpace;
FNiagaraVariable INiagaraModule::Emitter_Determinism;
FNiagaraVariable INiagaraModule::Emitter_OverrideGlobalSpawnCountScale;
FNiagaraVariable INiagaraModule::Emitter_SimulationTarget;
FNiagaraVariable INiagaraModule::Emitter_RandomSeed;
FNiagaraVariable INiagaraModule::Emitter_SpawnRate;
FNiagaraVariable INiagaraModule::Emitter_SpawnInterval;
FNiagaraVariable INiagaraModule::Emitter_InterpSpawnStartDt;
FNiagaraVariable INiagaraModule::Emitter_SpawnGroup;

FNiagaraVariable INiagaraModule::Particles_UniqueID;
FNiagaraVariable INiagaraModule::Particles_ID;
FNiagaraVariable INiagaraModule::Particles_Position;
FNiagaraVariable INiagaraModule::Particles_Velocity;
FNiagaraVariable INiagaraModule::Particles_Color;
FNiagaraVariable INiagaraModule::Particles_SpriteRotation;
FNiagaraVariable INiagaraModule::Particles_NormalizedAge;
FNiagaraVariable INiagaraModule::Particles_SpriteSize;
FNiagaraVariable INiagaraModule::Particles_SpriteFacing;
FNiagaraVariable INiagaraModule::Particles_SpriteAlignment;
FNiagaraVariable INiagaraModule::Particles_SubImageIndex;
FNiagaraVariable INiagaraModule::Particles_DynamicMaterialParameter;
FNiagaraVariable INiagaraModule::Particles_DynamicMaterialParameter1;
FNiagaraVariable INiagaraModule::Particles_DynamicMaterialParameter2;
FNiagaraVariable INiagaraModule::Particles_DynamicMaterialParameter3;
FNiagaraVariable INiagaraModule::Particles_Scale;
FNiagaraVariable INiagaraModule::Particles_Lifetime;
FNiagaraVariable INiagaraModule::Particles_MeshOrientation;
FNiagaraVariable INiagaraModule::Particles_UVScale;
FNiagaraVariable INiagaraModule::Particles_CameraOffset;
FNiagaraVariable INiagaraModule::Particles_MaterialRandom;
FNiagaraVariable INiagaraModule::Particles_LightRadius;
FNiagaraVariable INiagaraModule::Particles_LightExponent;
FNiagaraVariable INiagaraModule::Particles_LightEnabled;
FNiagaraVariable INiagaraModule::Particles_LightVolumetricScattering;
FNiagaraVariable INiagaraModule::Particles_RibbonID;
FNiagaraVariable INiagaraModule::Particles_RibbonWidth;
FNiagaraVariable INiagaraModule::Particles_RibbonTwist;
FNiagaraVariable INiagaraModule::Particles_RibbonFacing;
FNiagaraVariable INiagaraModule::Particles_RibbonLinkOrder;
FNiagaraVariable INiagaraModule::ScriptUsage;
FNiagaraVariable INiagaraModule::DataInstance_Alive;
FNiagaraVariable INiagaraModule::Translator_BeginDefaults;

void INiagaraModule::StartupModule()
{
	VectorVM::Init();
	FNiagaraTypeDefinition::Init();
	FNiagaraViewDataMgr::Init();

	FNiagaraWorldManager::OnStartup();

#if WITH_EDITOR	
	// Loading uncooked data in a game environment, we still need to get some functionality from the NiagaraEditor module.
	// This includes the ability to compile scripts and load WITH_EDITOR_ONLY data.
	// Note that when loading with the Editor, the NiagaraEditor module is loaded based on the plugin description.
	FModuleManager::Get().LoadModule(TEXT("NiagaraEditor"));
#endif

	CVarDetailLevel.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate::CreateRaw(this, &INiagaraModule::OnChangeDetailLevel));
	EngineDetailLevel = CVarDetailLevel.GetValueOnGameThread();

	//Init commonly used FNiagaraVariables

	Engine_DeltaTime = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.DeltaTime"));
	Engine_InvDeltaTime = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.InverseDeltaTime"));
	
	Engine_Time = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.Time"));
	Engine_RealTime = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.RealTime"));

	Engine_Owner_Position = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Engine.Owner.Position"));
	Engine_Owner_Velocity = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Engine.Owner.Velocity"));
	Engine_Owner_XAxis = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Engine.Owner.SystemXAxis"));
	Engine_Owner_YAxis = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Engine.Owner.SystemYAxis"));
	Engine_Owner_ZAxis = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Engine.Owner.SystemZAxis"));
	Engine_Owner_Scale = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Engine.Owner.Scale"));
	Engine_Owner_Rotation = FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Engine.Owner.Rotation"));

	Engine_Owner_SystemLocalToWorld = FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Engine.Owner.SystemLocalToWorld"));
	Engine_Owner_SystemWorldToLocal = FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Engine.Owner.SystemWorldToLocal"));
	Engine_Owner_SystemLocalToWorldTransposed = FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Engine.Owner.SystemLocalToWorldTransposed"));
	Engine_Owner_SystemWorldToLocalTransposed = FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Engine.Owner.SystemWorldToLocalTransposed"));
	Engine_Owner_SystemLocalToWorldNoScale = FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Engine.Owner.SystemLocalToWorldNoScale"));
	Engine_Owner_SystemWorldToLocalNoScale = FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Engine.Owner.SystemWorldToLocalNoScale"));

	Engine_Owner_TimeSinceRendered = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.Owner.TimeSinceRendered"));
	Engine_Owner_LODDistance = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.Owner.LODDistance"));
	Engine_Owner_LODDistanceFraction = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.Owner.LODDistanceFraction"));

	Engine_Owner_ExecutionState = FNiagaraVariable(FNiagaraTypeDefinition::GetExecutionStateEnum(), TEXT("Engine.Owner.ExecutionState"));
	
	Engine_ExecutionCount = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Engine.ExecutionCount"));
	Engine_Emitter_NumParticles = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Engine.Emitter.NumParticles"));
	Engine_Emitter_TotalSpawnedParticles = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Engine.Emitter.TotalSpawnedParticles"));
	Engine_Emitter_SpawnCountScale = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.Emitter.SpawnCountScale"));
	Engine_System_TickCount = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Engine.System.TickCount"));
	Engine_System_NumEmittersAlive = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Engine.System.NumEmittersAlive"));
	Engine_System_NumEmitters = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Engine.System.NumEmitters"));
	Engine_NumSystemInstances = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Engine.NumSystemInstances"));

	Engine_GlobalSpawnCountScale = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.GlobalSpawnCountScale"));
	Engine_GlobalSystemScale = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.GlobalSystemCountScale"));

	Engine_System_Age = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Engine.System.Age"));
	Emitter_Age = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.Age"));
	Emitter_LocalSpace = FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Emitter.LocalSpace"));
	Emitter_RandomSeed = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Emitter.RandomSeed"));
	Emitter_Determinism = FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Emitter.Determinism"));
	Emitter_OverrideGlobalSpawnCountScale = FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Emitter.OverrideGlobalSpawnCountScale"));
	Emitter_SimulationTarget = FNiagaraVariable(FNiagaraTypeDefinition::GetSimulationTargetEnum(), TEXT("Emitter.SimulationTarget"));
	Emitter_SpawnRate = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.SpawnRate"));
	Emitter_SpawnInterval = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.SpawnInterval"));
	Emitter_InterpSpawnStartDt = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.InterpSpawnStartDt"));
	Emitter_SpawnGroup = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Emitter.SpawnGroup"));

	Particles_UniqueID = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particles.UniqueID"));
	Particles_ID = FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particles.ID"));
	Particles_Position = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particles.Position"));
	Particles_Velocity = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particles.Velocity"));
	Particles_Color = FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Particles.Color"));
	Particles_SpriteRotation = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.SpriteRotation"));
	Particles_NormalizedAge = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.NormalizedAge"));
	Particles_SpriteSize = FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Particles.SpriteSize"));
	Particles_SpriteFacing = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particles.SpriteFacing"));
	Particles_SpriteAlignment = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particles.SpriteAlignment"));
	Particles_SubImageIndex = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.SubImageIndex"));
	Particles_DynamicMaterialParameter = FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Particles.DynamicMaterialParameter"));
	Particles_DynamicMaterialParameter1 = FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Particles.DynamicMaterialParameter1"));
	Particles_DynamicMaterialParameter2 = FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Particles.DynamicMaterialParameter2"));
	Particles_DynamicMaterialParameter3 = FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Particles.DynamicMaterialParameter3"));
	Particles_Scale = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particles.Scale"));
	Particles_Lifetime = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.Lifetime"));
	Particles_MeshOrientation = FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Particles.MeshOrientation"));
	Particles_UVScale = FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Particles.UVScale"));
	Particles_CameraOffset = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.CameraOffset"));
	Particles_MaterialRandom = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.MaterialRandom"));
	Particles_LightRadius = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.LightRadius"));
	Particles_LightExponent = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.LightExponent"));
	Particles_LightEnabled = FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Particles.LightEnabled"));
	Particles_LightVolumetricScattering = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.LightVolumetricScattering"));
	Particles_RibbonID = FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particles.RibbonID"));
	Particles_RibbonWidth = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.RibbonWidth"));
	Particles_RibbonTwist = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.RibbonTwist"));
	Particles_RibbonFacing = FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particles.RibbonFacing"));
	Particles_RibbonLinkOrder = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.RibbonLinkOrder"));

	ScriptUsage = FNiagaraVariable(FNiagaraTypeDefinition::GetScriptUsageEnum(), TEXT("Script.Usage"));
	DataInstance_Alive = FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("DataInstance.Alive"));

	Translator_BeginDefaults = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Begin Defaults"));

	FNiagaraConstants::Init();
	UNiagaraLightRendererProperties::InitCDOPropertiesAfterModuleStartup();
	UNiagaraSpriteRendererProperties::InitCDOPropertiesAfterModuleStartup();
	UNiagaraRibbonRendererProperties::InitCDOPropertiesAfterModuleStartup();
	UNiagaraMeshRendererProperties::InitCDOPropertiesAfterModuleStartup();

	// Register the data interface CDO finder with teh shader module..
	INiagaraShaderModule& NiagaraShaderModule = FModuleManager::LoadModuleChecked<INiagaraShaderModule>("NiagaraShader");
	NiagaraShaderModule.SetOnRequestDefaultDataInterfaceHandler(INiagaraShaderModule::FOnRequestDefaultDataInterface::CreateLambda([](const FString& DIClassName) -> UNiagaraDataInterfaceBase*
	{
		return FNiagaraTypeRegistry::GetDefaultDataInterfaceByName(DIClassName);
	}));

	FFXSystemInterface::RegisterCustomFXSystem(NiagaraEmitterInstanceBatcher::Name, FCreateCustomFXSystemDelegate::CreateLambda([](ERHIFeatureLevel::Type InFeatureLevel, EShaderPlatform InShaderPlatform) -> FFXSystemInterface*
	{
		return new NiagaraEmitterInstanceBatcher(InFeatureLevel, InShaderPlatform);
	}));

#if WITH_EDITORONLY_DATA
	InitFixedSystemInstanceParameterStore();
#endif
}

void INiagaraModule::ShutdownRenderingResources()
{
	FFXSystemInterface::UnregisterCustomFXSystem(NiagaraEmitterInstanceBatcher::Name);

	FNiagaraViewDataMgr::Shutdown();
}

void INiagaraModule::ShutdownModule()
{
	FNiagaraWorldManager::OnShutdown();

	// Clear out the handler when shutting down..
	INiagaraShaderModule& NiagaraShaderModule = FModuleManager::LoadModuleChecked<INiagaraShaderModule>("NiagaraShader");
	NiagaraShaderModule.ResetOnRequestDefaultDataInterfaceHandler();

	CVarDetailLevel.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate());
	ShutdownRenderingResources();
}

#if WITH_EDITOR
const INiagaraMergeManager& INiagaraModule::GetMergeManager() const
{
	checkf(MergeManager.IsValid(), TEXT("Merge manager was never registered, or was unregistered."));
	return *MergeManager.Get();
}

void INiagaraModule::RegisterMergeManager(TSharedRef<INiagaraMergeManager> InMergeManager)
{
	checkf(MergeManager.IsValid() == false, TEXT("Only one merge manager can be registered at a time."));
	MergeManager = InMergeManager;
}

void INiagaraModule::UnregisterMergeManager(TSharedRef<INiagaraMergeManager> InMergeManager)
{
	checkf(MergeManager.IsValid(), TEXT("MergeManager is not registered"));
	checkf(MergeManager == InMergeManager, TEXT("Can only unregister the merge manager which was previously registered."));
	MergeManager.Reset();
}

const INiagaraEditorOnlyDataUtilities& INiagaraModule::GetEditorOnlyDataUtilities() const
{
	checkf(EditorOnlyDataUtilities.IsValid(), TEXT("Editor only data utilities object was never registered, or was unregistered."));
	return *EditorOnlyDataUtilities.Get();
}

void INiagaraModule::RegisterEditorOnlyDataUtilities(TSharedRef<INiagaraEditorOnlyDataUtilities> InEditorOnlyDataUtilities)
{
	checkf(EditorOnlyDataUtilities.IsValid() == false, TEXT("Only one editor only data utilities object can be registered at a time."));
	EditorOnlyDataUtilities = InEditorOnlyDataUtilities;
}

void INiagaraModule::UnregisterEditorOnlyDataUtilities(TSharedRef<INiagaraEditorOnlyDataUtilities> InEditorOnlyDataUtilities)
{
	checkf(EditorOnlyDataUtilities.IsValid(), TEXT("Editor only data utilities object is not registered"));
	checkf(EditorOnlyDataUtilities == InEditorOnlyDataUtilities, TEXT("Can only unregister the editor only data utilities object which was previously registered."));
	EditorOnlyDataUtilities.Reset();
}

TSharedPtr<FNiagaraVMExecutableData> INiagaraModule::CompileScript(const FNiagaraCompileRequestDataBase* InCompileData, const FNiagaraCompileOptions& InCompileOptions)
{
	checkf(ScriptCompilerDelegate.IsBound(), TEXT("Create default script compiler delegate not bound."));
	return ScriptCompilerDelegate.Execute(InCompileData, InCompileOptions);
}

FDelegateHandle INiagaraModule::RegisterScriptCompiler(FScriptCompiler ScriptCompiler)
{
	checkf(ScriptCompilerDelegate.IsBound() == false, TEXT("Only one handler is allowed for the ScriptCompiler delegate"));
	ScriptCompilerDelegate = ScriptCompiler;
	return ScriptCompilerDelegate.GetHandle();
}

void INiagaraModule::UnregisterScriptCompiler(FDelegateHandle DelegateHandle)
{
	checkf(ScriptCompilerDelegate.IsBound(), TEXT("ScriptCompiler is not registered"));
	checkf(ScriptCompilerDelegate.GetHandle() == DelegateHandle, TEXT("Can only unregister the ScriptCompiler delegate with the handle it was registered with."));
	ScriptCompilerDelegate.Unbind();
}


TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> INiagaraModule::Precompile(UObject* Obj)
{
	checkf(ObjectPrecompilerDelegate.IsBound(), TEXT("ObjectPrecompiler delegate not bound."));
	return ObjectPrecompilerDelegate.Execute(Obj);
}

FDelegateHandle INiagaraModule::RegisterPrecompiler(FOnPrecompile PreCompiler)
{
	checkf(ObjectPrecompilerDelegate.IsBound() == false, TEXT("Only one handler is allowed for the ObjectPrecompiler delegate"));
	ObjectPrecompilerDelegate = PreCompiler;
	return ObjectPrecompilerDelegate.GetHandle();
}

void INiagaraModule::UnregisterPrecompiler(FDelegateHandle DelegateHandle)
{
	checkf(ObjectPrecompilerDelegate.IsBound(), TEXT("ObjectPrecompiler is not registered"));
	checkf(ObjectPrecompilerDelegate.GetHandle() == DelegateHandle, TEXT("Can only unregister the ObjectPrecompiler delegate with the handle it was registered with."));
	ObjectPrecompilerDelegate.Unbind();
}

#endif


bool INiagaraModule::IsTargetPlatformIncludedInLevelRangeForCook(const ITargetPlatform* InTargetPlatform, const UNiagaraEmitter* InEmitter)
{
#if WITH_EDITORONLY_DATA
	if (UDeviceProfile* DeviceProfile = UDeviceProfileManager::Get().FindProfile(InTargetPlatform->IniPlatformName()))
	{
		// get local scalability CVars that could cull this actor
		int32 CVarCullBasedOnDetailLevel;
		if (DeviceProfile->GetConsolidatedCVarValue(TEXT("fx.NiagaraPruneEmittersOnCookByDetailLevel"), CVarCullBasedOnDetailLevel) && CVarCullBasedOnDetailLevel == 1)
		{
			int32 CVarDetailLevelFoundValue;
			if (InEmitter && DeviceProfile->GetConsolidatedCVarValue(TEXT("r.DetailLevel"), CVarDetailLevelFoundValue))
			{
				// Check emitter's detail level range contains the platform's level
				// If e.g. the emitter's detail level range is between 0 and 2  and the platform detail is 3 only,
				// then we should cull it.
				if (InEmitter->IsAllowedByDetailLevel(CVarDetailLevelFoundValue))
				{
					return true;
				}
				return false;
			}
		}
	}
#endif
	return true;
}

#if WITH_EDITORONLY_DATA
void INiagaraModule::InitFixedSystemInstanceParameterStore()
{
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_POSITION, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_ROTATION, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_SCALE, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_VELOCITY, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_X_AXIS, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_Y_AXIS, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_Z_AXIS, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_LOCAL_TO_WORLD, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_WORLD_TO_LOCAL, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_LOCAL_TO_WORLD_TRANSPOSED, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_WORLD_TO_LOCAL_TRANSPOSED, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_LOCAL_TO_WORLD_NO_SCALE, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_WORLD_TO_LOCAL_NO_SCALE, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_DELTA_TIME, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_TIME, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_REAL_TIME, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_INV_DELTA_TIME, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_TIME_SINCE_RENDERED, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_EXECUTION_STATE, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_LOD_DISTANCE, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_LOD_DISTANCE_FRACTION, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_SYSTEM_NUM_EMITTERS, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_SYSTEM_NUM_EMITTERS_ALIVE, true, false);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_SYSTEM_AGE);
	FixedSystemInstanceParameters.AddParameter(SYS_PARAM_ENGINE_SYSTEM_TICK_COUNT);
}
#endif

void INiagaraModule::OnChangeDetailLevel(class IConsoleVariable* CVar)
{
	int32 NewDetailLevel = CVar->GetInt();
	if (EngineDetailLevel != NewDetailLevel)
	{
		EngineDetailLevel = NewDetailLevel;

		for (TObjectIterator<UNiagaraSystem> It; It; ++It)
		{
			UNiagaraSystem* System = *It;
			check(System);
			System->OnDetailLevelChanges(NewDetailLevel);
		}

		// If the detail level has changed we have to reset all systems,
		// and only activate ones that were previously active
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			UNiagaraComponent* Comp = *It;
			check(Comp);

			const bool bWasActive = Comp->IsActive();
			Comp->DestroyInstance();
			if ( bWasActive )
			{
				Comp->Activate(true);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////

UScriptStruct* FNiagaraTypeDefinition::ParameterMapStruct;
UScriptStruct* FNiagaraTypeDefinition::IDStruct;
UScriptStruct* FNiagaraTypeDefinition::NumericStruct;
UScriptStruct* FNiagaraTypeDefinition::FloatStruct;
UScriptStruct* FNiagaraTypeDefinition::BoolStruct;
UScriptStruct* FNiagaraTypeDefinition::IntStruct;
UScriptStruct* FNiagaraTypeDefinition::Matrix4Struct;
UScriptStruct* FNiagaraTypeDefinition::Vec4Struct;
UScriptStruct* FNiagaraTypeDefinition::Vec3Struct;
UScriptStruct* FNiagaraTypeDefinition::Vec2Struct;
UScriptStruct* FNiagaraTypeDefinition::ColorStruct;
UScriptStruct* FNiagaraTypeDefinition::QuatStruct;

UClass* FNiagaraTypeDefinition::UObjectClass;
UClass* FNiagaraTypeDefinition::UMaterialClass;

UEnum* FNiagaraTypeDefinition::ExecutionStateEnum;
UEnum* FNiagaraTypeDefinition::SimulationTargetEnum;
UEnum* FNiagaraTypeDefinition::ExecutionStateSourceEnum;
UEnum* FNiagaraTypeDefinition::ScriptUsageEnum;

FNiagaraTypeDefinition FNiagaraTypeDefinition::ParameterMapDef;
FNiagaraTypeDefinition FNiagaraTypeDefinition::IDDef;
FNiagaraTypeDefinition FNiagaraTypeDefinition::NumericDef;
FNiagaraTypeDefinition FNiagaraTypeDefinition::FloatDef;
FNiagaraTypeDefinition FNiagaraTypeDefinition::BoolDef;
FNiagaraTypeDefinition FNiagaraTypeDefinition::IntDef;
FNiagaraTypeDefinition FNiagaraTypeDefinition::Matrix4Def;
FNiagaraTypeDefinition FNiagaraTypeDefinition::Vec4Def;
FNiagaraTypeDefinition FNiagaraTypeDefinition::Vec3Def;
FNiagaraTypeDefinition FNiagaraTypeDefinition::Vec2Def;
FNiagaraTypeDefinition FNiagaraTypeDefinition::ColorDef;
FNiagaraTypeDefinition FNiagaraTypeDefinition::QuatDef;

FNiagaraTypeDefinition FNiagaraTypeDefinition::UObjectDef;
FNiagaraTypeDefinition FNiagaraTypeDefinition::UMaterialDef;

TSet<UScriptStruct*> FNiagaraTypeDefinition::NumericStructs;
TArray<FNiagaraTypeDefinition> FNiagaraTypeDefinition::OrderedNumericTypes;

TSet<UScriptStruct*> FNiagaraTypeDefinition::ScalarStructs;

TSet<UStruct*> FNiagaraTypeDefinition::FloatStructs;
TSet<UStruct*> FNiagaraTypeDefinition::IntStructs;
TSet<UStruct*> FNiagaraTypeDefinition::BoolStructs;

FNiagaraTypeDefinition FNiagaraTypeDefinition::CollisionEventDef;


TArray<FNiagaraTypeDefinition> FNiagaraTypeRegistry::RegisteredTypes;
TArray<FNiagaraTypeDefinition> FNiagaraTypeRegistry::RegisteredParamTypes;
TArray<FNiagaraTypeDefinition> FNiagaraTypeRegistry::RegisteredPayloadTypes;
TArray<FNiagaraTypeDefinition> FNiagaraTypeRegistry::RegisteredUserDefinedTypes;
TArray<FNiagaraTypeDefinition> FNiagaraTypeRegistry::RegisteredNumericTypes;


bool FNiagaraTypeDefinition::IsDataInterface()const
{
	return GetStruct()->IsChildOf(UNiagaraDataInterface::StaticClass());
}

void FNiagaraTypeDefinition::Init()
{
	static auto* CoreUObjectPkg = FindObjectChecked<UPackage>(nullptr, TEXT("/Script/CoreUObject"));
	static auto* NiagaraPkg = FindObjectChecked<UPackage>(nullptr, TEXT("/Script/Niagara"));
	FNiagaraTypeDefinition::ParameterMapStruct = FindObjectChecked<UScriptStruct>(NiagaraPkg, TEXT("NiagaraParameterMap"));
	FNiagaraTypeDefinition::IDStruct = FindObjectChecked<UScriptStruct>(NiagaraPkg, TEXT("NiagaraID"));
	FNiagaraTypeDefinition::NumericStruct = FindObjectChecked<UScriptStruct>(NiagaraPkg, TEXT("NiagaraNumeric"));
	FNiagaraTypeDefinition::FloatStruct = FindObjectChecked<UScriptStruct>(NiagaraPkg, TEXT("NiagaraFloat"));
	FNiagaraTypeDefinition::BoolStruct = FindObjectChecked<UScriptStruct>(NiagaraPkg, TEXT("NiagaraBool"));
	FNiagaraTypeDefinition::IntStruct = FindObjectChecked<UScriptStruct>(NiagaraPkg, TEXT("NiagaraInt32"));
	FNiagaraTypeDefinition::Matrix4Struct = FindObjectChecked<UScriptStruct>(NiagaraPkg, TEXT("NiagaraMatrix"));

	FNiagaraTypeDefinition::Vec2Struct = FindObjectChecked<UScriptStruct>(CoreUObjectPkg, TEXT("Vector2D"));
	FNiagaraTypeDefinition::Vec3Struct = FindObjectChecked<UScriptStruct>(CoreUObjectPkg, TEXT("Vector"));
	FNiagaraTypeDefinition::Vec4Struct = FindObjectChecked<UScriptStruct>(CoreUObjectPkg, TEXT("Vector4"));
	FNiagaraTypeDefinition::ColorStruct = FindObjectChecked<UScriptStruct>(CoreUObjectPkg, TEXT("LinearColor"));
	FNiagaraTypeDefinition::QuatStruct = FindObjectChecked<UScriptStruct>(CoreUObjectPkg, TEXT("Quat"));

	FNiagaraTypeDefinition::UObjectClass = UObject::StaticClass();
	FNiagaraTypeDefinition::UMaterialClass = UMaterialInterface::StaticClass();
	
	ParameterMapDef = FNiagaraTypeDefinition(ParameterMapStruct);
	IDDef = FNiagaraTypeDefinition(IDStruct);
	NumericDef = FNiagaraTypeDefinition(NumericStruct);
	FloatDef = FNiagaraTypeDefinition(FloatStruct);
	BoolDef = FNiagaraTypeDefinition(BoolStruct);
	IntDef = FNiagaraTypeDefinition(IntStruct);
	Vec2Def = FNiagaraTypeDefinition(Vec2Struct);
	Vec3Def = FNiagaraTypeDefinition(Vec3Struct);
	Vec4Def = FNiagaraTypeDefinition(Vec4Struct);
	ColorDef = FNiagaraTypeDefinition(ColorStruct);
	QuatDef = FNiagaraTypeDefinition(QuatStruct);
	Matrix4Def = FNiagaraTypeDefinition(Matrix4Struct);

	UObjectDef = FNiagaraTypeDefinition(UObjectClass);
	UMaterialDef = FNiagaraTypeDefinition(UMaterialClass);

	CollisionEventDef = FNiagaraTypeDefinition(FNiagaraCollisionEventPayload::StaticStruct());
	NumericStructs.Add(NumericStruct);
	NumericStructs.Add(FloatStruct);
	NumericStructs.Add(IntStruct);
	NumericStructs.Add(Vec2Struct);
	NumericStructs.Add(Vec3Struct);
	NumericStructs.Add(Vec4Struct);
	NumericStructs.Add(ColorStruct);
	NumericStructs.Add(QuatStruct);
	//Make matrix a numeric type?

	FloatStructs.Add(FloatStruct);
	FloatStructs.Add(Vec2Struct);
	FloatStructs.Add(Vec3Struct);
	FloatStructs.Add(Vec4Struct);
	//FloatStructs.Add(Matrix4Struct)??
	FloatStructs.Add(ColorStruct);
	FloatStructs.Add(QuatStruct);

	IntStructs.Add(IntStruct);

	BoolStructs.Add(BoolStruct);

	OrderedNumericTypes.Add(IntStruct);
	OrderedNumericTypes.Add(FloatStruct);
	OrderedNumericTypes.Add(Vec2Struct);
	OrderedNumericTypes.Add(Vec3Struct);
	OrderedNumericTypes.Add(Vec4Struct);
	OrderedNumericTypes.Add(ColorStruct);
	OrderedNumericTypes.Add(QuatStruct);

	ScalarStructs.Add(BoolStruct);
	ScalarStructs.Add(IntStruct);
	ScalarStructs.Add(FloatStruct);

	ExecutionStateEnum = StaticEnum<ENiagaraExecutionState>();
	ExecutionStateSourceEnum = StaticEnum<ENiagaraExecutionStateSource>();
	SimulationTargetEnum = StaticEnum<ENiagaraSimTarget>();
	ScriptUsageEnum = StaticEnum<ENiagaraScriptUsage>();
	
	RecreateUserDefinedTypeRegistry();
}

bool FNiagaraTypeDefinition::IsValidNumericInput(const FNiagaraTypeDefinition& TypeDef)
{
	if (NumericStructs.Contains(TypeDef.GetScriptStruct()))
	{
		return true;
	}
	return false;
}

void FNiagaraTypeDefinition::RecreateUserDefinedTypeRegistry()
{
	static auto* CoreUObjectPkg = FindObjectChecked<UPackage>(nullptr, TEXT("/Script/CoreUObject"));
	static auto* NiagaraPkg = FindObjectChecked<UPackage>(nullptr, TEXT("/Script/Niagara"));

	FNiagaraTypeRegistry::ClearUserDefinedRegistry();

	FNiagaraTypeRegistry::Register(CollisionEventDef, false, true, false);

	FNiagaraTypeRegistry::Register(ParameterMapDef, true, false, false);
	FNiagaraTypeRegistry::Register(IDDef, true, true, false);
	FNiagaraTypeRegistry::Register(NumericDef, true, false, false);
	FNiagaraTypeRegistry::Register(FloatDef, true, true, false);
	FNiagaraTypeRegistry::Register(IntDef, true, true, false);
	FNiagaraTypeRegistry::Register(BoolDef, true, true, false);
	FNiagaraTypeRegistry::Register(Vec2Def, true, true, false);
	FNiagaraTypeRegistry::Register(Vec3Def, true, true, false);
	FNiagaraTypeRegistry::Register(Vec4Def, true, true, false);
	FNiagaraTypeRegistry::Register(ColorDef, true, true, false);
	FNiagaraTypeRegistry::Register(QuatDef, true, true, false);
	FNiagaraTypeRegistry::Register(Matrix4Def, true, false, false);

	FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(ExecutionStateEnum), true, true, false);
	FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(ExecutionStateSourceEnum), true, true, false);

	UScriptStruct* TestStruct = FindObjectChecked<UScriptStruct>(NiagaraPkg, TEXT("NiagaraTestStruct"));
	FNiagaraTypeDefinition TestDefinition(TestStruct);
	FNiagaraTypeRegistry::Register(TestDefinition, true, false, false);

	UScriptStruct* SpawnInfoStruct = FindObjectChecked<UScriptStruct>(NiagaraPkg, TEXT("NiagaraSpawnInfo"));
	FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(SpawnInfoStruct), true, false, false);

	FNiagaraTypeRegistry::Register(UObjectDef, true, false, false);
	FNiagaraTypeRegistry::Register(UMaterialDef, true, false, false);

	const UNiagaraSettings* Settings = GetDefault<UNiagaraSettings>();
	check(Settings);
	TArray<FSoftObjectPath> TotalStructAssets;
	for (FSoftObjectPath AssetRef : Settings->AdditionalParameterTypes)
	{
		TotalStructAssets.AddUnique(AssetRef);
	}

	for (FSoftObjectPath AssetRef : Settings->AdditionalPayloadTypes)
	{
		TotalStructAssets.AddUnique(AssetRef);
	}

	for (FSoftObjectPath AssetRef : TotalStructAssets)
	{
		FName AssetRefPathNamePreResolve = AssetRef.GetAssetPathName();

		UObject* Obj = AssetRef.ResolveObject();
		if (Obj == nullptr)
		{
			Obj = AssetRef.TryLoad();
		}

		if (Obj != nullptr)
		{
			const FSoftObjectPath* ParamRefFound = Settings->AdditionalParameterTypes.FindByPredicate([&](const FSoftObjectPath& Ref) { return Ref.ToString() == AssetRef.ToString(); });
			const FSoftObjectPath* PayloadRefFound = Settings->AdditionalPayloadTypes.FindByPredicate([&](const FSoftObjectPath& Ref) { return Ref.ToString() == AssetRef.ToString(); });
			UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Obj);
			if (ScriptStruct != nullptr)
			{
				FNiagaraTypeRegistry::Register(ScriptStruct, ParamRefFound != nullptr, PayloadRefFound != nullptr, true);
			}
			if (Obj->GetPathName() != AssetRefPathNamePreResolve.ToString())
			{
				UE_LOG(LogNiagara, Warning, TEXT("Additional parameter/payload enum has moved from where it was in settings (this may cause errors at runtime): Was: \"%s\" Now: \"%s\""), *AssetRefPathNamePreResolve.ToString(), *Obj->GetPathName());
			}

		}
		else
		{
			UE_LOG(LogNiagara, Warning, TEXT("Could not find additional parameter/payload type: %s"), *AssetRef.ToString());
		}
	}


	for (FSoftObjectPath AssetRef : Settings->AdditionalParameterEnums)
	{
		FName AssetRefPathNamePreResolve = AssetRef.GetAssetPathName();
		UObject* Obj = AssetRef.ResolveObject();
		if (Obj == nullptr)
		{
			Obj = AssetRef.TryLoad();
		}

		if (Obj != nullptr)
		{
			const FSoftObjectPath* ParamRefFound = Settings->AdditionalParameterEnums.FindByPredicate([&](const FSoftObjectPath& Ref) { return Ref.ToString() == AssetRef.ToString(); });
			const FSoftObjectPath* PayloadRefFound = nullptr;
			UEnum* Enum = Cast<UEnum>(Obj);
			if (Enum != nullptr)
			{
				FNiagaraTypeRegistry::Register(Enum, ParamRefFound != nullptr, PayloadRefFound != nullptr, true);
			}

			if (Obj->GetPathName() != AssetRefPathNamePreResolve.ToString())
			{
				UE_LOG(LogNiagara, Warning, TEXT("Additional parameter/payload enum has moved from where it was in settings (this may cause errors at runtime): Was: \"%s\" Now: \"%s\""), *AssetRefPathNamePreResolve.ToString(), *Obj->GetPathName());
			}
		}
		else
		{
			UE_LOG(LogNiagara, Warning, TEXT("Could not find additional parameter/payload enum: %s"), *AssetRef.ToString());
		}
	}

	FNiagaraTypeRegistry::Register(FNiagaraRandInfo::StaticStruct(), true, true, true);

	FNiagaraTypeRegistry::Register(StaticEnum<ENiagaraLegacyTrailWidthMode>(), true, true, false);
}

bool FNiagaraTypeDefinition::IsScalarDefinition(const FNiagaraTypeDefinition& Type)
{
	return ScalarStructs.Contains(Type.GetScriptStruct()) || (Type.GetScriptStruct() == IntStruct && Type.GetEnum() != nullptr);
}

bool FNiagaraTypeDefinition::TypesAreAssignable(const FNiagaraTypeDefinition& TypeA, const FNiagaraTypeDefinition& TypeB)
{
	if (const UClass* AClass = TypeA.GetClass())
	{
		if (const UClass* BClass = TypeB.GetClass())
		{
			return AClass == BClass;
			return true;
		}
	}
	
	if (const UClass* BClass = TypeB.GetClass())
	{
		return false;
	}

	if (const UClass* AClass = TypeA.GetClass())
	{
		return false;
	}

	// Make sure that enums are not assignable to enums of different types or just plain ints
	if (TypeA.GetStruct() == TypeB.GetStruct() &&
		TypeA.GetEnum() != TypeB.GetEnum())
	{
		return false;
	}

	if (TypeA.GetStruct() == TypeB.GetStruct())
	{
		return true;
	}

	bool bIsSupportedConversion = false;
	if (IsScalarDefinition(TypeA) && IsScalarDefinition(TypeB))
	{
		bIsSupportedConversion = (TypeA == IntDef && TypeB == FloatDef) || (TypeB == IntDef && TypeA == FloatDef);
	}
	else
	{
		bIsSupportedConversion = (TypeA == ColorDef && TypeB == Vec4Def) || (TypeB == ColorDef && TypeA == Vec4Def);
	}

	if (bIsSupportedConversion)
	{
		return true;
	}

	return	(TypeA == NumericDef && NumericStructs.Contains(TypeB.GetScriptStruct())) ||
			(TypeB == NumericDef && NumericStructs.Contains(TypeA.GetScriptStruct())) ||
			(TypeA == NumericDef && (TypeB.GetStruct() == GetIntStruct()) && TypeB.GetEnum() != nullptr) ||
			(TypeB == NumericDef && (TypeA.GetStruct() == GetIntStruct()) && TypeA.GetEnum() != nullptr);
}

bool FNiagaraTypeDefinition::IsLossyConversion(const FNiagaraTypeDefinition& TypeA, const FNiagaraTypeDefinition& TypeB)
{
	return (TypeA == IntDef && TypeB == FloatDef) || (TypeB == IntDef && TypeA == FloatDef);
}

FNiagaraTypeDefinition FNiagaraTypeDefinition::GetNumericOutputType(const TArray<FNiagaraTypeDefinition> TypeDefinintions, ENiagaraNumericOutputTypeSelectionMode SelectionMode)
{
	checkf(SelectionMode != ENiagaraNumericOutputTypeSelectionMode::None, TEXT("Can not get numeric output type with selection mode none."));

	//This may need some work. Should work fine for now.
	if (SelectionMode == ENiagaraNumericOutputTypeSelectionMode::Scalar)
	{
		bool bHasFloats = false;
		bool bHasInts = false;
		bool bHasBools = false;
		for (const FNiagaraTypeDefinition& Type : TypeDefinintions)
		{
			bHasFloats |= FloatStructs.Contains(Type.GetStruct());
			bHasInts |= IntStructs.Contains(Type.GetStruct());
			bHasBools |= BoolStructs.Contains(Type.GetStruct());
		}
		//Not sure what to do if we have multiple different types here.
		//Possibly pick this up ealier and throw a compile error?
		if (bHasFloats) return FNiagaraTypeDefinition::GetFloatDef();
		if (bHasInts) return FNiagaraTypeDefinition::GetIntDef();
		if (bHasBools) return FNiagaraTypeDefinition::GetBoolDef();
	}
	// Always return the numeric type definition if it's included since this isn't a valid use case and we don't want to hide it.
	int32 NumericTypeDefinitionIndex = TypeDefinintions.IndexOfByKey(NumericDef);
	if (NumericTypeDefinitionIndex != INDEX_NONE)
	{
		// TODO: Warning here?
		return NumericDef;
	}

	TArray<FNiagaraTypeDefinition> SortedTypeDefinitions = TypeDefinintions;
	SortedTypeDefinitions.Sort([&](const FNiagaraTypeDefinition& TypeA, const FNiagaraTypeDefinition& TypeB)
	{
		int32 AIndex = OrderedNumericTypes.IndexOfByKey(TypeA);
		int32 BIndex = OrderedNumericTypes.IndexOfByKey(TypeB);
		return AIndex < BIndex;
	});

	if (SelectionMode == ENiagaraNumericOutputTypeSelectionMode::Largest)
	{
		return SortedTypeDefinitions.Last();
	}
	else // if (SelectionMode == ENiagaraNumericOutputTypeSelectionMode::Smallest)
	{
		return SortedTypeDefinitions[0];
	}

	return FNiagaraTypeDefinition::GetGenericNumericDef();
}

bool FNiagaraTypeDefinition::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);
	return false;
}

void FNiagaraTypeDefinition::PostSerialize(const FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading() && Ar.CustomVer(FNiagaraCustomVersion::GUID) < FNiagaraCustomVersion::MemorySaving)
	{
		if (Enum_DEPRECATED != nullptr)
		{
			UnderlyingType = UT_Enum;
			ClassStructOrEnum = Enum_DEPRECATED;
		}
		else if (Struct_DEPRECATED != nullptr)
		{
			UnderlyingType = Struct_DEPRECATED->IsA<UClass>() ? UT_Class : UT_Struct;
			ClassStructOrEnum = Struct_DEPRECATED;
		}
		else
		{
			UnderlyingType = UT_None;
			ClassStructOrEnum = nullptr;
		}
	}
#endif
}

//////////////////////////////////////////////////////////////////////////

UNiagaraDataInterfaceBase* FNiagaraTypeRegistry::GetDefaultDataInterfaceByName(const FString& DIClassName)
{
	UClass* DIClass = nullptr;
	for (const FNiagaraTypeDefinition& Def : RegisteredTypes)
	{
		if (Def.IsDataInterface())
		{
			UClass* FoundDIClass = Def.GetClass();
			if (FoundDIClass && (FoundDIClass->GetName() == DIClassName || FoundDIClass->GetFullName() == DIClassName))
			{
				DIClass = FoundDIClass;
				break;
			}
		}
	}
	// Consider the possibility of a redirector pointing to a new location..
	if (DIClass == nullptr)
	{
		FCoreRedirectObjectName OldObjName;
		OldObjName.ObjectName = *DIClassName;
		FCoreRedirectObjectName NewObjName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, OldObjName);
		if (NewObjName.IsValid() && OldObjName != NewObjName)
		{
			return GetDefaultDataInterfaceByName(NewObjName.ObjectName.ToString());
		}
	}

	if (DIClass)
	{
		return CastChecked<UNiagaraDataInterfaceBase>(DIClass->GetDefaultObject(false)); // We wouldn't be registered if the CDO had not already been created...
	}

	return nullptr;
}


FDelegateHandle INiagaraModule::SetOnProcessShaderCompilationQueue(FOnProcessQueue InOnProcessQueue)
{
	checkf(OnProcessQueue.IsBound() == false, TEXT("Shader processing queue delegate already set."));
	OnProcessQueue = InOnProcessQueue;
	return OnProcessQueue.GetHandle();
}

void INiagaraModule::ResetOnProcessShaderCompilationQueue(FDelegateHandle DelegateHandle)
{
	checkf(OnProcessQueue.GetHandle() == DelegateHandle, TEXT("Can only reset the process compilation queue delegate with the handle it was created with."));
	OnProcessQueue.Unbind();
}

void INiagaraModule::ProcessShaderCompilationQueue()
{
	checkf(OnProcessQueue.IsBound(), TEXT("Can not process shader queue.  Delegate was never set."));
	return OnProcessQueue.Execute();
}

#undef LOCTEXT_NAMESPACE


