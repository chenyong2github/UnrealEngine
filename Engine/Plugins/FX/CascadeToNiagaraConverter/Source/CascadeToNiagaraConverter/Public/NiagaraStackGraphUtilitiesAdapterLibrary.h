// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NiagaraEditor/Public/ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "AssetData.h"
#include "Particles/ParticleEmitter.h"
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
struct FCascadeSpriteRendererProps
{
	GENERATED_BODY()

	/** The material to utilize for the emitter at this LOD level.						*/
	UPROPERTY(EditAnywhere, Category = Emitter)
	class UMaterialInterface* Material;

	/**
	 *	The screen alignment to utilize for the emitter at this LOD level.
	 *	One of the following:
	 *	PSA_FacingCameraPosition - Faces the camera position, but is not dependent on the camera rotation.
	 *								This method produces more stable particles under camera rotation.
	 *	PSA_Square			- Uniform scale (via SizeX) facing the camera
	 *	PSA_Rectangle		- Non-uniform scale (via SizeX and SizeY) facing the camera
	 *	PSA_Velocity		- Orient the particle towards both the camera and the direction
	 *						  the particle is moving. Non-uniform scaling is allowed.
	 *	PSA_TypeSpecific	- Use the alignment method indicated in the type data module.
	 *	PSA_FacingCameraDistanceBlend - Blends between PSA_FacingCameraPosition and PSA_Square over specified distance.
	 */
	UPROPERTY(EditAnywhere, Category = Emitter)
	TEnumAsByte<enum EParticleScreenAlignment> ScreenAlignment;


// 	UPROPERTY(BlueprintReadWrite, Category = "Properties")
// 	FVector2D PivotInUVSpace;

	/** The number of sub-images horizontally in the texture							*/
	UPROPERTY(EditAnywhere, Category = SubUV)
	int32 SubImages_Horizontal;

	/** The number of sub-images vertically in the texture								*/
	UPROPERTY(EditAnywhere, Category = SubUV)
	int32 SubImages_Vertical;

	/**
	 *	The sorting mode to use for this emitter.
	 *	PSORTMODE_None				- No sorting required.
	 *	PSORTMODE_ViewProjDepth		- Sort by view projected depth of the particle.
	 *	PSORTMODE_DistanceToView	- Sort by distance of particle to view in world space.
	 *	PSORTMODE_Age_OldestFirst	- Sort by age, oldest drawn first.
	 *	PSORTMODE_Age_NewestFirst	- Sort by age, newest drawn first.
	 *
	 */
	UPROPERTY(EditAnywhere, Category = Emitter)
	TEnumAsByte<enum EParticleSortMode> SortMode;

	/**
	 *	The interpolation method to used for the SubUV image selection.
	 *	One of the following:
	 *	PSUVIM_None			- Do not apply SubUV modules to this emitter.
	 *	PSUVIM_Linear		- Smoothly transition between sub-images in the given order,
	 *						  with no blending between the current and the next
	 *	PSUVIM_Linear_Blend	- Smoothly transition between sub-images in the given order,
	 *						  blending between the current and the next
	 *	PSUVIM_Random		- Pick the next image at random, with no blending between
	 *						  the current and the next
	 *	PSUVIM_Random_Blend	- Pick the next image at random, blending between the current
	 *						  and the next
	 */
	UPROPERTY(EditAnywhere, Category = SubUV)
	TEnumAsByte<EParticleSubUVInterpMethod> InterpolationMethod;

	/** If true, removes the HMD view roll (e.g. in VR) */
	UPROPERTY(EditAnywhere, Category = Emitter, meta = (DisplayName = "Remove HMD Roll"))
	uint8 bRemoveHMDRoll : 1;

	/** The distance at which PSA_FacingCameraDistanceBlend	is fully PSA_Square */
	UPROPERTY(EditAnywhere, Category = Emitter, meta = (UIMin = "0", DisplayAfter = "ScreenAlignment"))
	float MinFacingCameraBlendDistance;

	/** The distance at which PSA_FacingCameraDistanceBlend	is fully PSA_FacingCameraPosition */
	UPROPERTY(EditAnywhere, Category = Emitter, meta = (UIMin = "0", DisplayAfter = "MinFacingCameraBlendDistance"))
	float MaxFacingCameraBlendDistance;

	/**
	* Texture to generate bounding geometry from.
	*/
	UPROPERTY(EditAnywhere, Category = ParticleCutout)
	UTexture2D* CutoutTexture;

	/**
	* More bounding vertices results in reduced overdraw, but adds more triangle overhead.
	* The eight vertex mode is best used when the SubUV texture has a lot of space to cut out that is not captured by the four vertex version,
	* and when the particles using the texture will be few and large.
	*/
	UPROPERTY(EditAnywhere, Category = ParticleCutout)
	TEnumAsByte<enum ESubUVBoundingVertexCount> BoundingMode;

	UPROPERTY(EditAnywhere, Category = ParticleCutout)
	TEnumAsByte<enum EOpacitySourceMode> OpacitySourceMode;
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
	void AddScript(
		UNiagaraScriptConversionContext* ScriptConversionContext
		, EScriptExecutionCategory TargetScriptExecutionCategory
		, int32 TargetIndex);

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	void AddRenderer(UNiagaraRendererProperties* NewRendererProperties);

	void Init(UNiagaraEmitter* InEmitter, FGuid InEmitterHandleViewModelGuid)
	{
		Emitter = InEmitter;
		EmitterHandleViewModelGuid = InEmitterHandleViewModelGuid;
	};

private:
	UPROPERTY()
	UNiagaraEmitter* Emitter;

	UPROPERTY()
	FGuid EmitterHandleViewModelGuid;
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

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static FParticleModuleSpawnProps GetCascadeModuleSpawnProps(UParticleModuleSpawn* ParticleModuleSpawn);

	UFUNCTION(BlueprintCallable, meta = (ScriptMethod), Category = "FXConverterUtilities")
	static FCascadeSpriteRendererProps GetCascadeSpriteRendererProps(UParticleModuleRequired* ParticleModuleRequired);

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
// 	void UFXConverterUtilitiesLibrary::GetFloatDistributionUniformValues(UDistributionFloatUniform* Distribution);
// 	void UFXConverterUtilitiesLibrary::GetFloatDistributionUniformCurveValues(UDistributionFloatUniformCurve* Distribution);
// 	void UFXConverterUtilitiesLibrary::GetFloatDistributionConstCurveValues(UDistributionFloatParticleParameter* Distribution);


	// Maps from python addressable FGuid to non-blueprint types
	static TMap<FGuid, TSharedPtr<FNiagaraEmitterHandleViewModel>> GuidToNiagaraEmitterHandleViewModelMap;
	static TMap<FGuid, TSharedPtr<FNiagaraSystemViewModel>> GuidToNiagaraSystemViewModelMap;

	UFUNCTION(BlueprintCallable, Category = "FXConverterUtilities")
	static void Cleanup();
};
