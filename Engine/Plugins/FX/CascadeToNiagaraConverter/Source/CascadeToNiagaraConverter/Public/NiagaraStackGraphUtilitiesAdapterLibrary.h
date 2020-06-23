// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NiagaraEditor/Public/ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "AssetData.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/SubUVAnimation.h"
#include "Particles/ParticleSpriteEmitter.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particles/TypeData/ParticleModuleTypeDataRibbon.h"
#include "NiagaraStackGraphUtilitiesAdapterLibrary.generated.h"

class UNiagaraSystem;
class UNiagaraEmitter;
class UNiagaraRendererProperties;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleModule;
class FNiagaraEmitterHandleViewModel;
class FNiagaraSystemViewModel;
class UNiagaraClipboardFunction;
class UNiagaraClipboardFunctionInput;
class UDistributionFloat;
struct FParticleBurst;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	Enums																									  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UENUM()
enum class EScriptExecutionCategory : uint8
{
	EmitterSpawn,
	EmitterUpdate,
	ParticleSpawn,
	ParticleUpdate
};

UENUM()
enum class EDistributionType : uint8
{
	Const,
	ConstCurve,
	Uniform,
	UniformCurve,
	Parameter,

	NONE
};

UENUM()
enum class EDistributionValueType : uint8
{
	Float,
	Vector,

	NONE
};

UENUM()
enum class ECascadeRendererType : uint8
{ 
	Sprite,
	Mesh,
	Ribbon,
	Beam,
	AnimTrail,

	NONE
};

// UENUM()
// enum class EConverterMessageSeverity : uint8
// {
// 	Log,
// 	Warning,
// 	Error,
// 
// 	NONE
// };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	Wrapper Structs																							  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** BlueprintType wrapper around FParticleBurst to allow managing in blueprint/python logic. */
USTRUCT(BlueprintInternalUseOnly)
struct FParticleBurstBlueprint
{
	GENERATED_USTRUCT_BODY()

	FParticleBurstBlueprint() {};

	FParticleBurstBlueprint(const FParticleBurst& InParticleBurst)
		: Count(InParticleBurst.Count)
		, CountLow(InParticleBurst.CountLow)
		, Time(InParticleBurst.Time)
	{};

	/** The number of particles to burst */
	UPROPERTY(BlueprintReadWrite, Category = ParticleBurst)
	int32 Count;

	/** If >= 0, use as a range [CountLow..Count] */
	UPROPERTY(BlueprintReadWrite, Category = ParticleBurst)
	int32 CountLow;

	/** The time at which to burst them (0..1: emitter lifetime) */
	UPROPERTY(BlueprintReadWrite, Category = ParticleBurst)
	float Time;
};

/** Wrapper for setting the value on a parameter of a UNiagaraScript, applied through a UNiagaraScriptConversionContext. */
USTRUCT(BlueprintInternalUseOnly)
struct FNiagaraScriptContextInput
{
	GENERATED_BODY()

	FNiagaraScriptContextInput() {};

	FNiagaraScriptContextInput(UNiagaraClipboardFunctionInput* InClipboardFunctionInput, const FNiagaraTypeDefinition& InTargetTypeDefinition)
		: ClipboardFunctionInput(InClipboardFunctionInput)
		, TargetTypeDefinition(InTargetTypeDefinition)
	{};

	UPROPERTY()
	UNiagaraClipboardFunctionInput* ClipboardFunctionInput;

	UPROPERTY()
	FNiagaraTypeDefinition TargetTypeDefinition;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	Logging Framework																						  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
USTRUCT(BlueprintInternalUseOnly)
struct FGenericConverterMessage
{
	GENERATED_BODY()

public:
	FGenericConverterMessage() {};

	FGenericConverterMessage(FString InMessage, ENiagaraMessageSeverity InMessageSeverity, bool bInIsVerbose)
		: Message(InMessage)
		, MessageSeverity(InMessageSeverity)
		, bIsVerbose(bInIsVerbose)
	{};

	FString Message;
	ENiagaraMessageSeverity MessageSeverity;
	bool bIsVerbose;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	Wrapper Classes																							  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Wrapper for modifying a UNiagaraSystem by adding Emitters through UNiagaraEmitterConversionContexts.
 */
UCLASS(BlueprintInternalUseOnly)
class UNiagaraSystemConversionContext : public UObject
{
	GENERATED_BODY()

public:
	UNiagaraSystemConversionContext() {};

	/**
	 * Init the System Conversion Context.
	 * @param InSystem					The System to convert.
	 * @param InSystemViewModelGuid		A Guid key to the FNiagaraSystemViewModel pointing at the InSystem.
	 */
	void Init(UNiagaraSystem* InSystem, FGuid InSystemViewModelGuid)
	{
		System = InSystem;
		SystemViewModelGuid = InSystemViewModelGuid;
	}

