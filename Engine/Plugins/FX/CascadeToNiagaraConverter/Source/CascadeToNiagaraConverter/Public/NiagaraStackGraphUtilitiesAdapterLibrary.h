// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NiagaraEditor/Public/ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "AssetData.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/SubUVAnimation.h"
#include "Particles/Camera/ParticleModuleCameraOffset.h"
#include "Particles/Parameter/ParticleModuleParameterDynamic.h"
#include "Particles/ParticleSpriteEmitter.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/Orbit/ParticleModuleOrbit.h"
#include "Particles/Collision/ParticleModuleCollisionBase.h"
#include "Particles/TypeData/ParticleModuleTypeDataGpu.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particles/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Curves/RichCurve.h"
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
struct FNiagaraMeshMaterialOverride;
struct FRichCurveKey;

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

UENUM()
enum class ENiagaraScriptInputType : uint8
{
	Int,
	Float,
	Vec2,
	Vec3,
	Vec4,
	LinearColor,
	Quaternion,
	Struct,
	Enum,
	DataInterface,

	NONE
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	Wrapper Structs																							  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Wrapper for storing a script execution category with a script conversion context. */
USTRUCT()
struct FScriptConversionContextAndExecutionCategory
{
	GENERATED_BODY()

	FScriptConversionContextAndExecutionCategory() = default;

	FScriptConversionContextAndExecutionCategory(UNiagaraScriptConversionContext* InScriptConversionContext, EScriptExecutionCategory InScriptExecutionCategory)
		: ScriptConversionContext(InScriptConversionContext)
		, ScriptExecutionCategory(InScriptExecutionCategory)
	{};

	UPROPERTY()
		UNiagaraScriptConversionContext* ScriptConversionContext;

	UPROPERTY()
		EScriptExecutionCategory ScriptExecutionCategory;

};

/** BlueprintType wrapper around FEmitterDynamicParameterBP to allow managing in blueprint/python logic. */
USTRUCT(BlueprintInternalUseOnly)
struct FEmitterDynamicParameterBP
{
	GENERATED_USTRUCT_BODY()

	/** The parameter name - from the material DynamicParameter expression. READ-ONLY */
	UPROPERTY(BlueprintReadWrite, Category = EmitterDynamicParameter)
	FName ParamName;

	/** If true, use the EmitterTime to retrieve the value, otherwise use Particle RelativeTime. */
	UPROPERTY(BlueprintReadWrite, Category = EmitterDynamicParameter)
	bool bUseEmitterTime;

	/** If true, only set the value at spawn time of the particle, otherwise update each frame. */
	UPROPERTY(BlueprintReadWrite, Category = EmitterDynamicParameter)
	bool bSpawnTimeOnly;

	/** Where to get the parameter value from. */
	UPROPERTY(BlueprintReadWrite, Category = EmitterDynamicParameter)
	TEnumAsByte<EEmitterDynamicParameterValue> ValueMethod;

	/** If true, scale the velocity value selected in ValueMethod by the evaluated ParamValue. */
	UPROPERTY(BlueprintReadWrite, Category = EmitterDynamicParameter)
	bool bScaleVelocityByParamValue;

	/** The distribution for the parameter value. */
	UPROPERTY(EditAnywhere, Category = EmitterDynamicParameter)
	struct FRawDistributionFloat ParamValue;


	FEmitterDynamicParameterBP()
		: bUseEmitterTime(false)
		, bSpawnTimeOnly(false)
		, ValueMethod(0)
		, bScaleVelocityByParamValue(false)
	{
	}
	FEmitterDynamicParameterBP(FName InParamName, uint32 InUseEmitterTime, TEnumAsByte<enum EEmitterDynamicParameterValue> InValueMethod, UDistributionFloatConstant* InDistribution)
		: ParamName(InParamName)
		, bUseEmitterTime((bool)InUseEmitterTime)
		, bSpawnTimeOnly(false)
		, ValueMethod(InValueMethod)
		, bScaleVelocityByParamValue(false)
	{
		ParamValue.Distribution = InDistribution;
	}

