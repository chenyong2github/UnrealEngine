// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Modules/ModuleInterface.h"
#include "Engine/World.h"
#include "NiagaraTypes.h"
#include "Templates/SharedPointer.h"

class FNiagaraWorldManager;
class UNiagaraEmitter;
struct FNiagaraVMExecutableData;
class UNiagaraScript;
class FNiagaraCompileOptions;
class FNiagaraCompileRequestDataBase;
class INiagaraMergeManager;
class INiagaraEditorOnlyDataUtilities;
struct FNiagaraParameterStore;

extern NIAGARA_API int32 GEnableVerboseNiagaraChangeIdLogging;

/**
* Niagara module interface
*/
class NIAGARA_API INiagaraModule : public IModuleInterface
{
public:
#if WITH_EDITOR
	typedef TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> CompileRequestPtr;
	DECLARE_DELEGATE_RetVal_TwoParams(TSharedPtr<FNiagaraVMExecutableData>, FScriptCompiler,const FNiagaraCompileRequestDataBase*, const FNiagaraCompileOptions&);
	DECLARE_DELEGATE_RetVal_OneParam(CompileRequestPtr, FOnPrecompile, UObject*);
#endif
	DECLARE_DELEGATE_RetVal(void, FOnProcessQueue);

public:
	virtual void StartupModule()override;
	virtual void ShutdownModule()override;
	void ShutdownRenderingResources();

	FDelegateHandle SetOnProcessShaderCompilationQueue(FOnProcessQueue InOnProcessQueue);
	void ResetOnProcessShaderCompilationQueue(FDelegateHandle DelegateHandle);
	void ProcessShaderCompilationQueue();

#if WITH_EDITOR
	const INiagaraMergeManager& GetMergeManager() const;

	void RegisterMergeManager(TSharedRef<INiagaraMergeManager> InMergeManager);

	void UnregisterMergeManager(TSharedRef<INiagaraMergeManager> InMergeManager);

	const INiagaraEditorOnlyDataUtilities& GetEditorOnlyDataUtilities() const;

	void RegisterEditorOnlyDataUtilities(TSharedRef<INiagaraEditorOnlyDataUtilities> InEditorOnlyDataUtilities);

	void UnregisterEditorOnlyDataUtilities(TSharedRef<INiagaraEditorOnlyDataUtilities> InEditorOnlyDataUtilities);

	TSharedPtr<FNiagaraVMExecutableData> CompileScript(const FNiagaraCompileRequestDataBase* InCompileData, const FNiagaraCompileOptions& InCompileOptions);
	FDelegateHandle RegisterScriptCompiler(FScriptCompiler ScriptCompiler);
	void UnregisterScriptCompiler(FDelegateHandle DelegateHandle);

	TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> Precompile(UObject* InObj);
	FDelegateHandle RegisterPrecompiler(FOnPrecompile PreCompiler);
	void UnregisterPrecompiler(FDelegateHandle DelegateHandle);

#endif

	FORCEINLINE static int32 GetDetailLevel() { return EngineDetailLevel; }
	FORCEINLINE static float GetGlobalSpawnCountScale() { return EngineGlobalSpawnCountScale; }
	FORCEINLINE static float GetGlobalSystemCountScale() { return EngineGlobalSystemCountScale; }
	static bool IsTargetPlatformIncludedInLevelRangeForCook(const ITargetPlatform* InTargetPlatform, const class UNiagaraEmitter* InEmitter);