	/** Add an empty emitter to the system and return an emitter conversion context. */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	UNiagaraEmitterConversionContext* AddEmptyEmitter(FString NewEmitterNameString);

	void Finalize();

private:
	UPROPERTY()
	UNiagaraSystem* System;

	UPROPERTY()
	FGuid SystemViewModelGuid;
};

/** 
 * Wrapper for modifying a UNiagaraEmitter by adding Scripts and Renderers through UNiagaraScriptConversionContexts and 
 * UNiagaraRendererProperties, respectively. 
 */
UCLASS(BlueprintInternalUseOnly)
class UNiagaraEmitterConversionContext : public UObject
{
	GENERATED_BODY()

public:
	UNiagaraEmitterConversionContext() {};
	
	/** 
	 * Find or add a script conversion context to this emitter conversion context. If a script conversion context
	 * is not found by name string then a new one is created and initialized from the NiagaraScriptAssetData.
	 */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	UNiagaraScriptConversionContext* FindOrAddScript(FString ScriptNameString, FAssetData NiagaraScriptAssetData);

	/** Add a renderer to this emitter conversion context through renderer properties. */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void AddRenderer(FString RendererNameString, UNiagaraRendererProperties* NewRendererProperties);

	/** Find an added renderer properties by name string. */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	UNiagaraRendererProperties* FindRenderer(FString RendererNameString);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void Log(FString Message, ENiagaraMessageSeverity Severity, bool bIsVerbose);

	/** 
	 * Apply all pending UNiagaraScriptConversionContexts and UNiagaraRendererProperties to this
	 * UNiagaraEmitterContext by creating clipboard inputs and pasting them onto the emitter conversion context's
	 * Emitter.
	 */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void Finalize();

	/** 
	 * Init the Emitter Conversion Context. 
	 * @param InEmitter						The Emitter to convert.
	 * @param InEmitterHandleViewModelGuid	A Guid key to the FNiagaraEmitterHandleViewModel pointing at the InEmitter.
	 */
	void Init(UNiagaraEmitter* InEmitter, FGuid InEmitterHandleViewModelGuid)
	{
		Emitter = InEmitter;
		EmitterHandleViewModelGuid = InEmitterHandleViewModelGuid;
	};

private:
	UPROPERTY(EditAnywhere, Category = "FXConverterUtilities")
	UNiagaraEmitter* Emitter;

	UPROPERTY()
	FGuid EmitterHandleViewModelGuid;

	UPROPERTY()
	TMap<FString, UNiagaraScriptConversionContext*> ScriptNameToStagedScriptMap;

	UPROPERTY()
	TMap<FString, UNiagaraRendererProperties*> RendererNameToStagedRendererPropertiesMap;

	UPROPERTY()
	TArray<FGenericConverterMessage> EmitterMessages;

	UNiagaraNodeFunctionCall* PastedFunctionCallNode = nullptr;

	UFUNCTION()
	void SetPastedFunctionCallNode(UNiagaraNodeFunctionCall* InFunctionCallNode) {PastedFunctionCallNode = InFunctionCallNode;};
};

/** Wrapper for programmatically adding scripts to a UNiagaraEmitter through a UNiagaraEmitterConversionContext. */
UCLASS(BlueprintInternalUseOnly)
class UNiagaraScriptConversionContext : public UObject
{
	GENERATED_BODY()

public:
	UNiagaraScriptConversionContext() {};

	/** Init the Niagara Script Conversion Context with the assetdata to a UNiagaraScript. */
	void Init(const FAssetData& InNiagaraScriptAssetData);

	/** 
	 * Set a parameter on the Script this Script Conversion Context holds. 
	 * @param ParameterName		The target parameter name.
	 * @param ParameterInput	Value to set on the parameter.
	 * @return Whether setting the parameter was successful. 
	 */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	bool SetParameter(FString ParameterName, FNiagaraScriptContextInput ParameterInput);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void Log(FString Message, ENiagaraMessageSeverity Severity, bool bIsVerbose);

	const TArray<const UNiagaraClipboardFunctionInput*>& GetClipboardFunctionInputs() { return FunctionInputs; };

	UNiagaraScript* GetScript() { return Script; };

	const TArray<FGenericConverterMessage>& GetStackMessages() const {return StackMessages;};

public:
	// Execution category to add this script to when it is finalized to a system or emitter.
	UPROPERTY(BlueprintReadWrite, Category = "Properties")
	EScriptExecutionCategory TargetExecutionCategory;