	FEmitterDynamicParameterBP(const FEmitterDynamicParameter& DynamicParameter)
		: ParamName(DynamicParameter.ParamName)
		, bUseEmitterTime(DynamicParameter.bUseEmitterTime)
		, bSpawnTimeOnly(DynamicParameter.bSpawnTimeOnly)
		, ValueMethod(DynamicParameter.ValueMethod)
		, bScaleVelocityByParamValue(DynamicParameter.bScaleVelocityByParamValue)
	{
		ParamValue.Distribution = DynamicParameter.ParamValue.Distribution;
	}
};

/** BlueprintType wrapper around FOrbitOptions to allow managing in blueprint/python logic. */
USTRUCT(BlueprintInternalUseOnly)
struct FOrbitOptionsBP
{
	GENERATED_USTRUCT_BODY()

	/**
	 *	Whether to process the data during spawning.
	 */
	UPROPERTY(EditAnywhere, Category=OrbitOptions)
	bool bProcessDuringSpawn;

	/**
	 *	Whether to process the data during updating.
	 */
	UPROPERTY(EditAnywhere, Category=OrbitOptions)
	bool bProcessDuringUpdate;

	/**
	 *	Whether to use emitter time during data retrieval.
	 */
	UPROPERTY(EditAnywhere, Category=OrbitOptions)
	bool bUseEmitterTime;

	FOrbitOptionsBP()
		: bProcessDuringSpawn(true)
		, bProcessDuringUpdate(false)
		, bUseEmitterTime(false)
	{
	}

	FOrbitOptionsBP(const FOrbitOptions& OrbitOptions)
		: bProcessDuringSpawn(OrbitOptions.bProcessDuringSpawn)
		, bProcessDuringUpdate(OrbitOptions.bProcessDuringUpdate)
		, bUseEmitterTime(OrbitOptions.bUseEmitterTime)
	{
	}
};

/** BlueprintType wrapper around FParticleBurst to allow managing in blueprint/python logic. */
USTRUCT(BlueprintInternalUseOnly)
struct FParticleBurstBlueprint
{
	GENERATED_USTRUCT_BODY()

	FParticleBurstBlueprint()
	{
		Count = 0;
		CountLow = 0;
		Time = 0.0f;
	};

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

/** Wrapper for tracking indices of parameter set nodes added to Emitter Conversion Contexts. */
USTRUCT(BlueprintInternalUseOnly)
struct FParameterSetIndices
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> Indices;
};

USTRUCT(BlueprintInternalUseOnly)
struct FRichCurveKeyBP : public FRichCurveKey
{
	GENERATED_BODY()

	FRichCurveKeyBP()
		:FRichCurveKey()
	{};

	FRichCurveKeyBP(const FRichCurveKey& Other) 
		:FRichCurveKey(Other) 
	{};

	FRichCurveKey ToBase() const { return FRichCurveKey(Time, Value, ArriveTangent, LeaveTangent, InterpMode); };

	static TArray<FRichCurveKey> KeysToBase(const TArray<FRichCurveKeyBP>& InKeyBPs);
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

	DECLARE_DYNAMIC_DELEGATE_OneParam(FOnPasteScript, int32, ScriptIdx);

public:
	UNiagaraEmitterConversionContext()
		: PastedFunctionCallNode(nullptr)
	{};
	
	/** 
	 * Find or add a script conversion context to this emitter conversion context. If a script conversion context
	 * is not found by name string then a new one is created and initialized from the NiagaraScriptAssetData.
	 */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	UNiagaraScriptConversionContext* FindOrAddModuleScript(FString ScriptNameString, FAssetData NiagaraScriptAssetData, EScriptExecutionCategory ExecutionCategory);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	UNiagaraScriptConversionContext* FindModuleScript(FString ScriptNameString);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void AddModuleScript(UNiagaraScriptConversionContext* ScriptConversionContext, FString ScriptNameString, EScriptExecutionCategory ExecutionCategory);

