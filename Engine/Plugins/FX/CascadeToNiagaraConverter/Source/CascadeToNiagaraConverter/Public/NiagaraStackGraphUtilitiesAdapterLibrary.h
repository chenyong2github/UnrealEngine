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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////	Wrapper Structs																							  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

USTRUCT(BlueprintInternalUseOnly)
struct FParticleModuleSpawnProps
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Properties")
	UDistributionFloat* Rate;

	/** The scalar to apply to the rate. */
	UPROPERTY(BlueprintReadWrite, Category = "Properties")
	UDistributionFloat* RateScale;

	/** The method to utilize when burst-emitting particles. */
	UPROPERTY(BlueprintReadWrite, Category = "Properties")
	TEnumAsByte<EParticleBurstMethod> ParticleBurstMethod;

	/** The array of burst entries. */
	UPROPERTY(BlueprintReadWrite, Category = "Properties")
	TArray<FParticleBurstBlueprint> BurstList;

	/** Scale all burst entries by this amount. */
	UPROPERTY(BlueprintReadWrite, Category = "Properties")
	UDistributionFloat* BurstScale;

	/**	If true, the SpawnRate will be scaled by the global CVar r.EmitterSpawnRateScale */
	UPROPERTY(BlueprintReadWrite, Category = "Properties")
	uint32 bApplyGlobalSpawnRateScale : 1;

	UPROPERTY(BlueprintReadWrite, Category = "Properties")
	bool bProcessSpawnRate;

	UPROPERTY(BlueprintReadWrite, Category = "Properties")
	bool bProcessSpawnBurst;
};



USTRUCT(BlueprintInternalUseOnly)
struct FParticleEmitterProps
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Properties")
	UDistributionFloat* Rate;
};


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
/////	Wrapper Classes																							  /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UCLASS(BlueprintInternalUseOnly)
class UNiagaraEmitterConversionContext : public UObject
{
	GENERATED_BODY()

public:
	UNiagaraEmitterConversionContext() {};
	
	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	UNiagaraScriptConversionContext* FindOrAddScript(FString ScriptNameString, FAssetData NiagaraScriptAssetData);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void AddRenderer(UNiagaraRendererProperties* NewRendererProperties);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void FinalizeAddedScripts();

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
};

UCLASS(BlueprintInternalUseOnly)
class UNiagaraScriptConversionContext : public UObject
{
	GENERATED_BODY()

public:
	UNiagaraScriptConversionContext() {};

	void Init(const FAssetData& InNiagaraScriptAssetData);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	bool SetParameter(FString ParameterName, FNiagaraScriptContextInput ParameterInput);

	const TArray<const UNiagaraClipboardFunctionInput*>& GetClipboardFunctionInputs() { return FunctionInputs; };

	UNiagaraScript* GetScript() { return Script; };

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


	// Niagara System and Emitter Helpers
	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static FGuid CreateSystemViewModelForSystem(UNiagaraSystem* InSystem);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static UNiagaraEmitterConversionContext* AddEmptyEmitterToSystem(FGuid TargetSystemViewModelGuid, FString NewEmitterNameString);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void AddNiagaraEmitterStackMessage(UNiagaraEmitterConversionContext* EmitterContext, FString Message);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static void AddNiagaraScriptStackMessage(UNiagaraScriptConversionContext* ScriptContext, FString Message);


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
	//static void CopyCascadeDistributionCurveToNiagaraCurve(UDistribution* Distribution, )

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