	// Index in the execution category to add this script to when it is finalized to a system or emitter. Increasing index is lower in the stack.
	UPROPERTY(BlueprintReadWrite, Category = "Properties")
	int32 TargetIndex;

private:
	UPROPERTY()
	TArray<const UNiagaraClipboardFunctionInput*> FunctionInputs;

	UPROPERTY()
	UNiagaraScript* Script;

	UPROPERTY()
	TArray<FGenericConverterMessage> StackMessages;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	UFXConverterUtilitiesLibrary																			  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
* A C++ and Blueprint accessible library for converting fx type assets (Cascade and Niagara)
*/
UCLASS(BlueprintType)
class UFXConverterUtilitiesLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Generic Utilities
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static FString GetLongPackagePath(const FString& InLongPackageName) { return FPackageName::GetLongPackagePath(InLongPackageName); }
	
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static bool ObjectIsA(UObject* Object, UClass* Class) { return Object->IsA(Class); };


	// Cascade Emitter and ParticleLodLevel Getters
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static TArray<UParticleEmitter*> GetCascadeSystemEmitters(const UParticleSystem* System);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static UParticleLODLevel* GetCascadeEmitterLodLevel(UParticleEmitter* Emitter, const int32 Idx);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static TArray<UParticleModule*> GetLodLevelModules(UParticleLODLevel* LodLevel);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static UParticleModuleSpawn* GetLodLevelSpawnModule(UParticleLODLevel* LodLevel);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static UParticleModuleRequired* GetLodLevelRequiredModule(UParticleLODLevel* LodLevel);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static UParticleModuleTypeDataBase* GetLodLevelTypeDataModule(UParticleLODLevel* LodLevel);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static FName GetCascadeEmitterName(UParticleEmitter* Emitter);


	// Niagara Script and Script Input Helpers
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static UNiagaraScriptConversionContext* CreateScriptContext(FAssetData NiagaraScriptAssetData);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static FNiagaraScriptContextInput CreateScriptInputFloat(float Value);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static FNiagaraScriptContextInput CreateScriptInputVector(FVector Value);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static FNiagaraScriptContextInput CreateScriptInputInt(int32 Value);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static FNiagaraScriptContextInput CreateScriptInputDI(UNiagaraScriptConversionContext* Value, FString InputType);


	// Niagara Renderer Properties Helpers
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraRibbonRendererProperties* CreateRibbonRendererProperties();

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraMeshRendererProperties* CreateMeshRendererProperties();


	// Niagara System and Emitter Helpers
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static UNiagaraSystemConversionContext* CreateSystemConversionContext(UNiagaraSystem* InSystem);