	/**
	 * Add a set parameter module to the emitter handled by this emitter conversion context.
	 */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void SetParameterDirectly(FString ParameterNameString, UNiagaraScriptConversionContextInput* ParameterInput, EScriptExecutionCategory TargetExecutionCategory);

	/** Add a renderer to this emitter conversion context through renderer properties. */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void AddRenderer(FString RendererNameString, UNiagaraRendererProperties* NewRendererProperties);

	/** Find an added renderer properties by name string. */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	UNiagaraRendererProperties* FindRenderer(FString RendererNameString);

	/**
	 * Log a message to the niagara message log for the emitter.
	 * @param Message		The message string to display.
	 * @param Severity		The severity of the message.
	 * @param bIsVerbose	Whether the message is verbose and should be displayed conditionally.
	 */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void Log(FString Message, ENiagaraMessageSeverity Severity, bool bIsVerbose = false);

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
		bEnabled = true;
	};

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void SetEnabled(bool bInEnabled) {bEnabled = bInEnabled;}

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	bool GetEnabled() const {return bEnabled;};



private:
	UPROPERTY(EditAnywhere, Category = "FXConverterUtilities")
	UNiagaraEmitter* Emitter;

	UPROPERTY()
	FGuid EmitterHandleViewModelGuid;

	UPROPERTY()
	TMap<FString, FScriptConversionContextAndExecutionCategory> ScriptNameToStagedScriptMap;

	UPROPERTY()
	TMap<EScriptExecutionCategory, FParameterSetIndices> ScriptExecutionCategoryToParameterSetIndicesMap;

	UPROPERTY()
	TArray<UNiagaraClipboardFunction*> StagedParameterSets;

	UPROPERTY()
	TMap<FString, UNiagaraRendererProperties*> RendererNameToStagedRendererPropertiesMap;

	UPROPERTY()
	TArray<FGenericConverterMessage> EmitterMessages;

	UPROPERTY()
	bool bEnabled;

	UNiagaraNodeFunctionCall* PastedFunctionCallNode;

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
	bool SetParameter(FString ParameterName, UNiagaraScriptConversionContextInput* ParameterInput, bool bInHasEditCondition = false, bool bInEditConditionValue = false);

	/**
	 * Log a message to the stack and the niagara message log for the module associated with this script.
	 * @param Message		The message string to display.
	 * @param Severity		The severity of the message.
	 * @param bIsVerbose	Whether the message is verbose and should be displayed conditionally.
	 */
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void Log(FString Message, ENiagaraMessageSeverity Severity, bool bIsVerbose = false);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void SetEnabled(bool bInEnabled) {bEnabled = bInEnabled;};

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	bool GetEnabled() const {return bEnabled;};

	const TArray<const UNiagaraClipboardFunctionInput*>& GetClipboardFunctionInputs() { return FunctionInputs; };

	UNiagaraScript* GetScript() { return Script; };

	const TArray<FGenericConverterMessage>& GetStackMessages() const {return StackMessages;};

private:
	// Execution category to add this script to when it is finalized to a system or emitter.
	UPROPERTY()
	EScriptExecutionCategory TargetExecutionCategory;

	UPROPERTY()
	TArray<const UNiagaraClipboardFunctionInput*> FunctionInputs;

	UPROPERTY()
	UNiagaraScript* Script;

	UPROPERTY()
	TArray<FGenericConverterMessage> StackMessages;

	// Map of input variable names to their type defs for verifying inputs.
	UPROPERTY()
	TMap<FString, FNiagaraTypeDefinition> InputNameToTypeDefMap;

	UPROPERTY()
	bool bEnabled;
};