	static float EngineGlobalSpawnCountScale;
	static float EngineGlobalSystemCountScale;

	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_DeltaTime() { return Engine_DeltaTime; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_InvDeltaTime() { return Engine_InvDeltaTime; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Time() { return Engine_Time; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_RealTime() { return Engine_RealTime; }

	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_Position() { return Engine_Owner_Position; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_Velocity() { return Engine_Owner_Velocity; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_XAxis() { return Engine_Owner_XAxis; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_YAxis() { return Engine_Owner_YAxis; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_ZAxis() { return Engine_Owner_ZAxis; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_Scale() { return Engine_Owner_Scale; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_Rotation() { return Engine_Owner_Rotation; }

	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_SystemLocalToWorld() { return Engine_Owner_SystemLocalToWorld; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_SystemWorldToLocal() { return Engine_Owner_SystemWorldToLocal; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_SystemLocalToWorldTransposed() { return Engine_Owner_SystemLocalToWorldTransposed; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_SystemWorldToLocalTransposed() { return Engine_Owner_SystemWorldToLocalTransposed; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_SystemLocalToWorldNoScale() { return Engine_Owner_SystemLocalToWorldNoScale; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_SystemWorldToLocalNoScale() { return Engine_Owner_SystemWorldToLocalNoScale; }

	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_TimeSinceRendered() { return Engine_Owner_TimeSinceRendered; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_LODDistance() { return Engine_Owner_LODDistance; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_LODDistanceFraction() { return Engine_Owner_LODDistanceFraction; }

	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Owner_ExecutionState() { return Engine_Owner_ExecutionState; }

	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_ExecutionCount() { return Engine_ExecutionCount; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Emitter_NumParticles() { return Engine_Emitter_NumParticles; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Emitter_TotalSpawnedParticles() { return Engine_Emitter_TotalSpawnedParticles; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_Emitter_SpawnCountScale() { return Engine_Emitter_SpawnCountScale; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_System_TickCount() { return Engine_System_TickCount; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_System_NumEmittersAlive() { return Engine_System_NumEmittersAlive; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_System_NumEmitters() { return Engine_System_NumEmitters; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_NumSystemInstances() { return Engine_NumSystemInstances; }

	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_GlobalSpawnCountScale() { return Engine_GlobalSpawnCountScale; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_GlobalSystemScale() { return Engine_GlobalSystemScale; }

	FORCEINLINE static const FNiagaraVariable&  GetVar_Engine_System_Age() { return Engine_System_Age; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Emitter_Age() { return Emitter_Age; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Emitter_LocalSpace() { return Emitter_LocalSpace; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Emitter_Determinism() { return Emitter_Determinism; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Emitter_OverrideGlobalSpawnCountScale() { return Emitter_OverrideGlobalSpawnCountScale; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Emitter_RandomSeed() { return Emitter_RandomSeed; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Emitter_SpawnRate() { return Emitter_SpawnRate; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Emitter_SpawnInterval() { return Emitter_SpawnInterval; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Emitter_SimulationTarget() { return Emitter_SimulationTarget; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_ScriptUsage() { return ScriptUsage; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Emitter_InterpSpawnStartDt() { return Emitter_InterpSpawnStartDt; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Emitter_SpawnGroup() { return Emitter_SpawnGroup; }

	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_UniqueID() { return Particles_UniqueID; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_ID() { return Particles_ID; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_Position() { return Particles_Position; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_Velocity() { return Particles_Velocity; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_Color() { return Particles_Color; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_SpriteRotation() { return Particles_SpriteRotation; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_NormalizedAge() { return Particles_NormalizedAge; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_SpriteSize() { return Particles_SpriteSize; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_SpriteFacing() { return Particles_SpriteFacing; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_SpriteAlignment() { return Particles_SpriteAlignment; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_SubImageIndex() { return Particles_SubImageIndex; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_DynamicMaterialParameter() { return Particles_DynamicMaterialParameter; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_DynamicMaterialParameter1() { return Particles_DynamicMaterialParameter1; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_DynamicMaterialParameter2() { return Particles_DynamicMaterialParameter2; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_DynamicMaterialParameter3() { return Particles_DynamicMaterialParameter3; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_Scale() { return Particles_Scale; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_Lifetime() { return Particles_Lifetime; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_MeshOrientation() { return Particles_MeshOrientation; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_UVScale() { return Particles_UVScale; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_CameraOffset() { return Particles_CameraOffset; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_MaterialRandom() { return Particles_MaterialRandom; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_LightRadius() { return Particles_LightRadius; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_LightExponent() { return Particles_LightExponent; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_LightEnabled() { return Particles_LightEnabled; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_LightVolumetricScattering() { return Particles_LightVolumetricScattering; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_RibbonID() { return Particles_RibbonID; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_RibbonWidth() { return Particles_RibbonWidth; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_RibbonTwist() { return Particles_RibbonTwist; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_RibbonFacing() { return Particles_RibbonFacing; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_Particles_RibbonLinkOrder() { return Particles_RibbonLinkOrder; }
	
	FORCEINLINE static const FNiagaraVariable&  GetVar_DataInstance_Alive() { return DataInstance_Alive; }
	FORCEINLINE static const FNiagaraVariable&  GetVar_BeginDefaults() { return Translator_BeginDefaults; }

#if WITH_EDITORONLY_DATA
	FORCEINLINE static const FNiagaraParameterStore& GetFixedSystemInstanceParameterStore() { return FixedSystemInstanceParameters; }
#endif
private:

#if WITH_EDITORONLY_DATA
	void InitFixedSystemInstanceParameterStore();
#endif

	FOnProcessQueue OnProcessQueue;

#if WITH_EDITORONLY_DATA
	TSharedPtr<INiagaraMergeManager> MergeManager;
	TSharedPtr<INiagaraEditorOnlyDataUtilities> EditorOnlyDataUtilities;

	FScriptCompiler ScriptCompilerDelegate;
	FOnPrecompile ObjectPrecompilerDelegate;
#endif

	void OnChangeDetailLevel(class IConsoleVariable* CVar);
	static int32 EngineDetailLevel;

private:
	static FNiagaraVariable Engine_DeltaTime;
	static FNiagaraVariable Engine_InvDeltaTime;
	static FNiagaraVariable Engine_Time; 
	static FNiagaraVariable Engine_RealTime; 

	static FNiagaraVariable Engine_Owner_Position;
	static FNiagaraVariable Engine_Owner_Velocity;
	static FNiagaraVariable Engine_Owner_XAxis;
	static FNiagaraVariable Engine_Owner_YAxis;
	static FNiagaraVariable Engine_Owner_ZAxis;
	static FNiagaraVariable Engine_Owner_Scale;
	static FNiagaraVariable Engine_Owner_Rotation;

	static FNiagaraVariable Engine_Owner_SystemLocalToWorld;
	static FNiagaraVariable Engine_Owner_SystemWorldToLocal;
	static FNiagaraVariable Engine_Owner_SystemLocalToWorldTransposed;
	static FNiagaraVariable Engine_Owner_SystemWorldToLocalTransposed;
	static FNiagaraVariable Engine_Owner_SystemLocalToWorldNoScale;
	static FNiagaraVariable Engine_Owner_SystemWorldToLocalNoScale;

	static FNiagaraVariable Engine_Owner_TimeSinceRendered;
	static FNiagaraVariable Engine_Owner_LODDistance;
	static FNiagaraVariable Engine_Owner_LODDistanceFraction;
	
	static FNiagaraVariable Engine_Owner_ExecutionState;

	static FNiagaraVariable Engine_ExecutionCount;
	static FNiagaraVariable Engine_Emitter_NumParticles;
	static FNiagaraVariable Engine_Emitter_TotalSpawnedParticles;
	static FNiagaraVariable Engine_Emitter_SpawnCountScale;
	static FNiagaraVariable Engine_System_TickCount;
	static FNiagaraVariable Engine_System_NumEmittersAlive;
	static FNiagaraVariable Engine_System_NumEmitters;
	static FNiagaraVariable Engine_NumSystemInstances;

	static FNiagaraVariable Engine_GlobalSpawnCountScale;
	static FNiagaraVariable Engine_GlobalSystemScale;

	static FNiagaraVariable Engine_System_Age;
	static FNiagaraVariable Emitter_Age;
	static FNiagaraVariable Emitter_LocalSpace;
	static FNiagaraVariable Emitter_Determinism;
	static FNiagaraVariable Emitter_OverrideGlobalSpawnCountScale;
	static FNiagaraVariable Emitter_SimulationTarget;
	static FNiagaraVariable Emitter_RandomSeed;
	static FNiagaraVariable Emitter_SpawnRate;
	static FNiagaraVariable Emitter_SpawnInterval;
	static FNiagaraVariable Emitter_InterpSpawnStartDt;
	static FNiagaraVariable Emitter_SpawnGroup;

	static FNiagaraVariable Particles_UniqueID;
	static FNiagaraVariable Particles_ID;
	static FNiagaraVariable Particles_Position;
	static FNiagaraVariable Particles_Velocity;
	static FNiagaraVariable Particles_Color;
	static FNiagaraVariable Particles_SpriteRotation;
	static FNiagaraVariable Particles_NormalizedAge;
	static FNiagaraVariable Particles_SpriteSize;
	static FNiagaraVariable Particles_SpriteFacing;
	static FNiagaraVariable Particles_SpriteAlignment;
	static FNiagaraVariable Particles_SubImageIndex;
	static FNiagaraVariable Particles_DynamicMaterialParameter;
	static FNiagaraVariable Particles_DynamicMaterialParameter1;
	static FNiagaraVariable Particles_DynamicMaterialParameter2;
	static FNiagaraVariable Particles_DynamicMaterialParameter3;
	static FNiagaraVariable Particles_Scale;
	static FNiagaraVariable Particles_Lifetime;
	static FNiagaraVariable Particles_MeshOrientation;
	static FNiagaraVariable Particles_UVScale;
	static FNiagaraVariable Particles_CameraOffset;
	static FNiagaraVariable Particles_MaterialRandom;
	static FNiagaraVariable Particles_LightRadius;
	static FNiagaraVariable Particles_LightExponent;
	static FNiagaraVariable Particles_LightEnabled;
	static FNiagaraVariable Particles_LightVolumetricScattering;
	static FNiagaraVariable Particles_RibbonID;
	static FNiagaraVariable Particles_RibbonWidth;
	static FNiagaraVariable Particles_RibbonTwist;
	static FNiagaraVariable Particles_RibbonFacing;
	static FNiagaraVariable Particles_RibbonLinkOrder;

	static FNiagaraVariable ScriptUsage;
	static FNiagaraVariable DataInstance_Alive;
	static FNiagaraVariable Translator_BeginDefaults;

#if WITH_EDITORONLY_DATA
	static FNiagaraParameterStore FixedSystemInstanceParameters;
#endif
};