	// Cascade Particle Module Getters
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UClass* GetParticleModuleSpawnClass();

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UClass* GetParticleModuleRequiredClass();

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UClass* GetParticleModuleColorOverLifeClass();

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UClass* GetParticleModuleLifetimeClass();

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UClass* GetParticleModuleSizeClass();

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UClass* GetParticleModuleVelocityClass();

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UClass* GetParticleModuleTypeDataGPUClass();

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UClass* GetParticleModuleTypeDataMeshClass();

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UClass* GetParticleModuleConstantAccelerationClass();

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleTypeDataMeshProps(
			UParticleModuleTypeDataMesh* ParticleModule
			, UStaticMesh*& OutMesh
			, float& OutLODSizeScale
			, bool& bOutUseStaticMeshLODs
			, bool& bOutCastShadows
			, bool& bOutDoCollisions
			, TEnumAsByte<EMeshScreenAlignment>& OutMeshAlignment
			, bool& bOutOverrideMaterial
			, bool& bOutOverrideDefaultMotionBlurSettings
			, bool& bOutEnableMotionBlur
			, UDistribution*& OutRollPitchYawRange
			, TEnumAsByte<EParticleAxisLock>& OutAxisLockOption
			, bool& bOutCameraFacing
			, TEnumAsByte<EMeshCameraFacingUpAxis>& OutCameraFacingUpAxisOption_DEPRECATED
			, TEnumAsByte<EMeshCameraFacingOptions>& OutCameraFacingOption
			, bool& bOutApplyParticleRotationAsSpin
			, bool& bOutFacingCameraDirectionRatherThanPosition
			, bool& bOutCollisionsConsiderParticleSize
		);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UClass* GetParticleModuleTypeDataRibbonClass();

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleTypeDataRibbonProps(
		UParticleModuleTypeDataRibbon* ParticleModule
		, int32& OutMaxTessellationBetweenParticles
		, int32& OutSheetsPerTrail
		, int32& OutMaxTrailCount
		, int32& OutMaxParticleInTrailCount
		, bool& bOutDeadTrailsOnDeactivate
		, bool& bOutClipSourceSegment
		, bool& bOutEnablePreviousTangentRecalculation
		, bool& bOutTangentRecalculationEveryFrame
		, bool& bOutSpawnInitialParticle
		, TEnumAsByte<ETrailsRenderAxisOption>& OutRenderAxis
		, float& OutTangentSpawningScalar
		, bool& bOutRenderGeometry
		, bool& bOutRenderSpawnPoints
		, bool& bOutRenderTangents
		, bool& bOutRenderTessellation
		, float& OutTilingDistance
		, float& OutDistanceTessellationStepSize
		, bool& bOutEnableTangentDiffInterpScale
		, float& OutTangentTessellationScalar
	);
	
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleSpawnProps(
		  UParticleModuleSpawn* ParticleModuleSpawn
		, UDistributionFloat*& OutRate
		, UDistributionFloat*& OutRateScale
		, TEnumAsByte<EParticleBurstMethod>& OutBurstMethod
		, TArray<FParticleBurstBlueprint>& OutBurstList
		, UDistributionFloat*& OutBurstScale
		, bool& bOutApplyGlobalSpawnRateScale
		, bool& bOutProcessSpawnRate
		, bool& bOutProcessSpawnBurst
	);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleRequiredProps(
		  UParticleModuleRequired* ParticleModuleRequired
		, UMaterialInterface*& OutMaterialInterface
		, TEnumAsByte<EParticleScreenAlignment>& OutScreenAlignment
		, bool& bOutUseLocalSpace
		, int32& OutSubImages_Horizontal
		, int32& OutSubImages_Vertical
		, TEnumAsByte<EParticleSortMode>& OutSortMode
		, TEnumAsByte<EParticleSubUVInterpMethod>& OutInterpolationMethod
		, uint8& bOutRemoveHMDRoll
		, float& OutMinFacingCameraBlendDistance
		, float& OutMaxFacingCameraBlendDistance
		, UTexture2D*& OutCutoutTexture
		, TEnumAsByte<ESubUVBoundingVertexCount>& OutBoundingMode
		, TEnumAsByte<EOpacitySourceMode>& OutOpacitySourceMode
	);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleColorOverLifeProps(UParticleModuleColorOverLife* ParticleModule, UDistribution*& OutColorOverLife, UDistribution*& OutAlphaOverLife, bool& bOutClampAlpha);
	
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleLifetimeProps(UParticleModuleLifetime* ParticleModule, UDistribution*& OutLifetime);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleSizeProps(UParticleModuleSize* ParticleModule, UDistribution*& OutStartSize);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleVelocityProps(UParticleModuleVelocity* ParticleModule, UDistribution*& OutStartVelocity, UDistribution*& OutStartVelocityRadial, bool& bOutInWorldSpace, bool& bOutApplyOwnerScale);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleConstantAccelerationProps(UParticleModuleAccelerationConstant* ParticleModule, FVector& OutConstAcceleration);


	// Cascade Distribution Getters
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetDistributionType(
		UDistribution* Distribution
		, EDistributionType& OutDistributionType
		, EDistributionValueType& OutCascadeDistributionValueType);

	static bool GetIsDistributionOfType(
		UDistribution* Distribution
		, EDistributionType TargetDistributionType
		, EDistributionValueType TargetDistributionValueType
		, FText& OutStatus);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetFloatDistributionConstValues(UDistribution* Distribution, FText& OutStatus, float& OutConstFloat);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetVectorDistributionConstValues(UDistribution* Distribution, FText& OutStatus, FVector& OutConstVector);

	//static void GetNiagaraCurveForCascadeDistributionCurve(UDistribution* Distribution, )

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetFloatDistributionUniformValues(UDistribution* Distribution, FText& OutStatus, float& OutMin, float& OutMax);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetVectorDistributionUniformValues(UDistribution* Distribution, FText& OutStatus, FVector& OutMin, FVector& OutMax);
// 	void UFXConverterUtilitiesLibrary::GetFloatDistributionUniformCurveValues(UDistributionFloatUniformCurve* Distribution);
// 	void UFXConverterUtilitiesLibrary::GetFloatDistributionConstCurveValues(UDistributionFloatParticleParameter* Distribution);


	// Maps from python addressable FGuid to non-blueprint types
	static TMap<FGuid, TSharedPtr<FNiagaraEmitterHandleViewModel>> GuidToNiagaraEmitterHandleViewModelMap;
	static TMap<FGuid, TSharedPtr<FNiagaraSystemViewModel>> GuidToNiagaraSystemViewModelMap;

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void Cleanup();
};