/** Wrapper for setting the value on a parameter of a UNiagaraScript, applied through a UNiagaraScriptConversionContext. */
UCLASS(BlueprintInternalUseOnly)
class UNiagaraScriptConversionContextInput : public UObject
{
	GENERATED_BODY()

public:
	UNiagaraScriptConversionContextInput() {};

	UFUNCTION()
	void Init(UNiagaraClipboardFunctionInput* InClipboardFunctionInput, const ENiagaraScriptInputType InInputType, const FNiagaraTypeDefinition& InTypeDefinition);

	UPROPERTY()
	UNiagaraClipboardFunctionInput* ClipboardFunctionInput;

	UPROPERTY(BlueprintReadOnly, Category = StaticValue)
	ENiagaraScriptInputType InputType;

	UPROPERTY()
	FNiagaraTypeDefinition TypeDefinition;

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


	// Cascade Emitter and ParticleLodLevel Getters
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static TArray<UParticleEmitter*> GetCascadeSystemEmitters(const UParticleSystem* System);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static UParticleLODLevel* GetCascadeEmitterLodLevel(UParticleEmitter* Emitter, const int32 Idx);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static bool GetLodLevelIsEnabled(UParticleLODLevel* LodLevel);

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
	static UNiagaraScriptConversionContextInput* CreateScriptInputLinkedParameter(FString ParameterNameString, ENiagaraScriptInputType InputType);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraScriptConversionContextInput* CreateScriptInputFloat(float Value);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraScriptConversionContextInput* CreateScriptInputVec2(FVector2D Value);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraScriptConversionContextInput* CreateScriptInputVector(FVector Value);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraScriptConversionContextInput* CreateScriptInputStruct(UUserDefinedStruct* Value);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraScriptConversionContextInput* CreateScriptInputEnum(UUserDefinedEnum* Value);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraScriptConversionContextInput* CreateScriptInputInt(int32 Value);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraScriptConversionContextInput* CreateScriptInputDynamic(UNiagaraScriptConversionContext* Value, ENiagaraScriptInputType InputType);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraScriptConversionContextInput* CreateScriptInputDI(UNiagaraDataInterface* Value);


	// Niagara Renderer Properties Helpers
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraRibbonRendererProperties* CreateRibbonRendererProperties();

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraMeshRendererProperties* CreateMeshRendererProperties();


	// Niagara DI Helpers
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraDataInterfaceCurve* CreateFloatCurveDI(TArray<FRichCurveKeyBP> Keys);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraDataInterfaceVector2DCurve* CreateVec2CurveDI(TArray<FRichCurveKeyBP> X_Keys, TArray<FRichCurveKeyBP> Y_Keys);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraDataInterfaceVectorCurve* CreateVec3CurveDI(
		TArray<FRichCurveKeyBP> X_Keys,
		TArray<FRichCurveKeyBP> Y_Keys,
		TArray<FRichCurveKeyBP> Z_Keys
	);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static UNiagaraDataInterfaceVector4Curve* CreateVec4CurveDI(
	TArray<FRichCurveKeyBP> X_Keys,
	TArray<FRichCurveKeyBP> Y_Keys,
	TArray<FRichCurveKeyBP> Z_Keys,
	TArray<FRichCurveKeyBP> W_Keys
	);


	// Niagara System and Emitter Helpers
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static UNiagaraSystemConversionContext* CreateSystemConversionContext(UNiagaraSystem* InSystem);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleTypeDataGpuProps(UParticleModuleTypeDataGpu* ParticleModule);

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
		, UDistribution*& OutRate
		, UDistribution*& OutRateScale
		, TEnumAsByte<EParticleBurstMethod>& OutBurstMethod
		, TArray<FParticleBurstBlueprint>& OutBurstList
		, UDistribution*& OutBurstScale
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
		, float& OutAlphaThreshold
	);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleColorProps(UParticleModuleColor* ParticleModule, UDistribution*& OutStartColor, UDistribution*& OutStartAlpha, bool& bOutClampAlpha);

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

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleLocationPrimitiveSphereProps(UParticleModuleLocationPrimitiveSphere* ParticleModule, UDistribution*& OutStartRadius);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleMeshRotationProps(UParticleModuleMeshRotation* ParticleModule, UDistribution*& OutStartRotation, bool& bOutInheritParentRotation);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleCollisionProps(
		UParticleModuleCollision* ParticleModule
		, UDistribution*& OutDampingFactor
		, UDistribution*& OutDampingFactorRotation
		, UDistribution*& OutMaxCollisions
		, TEnumAsByte<EParticleCollisionComplete>& OutCollisionCompleteOption
		, TArray<TEnumAsByte<EObjectTypeQuery>>& OutCollisionTypes
		, bool& bOutApplyPhysics
		, bool& bOutIgnoreTriggerVolumes
		, UDistribution*& OutParticleMass
		, float& OutDirScalar
		, bool& bOutPawnsDoNotDecrementCount
		, bool& bOutOnlyVerticalNormalsDecrementCount
		, float& OutVerticalFudgeFactor
		, UDistribution*& OutDelayAmount
		, bool& bOutDropDetail
		, bool& bOutCollideOnlyIfVisible
		, bool& bOutIgnoreSourceActor
		, float& OutMaxCollisionDistance
	);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleSizeScaleBySpeedProps(UParticleModuleSizeScaleBySpeed* ParticleModule, FVector2D& OutSpeedScale, FVector2D& OutMaxScale);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleVectorFieldLocalProps(
		  UParticleModuleVectorFieldLocal* ParticleModule
		, UVectorField* OutVectorField
		, FVector& OutRelativeTranslation
		, FRotator& OutRelativeRotation
		, FVector& OutRelativeScale3D
		, float& OutIntensity
		, float& OutTightness
		, bool& bOutIgnoreComponentTransform
		, bool& bOutTileX
		, bool& bOutTileY
		, bool& bOutTileZ
		, bool& bOutUseFixDT
	);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetParticleModuleVectorFieldRotationRateProps(UParticleModuleVectorFieldRotationRate* ParticleModule, FVector& OutRotationRate);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void GetParticleModuleOrbitProps(
		UParticleModuleOrbit* ParticleModule
		, TEnumAsByte<enum EOrbitChainMode>& OutChainMode
		, UDistribution*& OutOffsetAmount
		, FOrbitOptionsBP& OutOffsetOptions
		, UDistribution*& OutRotationAmount
		, FOrbitOptionsBP& OutRotationOptions
		, UDistribution*& OutRotationRateAmount
		, FOrbitOptionsBP& OutRotationRateOptions
	);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void GetParticleModuleSizeMultiplyLifeProps(
		UParticleModuleSizeMultiplyLife* ParticleModule
		, UDistribution*& OutLifeMultiplier
		, bool& OutMultiplyX
		, bool& OutMultiplyY
		, bool& OutMultiplyZ
	);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void GetParticleModuleColorScaleOverLifeProps(
		UParticleModuleColorScaleOverLife* ParticleModule
		, UDistribution*& OutColorScaleOverLife
		, UDistribution*& OutAlphaScaleOverLife
		, bool& bOutEmitterTime
	);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void GetParticleModuleRotationProps(UParticleModuleRotation* ParticleModule, UDistribution*& OutStartRotation);
	
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void GetParticleModuleRotationRateProps(UParticleModuleRotationRate* ParticleModule, UDistribution*& OutStartRotationRate);
	
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void GetParticleModuleSubUVProps(
		UParticleModuleSubUV* ParticleModule
		, USubUVAnimation*& OutAnimation
		, UDistribution*& OutSubImageIndex
		, bool& bOutUseRealTime
	);
	
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void GetParticleModuleCameraOffsetProps(
		UParticleModuleCameraOffset* ParticleModule
		, UDistribution*& OutCameraOffset
		, bool& bOutSpawnTimeOnly
		, TEnumAsByte<EParticleCameraOffsetUpdateMethod>& OutUpdateMethod
	);
	
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void GetParticleModuleSubUVMovieProps(
		UParticleModuleSubUVMovie* ParticleModule
		, bool& bOutUseEmitterTime
		, UDistribution*& OutFrameRate
		, int32& OutStartingFrame
	);
	
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void GetParticleModuleParameterDynamicProps(UParticleModuleParameterDynamic* ParticleModule, TArray<FEmitterDynamicParameterBP>& OutDynamicParams, bool& bOutUsesVelocity);
	
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")	
	static void GetParticleModuleAccelerationDragProps(UParticleModuleAccelerationDrag* ParticleModule, UDistribution*& OutDragCoefficientRaw);
	
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void GetParticleModuleAccelerationProps(UParticleModuleAcceleration* ParticleModule, UDistribution*& OutAcceleration, bool& bOutApplyOwnerScale);


	// Cascade Distribution Getters
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetDistributionMinMaxValues(
		UDistribution* Distribution,
		bool& bOutSuccess,
		FVector& OutMinValue,
		FVector& OutMaxValue);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetDistributionType(
		UDistribution* Distribution
		, EDistributionType& OutDistributionType
		, EDistributionValueType& OutCascadeDistributionValueType);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetFloatDistributionConstValues(UDistributionFloatConstant* Distribution, float& OutConstFloat);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetVectorDistributionConstValues(UDistributionVectorConstant* Distribution, FVector& OutConstVector);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetFloatDistributionUniformValues(UDistributionFloatUniform* Distribution, float& OutMin, float& OutMax);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetVectorDistributionUniformValues(UDistributionVectorUniform* Distribution, FVector& OutMin, FVector& OutMax);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetFloatDistributionConstCurveValues(UDistributionFloatConstantCurve* Distribution, FInterpCurveFloat& OutInterpCurveFloat);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetVectorDistributionConstCurveValues(UDistributionVectorConstantCurve* Distribution, FInterpCurveVector& OutInterpCurveVector);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetFloatDistributionUniformCurveValues(UDistributionFloatUniformCurve* Distribution, FInterpCurveVector2D& OutInterpCurveVector2D);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetVectorDistributionUniformCurveValues(UDistributionVectorUniformCurve* Distribution, FInterpCurveTwoVectors& OutInterpCurveTwoVectors);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetFloatDistributionParameterValues(UDistributionFloatParameterBase* Distribution, FName& OutParameterName, float& OutMinInput, float& OutMaxInput, float& OutMinOutput, float& OutMaxOutput);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void GetVectorDistributionParameterValues(UDistributionVectorParameterBase* Distribution, FName& OutParameterName, FVector& OutMinInput, FVector& OutMaxInput, FVector& OutMinOutput, FVector& OutMaxOutput);


	// Cascade curve helpers
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static TArray<FRichCurveKeyBP> KeysFromInterpCurveFloat(FInterpCurveFloat Curve);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static TArray<FRichCurveKeyBP> KeysFromInterpCurveVector(FInterpCurveVector Curve, int32 ComponentIdx);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static TArray<FRichCurveKeyBP> KeysFromInterpCurveVector2D(FInterpCurveVector2D Curve, int32 ComponentIdx);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static TArray<FRichCurveKeyBP> KeysFromInterpCurveTwoVectors(FInterpCurveTwoVectors Curve, int32 ComponentIdx);


	// Maps from python addressable FGuid to non-blueprint types
	static TMap<FGuid, TSharedPtr<FNiagaraEmitterHandleViewModel>> GuidToNiagaraEmitterHandleViewModelMap;
	static TMap<FGuid, TSharedPtr<FNiagaraSystemViewModel>> GuidToNiagaraSystemViewModelMap;

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void Cleanup();


	// Code only utilities
	static FName GetNiagaraScriptInputTypeName(ENiagaraScriptInputType InputType);
};
