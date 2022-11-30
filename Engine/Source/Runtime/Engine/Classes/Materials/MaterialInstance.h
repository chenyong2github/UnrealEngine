// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "RenderCommandFence.h"
#include "HAL/ThreadSafeBool.h"
#include "Materials/MaterialInterface.h"
#include "StaticParameterSet.h"
#include "MaterialShared.h"
#include "MaterialCachedData.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceBasePropertyOverrides.h"
#include "Misc/App.h"
#if WITH_CHAOS
#include "Physics/PhysicsInterfaceCore.h"
#endif
#include "MaterialInstance.generated.h"

class ITargetPlatform;
class UPhysicalMaterial;
class USubsurfaceProfile;
class UTexture;

//
// Forward declarations.
//
class FMaterialShaderMap;
class FMaterialShaderMapId;
class FSHAHash;

/** Editable scalar parameter. */

USTRUCT()
struct FScalarParameterAtlasInstanceData
{
	GENERATED_BODY()
public:
	UPROPERTY()
	bool bIsUsedAsAtlasPosition;

	UPROPERTY()
	TSoftObjectPtr<class UCurveLinearColor> Curve;

	UPROPERTY()
	TSoftObjectPtr<class UCurveLinearColorAtlas> Atlas;

	bool operator==(const FScalarParameterAtlasInstanceData& Other) const
	{
		return
			bIsUsedAsAtlasPosition == Other.bIsUsedAsAtlasPosition &&
			Curve == Other.Curve &&
			Atlas == Other.Atlas;
	}
	bool operator!=(const FScalarParameterAtlasInstanceData& Other) const
	{
		return !((*this) == Other);
	}

	FScalarParameterAtlasInstanceData()
		: bIsUsedAsAtlasPosition(false)
	{
	}
};

USTRUCT(BlueprintType)
struct FScalarParameterValue
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FName ParameterName_DEPRECATED;

	UPROPERTY()
	FScalarParameterAtlasInstanceData AtlasData;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=ScalarParameterValue)
	FMaterialParameterInfo ParameterInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=ScalarParameterValue)
	float ParameterValue;

	UPROPERTY()
	FGuid ExpressionGUID;

	FScalarParameterValue()
		: ParameterValue(0)
	{
	}

	bool operator==(const FScalarParameterValue& Other) const
	{
		return
			ParameterInfo == Other.ParameterInfo &&
			ParameterValue == Other.ParameterValue &&
			ExpressionGUID == Other.ExpressionGUID;
	}
	bool operator!=(const FScalarParameterValue& Other) const
	{
		return !((*this) == Other);
	}
	
	typedef float ValueType;
	static ValueType GetValue(const FScalarParameterValue& Parameter) { return Parameter.ParameterValue; }
};

/** Editable vector parameter. */
USTRUCT(BlueprintType)
struct FVectorParameterValue
{
	GENERATED_USTRUCT_BODY()
		
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FName ParameterName_DEPRECATED;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=VectorParameterValue)
	FMaterialParameterInfo ParameterInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=VectorParameterValue)
	FLinearColor ParameterValue;

	UPROPERTY()
	FGuid ExpressionGUID;

	FVectorParameterValue()
		: ParameterValue(ForceInit)
	{
	}

	bool operator==(const FVectorParameterValue& Other) const
	{
		return
			ParameterInfo == Other.ParameterInfo &&
			ParameterValue == Other.ParameterValue &&
			ExpressionGUID == Other.ExpressionGUID;
	}
	bool operator!=(const FVectorParameterValue& Other) const
	{
		return !((*this) == Other);
	}
	
	typedef FLinearColor ValueType;
	static ValueType GetValue(const FVectorParameterValue& Parameter) { return Parameter.ParameterValue; }
};

/** Editable texture parameter. */
USTRUCT(BlueprintType)
struct FTextureParameterValue
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FName ParameterName_DEPRECATED;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=TextureParameterValue)
	FMaterialParameterInfo ParameterInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=TextureParameterValue)
	class UTexture* ParameterValue;

	UPROPERTY()
	FGuid ExpressionGUID;

	FTextureParameterValue()
		: ParameterValue(NULL)
	{
	}

	bool operator==(const FTextureParameterValue& Other) const
	{
		return
			ParameterInfo == Other.ParameterInfo &&
			ParameterValue == Other.ParameterValue &&
			ExpressionGUID == Other.ExpressionGUID;
	}
	bool operator!=(const FTextureParameterValue& Other) const
	{
		return !((*this) == Other);
	}

	typedef const UTexture* ValueType;
	static ValueType GetValue(const FTextureParameterValue& Parameter) { return Parameter.ParameterValue; }
};

/** Editable runtime virtual texture parameter. */
USTRUCT(BlueprintType)
struct FRuntimeVirtualTextureParameterValue
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RuntimeVirtualTextureParameterValue)
	FMaterialParameterInfo ParameterInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RuntimeVirtualTextureParameterValue)
	class URuntimeVirtualTexture* ParameterValue;

	UPROPERTY()
	FGuid ExpressionGUID;

	FRuntimeVirtualTextureParameterValue()
		: ParameterValue(NULL)
	{
	}

	bool operator==(const FRuntimeVirtualTextureParameterValue& Other) const
	{
		return
			ParameterInfo == Other.ParameterInfo &&
			ParameterValue == Other.ParameterValue &&
			ExpressionGUID == Other.ExpressionGUID;
	}
	bool operator!=(const FRuntimeVirtualTextureParameterValue& Other) const
	{
		return !((*this) == Other);
	}

	typedef const URuntimeVirtualTexture* ValueType;
	static ValueType GetValue(const FRuntimeVirtualTextureParameterValue& Parameter) { return Parameter.ParameterValue; }
};

/** Editable font parameter. */
USTRUCT(BlueprintType)
struct FFontParameterValue
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FName ParameterName_DEPRECATED;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=FontParameterValue)
	FMaterialParameterInfo ParameterInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=FontParameterValue)
	class UFont* FontValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=FontParameterValue)
	int32 FontPage;

	UPROPERTY()
	FGuid ExpressionGUID;

	FFontParameterValue()
		: FontValue(nullptr)
		, FontPage(0)
	{
	}

	bool operator==(const FFontParameterValue& Other) const
	{
		return
			ParameterInfo == Other.ParameterInfo &&
			FontValue == Other.FontValue &&
			FontPage == Other.FontPage &&
			ExpressionGUID == Other.ExpressionGUID;
	}
	bool operator!=(const FFontParameterValue& Other) const
	{
		return !((*this) == Other);
	}
	
	typedef const UTexture* ValueType;
	static ValueType GetValue(const FFontParameterValue& Parameter);
};

template<class T>
bool CompareValueArraysByExpressionGUID(const TArray<T>& InA, const TArray<T>& InB)
{
	if (InA.Num() != InB.Num())
	{
		return false;
	}
	if (!InA.Num())
	{
		return true;
	}
	TArray<T> AA(InA);
	TArray<T> BB(InB);
	AA.Sort([](const T& A, const T& B) { return B.ExpressionGUID < A.ExpressionGUID; });
	BB.Sort([](const T& A, const T& B) { return B.ExpressionGUID < A.ExpressionGUID; });
	return AA == BB;
}


UCLASS(abstract, BlueprintType,MinimalAPI)
class UMaterialInstance : public UMaterialInterface
{
	GENERATED_UCLASS_BODY()

	/** Physical material to use for this graphics material. Used for sounds, effects etc.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=MaterialInstance)
	class UPhysicalMaterial* PhysMaterial;

	/** Physical material map used with physical material mask, when it exists.*/
	UPROPERTY(EditAnywhere, Category = PhysicalMaterialMask)
	class UPhysicalMaterial* PhysicalMaterialMap[EPhysicalMaterialMaskColor::MAX];

	/** Parent material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=MaterialInstance, AssetRegistrySearchable)
	class UMaterialInterface* Parent;

	/**
	 * Delegate for custom static parameters getter.
	 *
	 * @param OutStaticParameterSet Parameter set to append.
	 * @param Material Material instance to collect parameters.
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FCustomStaticParametersGetterDelegate, FStaticParameterSet&, UMaterialInstance*);

	/**
	 * Delegate for custom static parameters updater.
	 *
	 * @param StaticParameterSet Parameter set to update.
	 * @param Material Material to update.
	 *
	 * @returns True if any parameter been updated. False otherwise.
	 */
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FCustomParameterSetUpdaterDelegate, FStaticParameterSet&, UMaterial*);

#if WITH_EDITORONLY_DATA
	// Custom static parameters getter delegate.
	ENGINE_API static FCustomStaticParametersGetterDelegate CustomStaticParametersGetters;

	// An array of custom parameter set updaters.
	ENGINE_API static TArray<FCustomParameterSetUpdaterDelegate> CustomParameterSetUpdaters;
#endif // WITH_EDITORONLY_DATA

	/**
	 * Gets static parameter set for this material.
	 *
	 * @returns Static parameter set.
	 */
	ENGINE_API const FStaticParameterSet& GetStaticParameters() const;

	/**
	 * Indicates whether the instance has static permutation resources (which are required when static parameters are present) 
	 * Read directly from the rendering thread, can only be modified with the use of a FMaterialUpdateContext.
	 * When true, StaticPermutationMaterialResources will always be valid and non-null.
	 */
	UPROPERTY()
	uint8 bHasStaticPermutationResource:1;

	/** Defines if SubsurfaceProfile from this instance is used or it uses the parent one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = MaterialInstance)
	uint8 bOverrideSubsurfaceProfile:1;

	uint8 TwoSided : 1;
	uint8 DitheredLODTransition : 1;
	uint8 bCastDynamicShadowAsMasked : 1;
	uint8 bIsShadingModelFromMaterialExpression : 1;

	TEnumAsByte<EBlendMode> BlendMode;

	//Cached copies of the base property overrides or the value from the parent to avoid traversing the parent chain for each access.
	float OpacityMaskClipValue;

	FORCEINLINE bool GetReentrantFlag() const
	{
#if WITH_EDITOR
		return ReentrantFlag[IsInGameThread() ? 0 : 1];
#else
		return false;
#endif
	}

	FORCEINLINE void SetReentrantFlag(const bool bValue)
	{
#if WITH_EDITOR
		ReentrantFlag[IsInGameThread() ? 0 : 1] = bValue;
#endif
	}

	/** Scalar parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=MaterialInstance)
	TArray<struct FScalarParameterValue> ScalarParameterValues;

	/** Vector parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=MaterialInstance)
	TArray<struct FVectorParameterValue> VectorParameterValues;

	/** Texture parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=MaterialInstance)
	TArray<struct FTextureParameterValue> TextureParameterValues;

	/** RuntimeVirtualTexture parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = MaterialInstance)
	TArray<struct FRuntimeVirtualTextureParameterValue> RuntimeVirtualTextureParameterValues;

	/** Font parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=MaterialInstance)
	TArray<struct FFontParameterValue> FontParameterValues;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bOverrideBaseProperties_DEPRECATED;
#endif

	UPROPERTY(EditAnywhere, Category=MaterialInstance)
	struct FMaterialInstanceBasePropertyOverrides BasePropertyOverrides;

#if STORE_ONLY_ACTIVE_SHADERMAPS
	// Relative offset to the beginning of the package containing this
	uint32 OffsetToFirstResource;
#endif

	FMaterialShadingModelField ShadingModels;

#if WITH_EDITOR
	/** Flag to detect cycles in the material instance graph, this is only used at content creation time where the hierarchy can be changed. */
	bool ReentrantFlag[2];
#endif

	/** 
	 * FMaterialRenderProxy derivative that represent this material instance to the renderer, when the renderer needs to fetch parameter values. 
	 */
	class FMaterialInstanceResource* Resource;

	ENGINE_API virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	ENGINE_API virtual float GetTextureDensity(FName TextureName, const struct FMeshUVChannelInfo& UVChannelData) const override;

	ENGINE_API bool Equivalent(const UMaterialInstance* CompareTo) const;

private:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FGuid> ReferencedTextureGuids;
#endif // WITH_EDITORONLY_DATA

	/** Static parameter values that are overridden in this instance. */
	UPROPERTY()
	FStaticParameterSet StaticParameters;

	UPROPERTY()
	FMaterialCachedParameters CachedLayerParameters;

	/**
	 * Cached texture references from all expressions in the material (including nested functions).
	 * This is used to link uniform texture expressions which were stored in the DDC with the UTextures that they reference.
	 */
	UPROPERTY()
	TArray<UObject*> CachedReferencedTextures;

#if WITH_EDITOR
	mutable TOptional<FStaticParameterSet> CachedStaticParameterValues;
	mutable uint8 AllowCachingStaticParameterValuesCounter = 0;
#endif // WITH_EDITOR

	/** Inline material resources serialized from disk. To be processed on game thread in PostLoad. */
	TArray<FMaterialResource> LoadedMaterialResources;

	/** 
	 * Material resources used for rendering this material instance, in the case of static parameters being present.
	 * These will always be valid and non-null when bHasStaticPermutationResource is true,
	 * But only the entries affected by CacheResourceShadersForRendering will be valid for rendering.
	 * There need to be as many entries in this array as can be used simultaneously for rendering.  
	 * For example the material instance needs to support being rendered at different quality levels and feature levels within the same process.
	 */
	TArray<FMaterialResource*> StaticPermutationMaterialResources;
#if WITH_EDITOR
	/** Material resources being cached for cooking. */
	TMap<const class ITargetPlatform*, TArray<FMaterialResource*>> CachedMaterialResourcesForCooking;
#endif
	/** Flag used to guarantee that the RT is finished using various resources in this UMaterial before cleanup. */
	FThreadSafeBool ReleasedByRT;

public:
	// Begin UMaterialInterface interface.
	virtual ENGINE_API UMaterial* GetMaterial() override;
	virtual ENGINE_API const UMaterial* GetMaterial() const override;
	virtual ENGINE_API const UMaterial* GetMaterial_Concurrent(TMicRecursionGuard RecursionGuard = TMicRecursionGuard()) const override;
	virtual ENGINE_API FMaterialResource* AllocatePermutationResource();
	virtual ENGINE_API FMaterialResource* GetMaterialResource(ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type QualityLevel = EMaterialQualityLevel::Num) override;
	virtual ENGINE_API const FMaterialResource* GetMaterialResource(ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type QualityLevel = EMaterialQualityLevel::Num) const override;

#if WITH_EDITOR
	virtual ENGINE_API bool GetScalarParameterSliderMinMax(const FHashedMaterialParameterInfo& ParameterInfo, float& OutSliderMin, float& OutSliderMax) const override;
#endif
	virtual ENGINE_API bool GetScalarParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue, bool bOveriddenOnly = false) const override;
#if WITH_EDITOR
	virtual ENGINE_API bool IsScalarParameterUsedAsAtlasPosition(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, TSoftObjectPtr<class UCurveLinearColor>& Curve, TSoftObjectPtr<class UCurveLinearColorAtlas>& Atlas) const override;
#endif
	virtual ENGINE_API bool GetVectorParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue, bool bOveriddenOnly = false) const override;
#if WITH_EDITOR
	virtual ENGINE_API bool IsVectorParameterUsedAsChannelMask(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue) const override;
	virtual ENGINE_API bool GetVectorParameterChannelNames(const FHashedMaterialParameterInfo& ParameterInfo, FParameterChannelNames& OutValue) const override;
#endif
	virtual ENGINE_API bool GetTextureParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, class UTexture*& OutValue, bool bOveriddenOnly = false) const override;
	virtual ENGINE_API bool GetRuntimeVirtualTextureParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, class URuntimeVirtualTexture*& OutValue, bool bOveriddenOnly = false) const override;
#if WITH_EDITOR
	virtual ENGINE_API bool GetTextureParameterChannelNames(const FHashedMaterialParameterInfo& ParameterInfo, FParameterChannelNames& OutValue) const override;
#endif
	virtual ENGINE_API bool GetFontParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, class UFont*& OutFontValue, int32& OutFontPage, bool bOveriddenOnly = false) const override;
	virtual ENGINE_API void GetUsedTextures(TArray<UTexture*>& OutTextures, EMaterialQualityLevel::Type QualityLevel, bool bAllQualityLevels, ERHIFeatureLevel::Type FeatureLevel, bool bAllFeatureLevels) const override;
	virtual ENGINE_API void GetUsedTexturesAndIndices(TArray<UTexture*>& OutTextures, TArray< TArray<int32> >& OutIndices, EMaterialQualityLevel::Type QualityLevel, ERHIFeatureLevel::Type FeatureLevel) const;
	virtual ENGINE_API void OverrideTexture(const UTexture* InTextureToOverride, UTexture* OverrideTexture, ERHIFeatureLevel::Type InFeatureLevel) override;
	virtual ENGINE_API void OverrideVectorParameterDefault(const FHashedMaterialParameterInfo& ParameterInfo, const FLinearColor& Value, bool bOverride, ERHIFeatureLevel::Type FeatureLevel) override;
	virtual ENGINE_API void OverrideScalarParameterDefault(const FHashedMaterialParameterInfo& ParameterInfo, float Value, bool bOverride, ERHIFeatureLevel::Type FeatureLevel) override;
	virtual ENGINE_API bool CheckMaterialUsage(const EMaterialUsage Usage) override;
	virtual ENGINE_API bool CheckMaterialUsage_Concurrent(const EMaterialUsage Usage) const override;
#if WITH_EDITORONLY_DATA
	virtual ENGINE_API bool GetStaticSwitchParameterValues(FStaticParamEvaluationContext& EvalContext, TBitArray<>& OutValues, FGuid* OutExpressionGuids, bool bCheckParent = true) const override;
	virtual ENGINE_API bool GetStaticComponentMaskParameterValues(FStaticParamEvaluationContext& EvalContext, TBitArray<>& OutRGBAOrderedValues, FGuid* OutExpressionGuids, bool bCheckParent = true) const override;
	virtual ENGINE_API bool GetMaterialLayersParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FMaterialLayersFunctions& OutLayers, FGuid &OutExpressionGuid, bool bCheckParent = true) const override;
#endif // WITH_EDITORONLY_DATA
	virtual ENGINE_API bool GetTerrainLayerWeightParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, int32& OutWeightmapIndex, FGuid &OutExpressionGuid) const override;
			ENGINE_API bool UpdateMaterialLayersParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, const FMaterialLayersFunctions& LayersValue, const bool bOverridden, const FGuid& GUID);
	virtual ENGINE_API bool IsDependent(UMaterialInterface* TestDependency) override;
	virtual ENGINE_API bool IsDependent_Concurrent(UMaterialInterface* TestDependency, TMicRecursionGuard RecursionGuard) override;
	virtual ENGINE_API FMaterialRenderProxy* GetRenderProxy() const override;
	virtual ENGINE_API UPhysicalMaterial* GetPhysicalMaterial() const override;
	virtual ENGINE_API UPhysicalMaterialMask* GetPhysicalMaterialMask() const override;
	virtual ENGINE_API UPhysicalMaterial* GetPhysicalMaterialFromMap(int32 Index) const override;
	virtual ENGINE_API bool UpdateLightmassTextureTracking() override;
	virtual ENGINE_API bool GetCastShadowAsMasked() const override;
	virtual ENGINE_API float GetEmissiveBoost() const override;
	virtual ENGINE_API float GetDiffuseBoost() const override;
	virtual ENGINE_API float GetExportResolutionScale() const override;
	virtual ENGINE_API int32 GetLayerParameterIndex(EMaterialParameterAssociation Association, UMaterialFunctionInterface* LayerFunction) const override;
#if WITH_EDITOR
	virtual ENGINE_API bool GetParameterDesc(const FHashedMaterialParameterInfo& ParameterInfo, FString& OutDesc, const TArray<struct FStaticMaterialLayersParameter>* MaterialLayersParameters = nullptr) const override;
	virtual ENGINE_API bool GetParameterSortPriority(const FHashedMaterialParameterInfo& ParameterInfo, int32& OutSortPriority, const TArray<struct FStaticMaterialLayersParameter>* MaterialLayersParameters = nullptr) const override;
	virtual ENGINE_API bool GetGroupSortPriority(const FString& InGroupName, int32& OutSortPriority) const override;
	virtual ENGINE_API bool GetTexturesInPropertyChain(EMaterialProperty InProperty, TArray<UTexture*>& OutTextures,
		TArray<FName>* OutTextureParamNames, struct FStaticParameterSet* InStaticParameterSet,
		ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type InQuality) override;
#endif
	virtual ENGINE_API void RecacheUniformExpressions(bool bRecreateUniformBuffer) const override;
	virtual ENGINE_API bool GetRefractionSettings(float& OutBiasValue) const override;

#if WITH_EDITOR
	ENGINE_API virtual void ForceRecompileForRendering() override;
#endif // WITH_EDITOR

	ENGINE_API virtual float GetOpacityMaskClipValue() const override;
	ENGINE_API virtual bool GetCastDynamicShadowAsMasked() const override;
	ENGINE_API virtual EBlendMode GetBlendMode() const override;
	ENGINE_API virtual FMaterialShadingModelField GetShadingModels() const override;
	ENGINE_API virtual bool IsShadingModelFromMaterialExpression() const override;
	ENGINE_API virtual bool IsTwoSided() const override;
	ENGINE_API virtual bool IsDitheredLODTransition() const override;
	ENGINE_API virtual bool IsMasked() const override;
	
	ENGINE_API virtual USubsurfaceProfile* GetSubsurfaceProfile_Internal() const override;
	ENGINE_API virtual bool CastsRayTracedShadows() const override;

	/** Checks to see if an input property should be active, based on the state of the material */
	ENGINE_API virtual bool IsPropertyActive(EMaterialProperty InProperty) const override;
#if WITH_EDITOR
	/** Allows material properties to be compiled with the option of being overridden by the material attributes input. */
	ENGINE_API virtual int32 CompilePropertyEx(class FMaterialCompiler* Compiler, const FGuid& AttributeID) override;
#endif // WITH_EDITOR
	//~ End UMaterialInterface Interface.

	//~ Begin UObject Interface.
	virtual ENGINE_API void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	virtual ENGINE_API void PostInitProperties() override;	
#if WITH_EDITOR
	virtual ENGINE_API void BeginCacheForCookedPlatformData(const ITargetPlatform *TargetPlatform) override;
	virtual ENGINE_API bool IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform) override;
	virtual ENGINE_API void ClearCachedCookedPlatformData(const ITargetPlatform *TargetPlatform) override;
	virtual ENGINE_API void ClearAllCachedCookedPlatformData() override;
#endif
	virtual ENGINE_API void Serialize(FArchive& Ar) override;
	virtual ENGINE_API void PostLoad() override;
	virtual ENGINE_API void BeginDestroy() override;
	virtual ENGINE_API bool IsReadyForFinishDestroy() override;
	virtual ENGINE_API void FinishDestroy() override;
	ENGINE_API static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
#if WITH_EDITOR
	virtual ENGINE_API void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/**
	 * Sets new static parameter overrides on the instance and recompiles the static permutation resources if needed (can be forced with bForceRecompile).
	 * Can be passed either a minimal parameter set (overridden parameters only) or the entire set generated by GetStaticParameterValues().
	 * Can also trigger recompile based on new set of FMaterialInstanceBasePropertyOverrides 
	 */
	ENGINE_API void UpdateStaticPermutation(const FStaticParameterSet& NewParameters, FMaterialInstanceBasePropertyOverrides& NewBasePropertyOverrides, const bool bForceStaticPermutationUpdate = false, FMaterialUpdateContext* MaterialUpdateContext = nullptr);
	/**
	* Sets new static parameter overrides on the instance and recompiles the static permutation resources if needed.
	* Can be passed either a minimal parameter set (overridden parameters only) or the entire set generated by GetStaticParameterValues().
	*/
	ENGINE_API void UpdateStaticPermutation(const FStaticParameterSet& NewParameters, FMaterialUpdateContext* MaterialUpdateContext = nullptr);
	/** Ensure's static permutations for current parameters and overrides are upto date. */
	ENGINE_API void UpdateStaticPermutation(FMaterialUpdateContext* MaterialUpdateContext = nullptr);

	ENGINE_API void SwapLayerParameterIndices(int32 OriginalIndex, int32 NewIndex);

#endif // WITH_EDITOR

	//~ End UObject Interface.

	/**
	 * Recompiles static permutations if necessary.
	 * Note: This modifies material variables used for rendering and is assumed to be called within a FMaterialUpdateContext!
	 */
	ENGINE_API void InitStaticPermutation();

	ENGINE_API void UpdateOverridableBaseProperties();

	/** 
	 * Cache resource shaders for rendering on the given shader platform. 
	 * If a matching shader map is not found in memory or the DDC, a new one will be compiled.
	 * The results will be applied to this FMaterial in the renderer when they are finished compiling.
	 * Note: This modifies material variables used for rendering and is assumed to be called within a FMaterialUpdateContext!
	 */
	void CacheResourceShadersForCooking(EShaderPlatform ShaderPlatform, TArray<FMaterialResource*>& OutCachedMaterialResources, const ITargetPlatform* TargetPlatform = nullptr);

	/** 
	 * Gathers actively used shader maps from all material resources used by this material instance
	 * Note - not refcounting the shader maps so the references must not be used after material resources are modified (compilation, loading, etc)
	 */
	void GetAllShaderMaps(TArray<FMaterialShaderMap*>& OutShaderMaps);

#if WITH_EDITORONLY_DATA
	/**
	 * Builds a composited set of static parameters, including inherited and overridden values
	 */
	ENGINE_API void GetStaticParameterValues(FStaticParameterSet& OutStaticParameters);

	/**
	 * Builds a composited set of parameter names, including inherited and overridden values
	 */
	template<typename ExpressionType>
	void GetAllParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
	{
		if (const UMaterial* Material = GetMaterial())
		{
			Material->GetAllParameterInfo<ExpressionType>(OutParameterInfo, OutParameterIds);
		}
	}
#endif // WITH_EDITORONLY_DATA

	void GetAllParametersOfType(EMaterialParameterType Type, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const;

	ENGINE_API virtual void GetAllScalarParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const override;
	ENGINE_API virtual void GetAllVectorParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const override;
	ENGINE_API virtual void GetAllTextureParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const override;
	ENGINE_API virtual void GetAllRuntimeVirtualTextureParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const override;
	ENGINE_API virtual void GetAllFontParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const override;

#if WITH_EDITORONLY_DATA
	ENGINE_API virtual void GetAllMaterialLayersParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const override;
	ENGINE_API virtual void GetAllStaticSwitchParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const override;
	ENGINE_API virtual void GetAllStaticComponentMaskParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const override;

	ENGINE_API virtual bool IterateDependentFunctions(TFunctionRef<bool(UMaterialFunctionInterface*)> Predicate) const override;
	ENGINE_API virtual void GetDependentFunctions(TArray<class UMaterialFunctionInterface*>& DependentFunctions) const override;
#endif // WITH_EDITORONLY_DATA

	ENGINE_API virtual bool GetScalarParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue, bool bOveriddenOnly = false, bool bCheckOwnedGlobalOverrides = false) const override;
	ENGINE_API virtual bool GetVectorParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue, bool bOveriddenOnly = false, bool bCheckOwnedGlobalOverrides = false) const override;
	ENGINE_API virtual bool GetTextureParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, UTexture*& OutValue, bool bCheckOwnedGlobalOverrides = false) const override;
	ENGINE_API virtual bool GetRuntimeVirtualTextureParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, URuntimeVirtualTexture*& OutValue, bool bCheckOwnedGlobalOverrides = false) const override;
	ENGINE_API virtual bool GetFontParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, UFont*& OutFontValue, int32& OutFontPage, bool bCheckOwnedGlobalOverrides = false) const override;
#if WITH_EDITOR
	ENGINE_API virtual bool GetStaticSwitchParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, FGuid& OutExpressionGuid, bool bCheckOwnedGlobalOverrides = false) const override;
	ENGINE_API virtual bool GetStaticComponentMaskParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutR, bool& OutG, bool& OutB, bool& OutA, FGuid& OutExpressionGuid, bool bCheckOwnedGlobalOverrides = false) const override;
	ENGINE_API virtual bool GetGroupName(const FHashedMaterialParameterInfo& ParameterInfo, FName& OutGroup) const override;
#endif // WITH_EDITOR

	/** Appends textures referenced by expressions, including nested functions. */
	ENGINE_API virtual TArrayView<UObject* const> GetReferencedTextures() const override final { return CachedReferencedTextures; }

#if WITH_EDITOR
	/** Add to the set any texture referenced by expressions, including nested functions, as well as any overrides from parameters. */
	ENGINE_API virtual void GetReferencedTexturesAndOverrides(TSet<const UTexture*>& InOutTextures) const;

	ENGINE_API void UpdateCachedLayerParameters();
#endif

	void GetBasePropertyOverridesHash(FSHAHash& OutHash)const;
	ENGINE_API virtual bool HasOverridenBaseProperties()const;

	// For all materials instances, UMaterialInstance::CacheResourceShadersForRendering
	ENGINE_API static void AllMaterialsCacheResourceShadersForRendering(bool bUpdateProgressDialog = false);

	/**
	 * Determine whether this Material Instance is a child of another Material
	 *
	 * @param	Material	Material to check against
	 * @return	true if this Material Instance is a child of the other Material.
	 */
	ENGINE_API bool IsChildOf(const UMaterialInterface* Material) const;


#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/**
	 * Output to the log which materials and textures are used by this material.
	 * @param Indent	Number of tabs to put before the log.
	 */
	ENGINE_API virtual void LogMaterialsAndTextures(FOutputDevice& Ar, int32 Indent) const override;
#endif

	ENGINE_API void ValidateTextureOverrides(ERHIFeatureLevel::Type InFeatureLevel) const;

	/**
	 *	Returns all the Guids related to this material. For material instances, this includes the parent hierarchy.
	 *  Used for versioning as parent changes don't update the child instance Guids.
	 *
	 *	@param	bIncludeTextures	Whether to include the referenced texture Guids.
	 *	@param	OutGuids			The list of all resource guids affecting the precomputed lighting system and texture streamer.
	 */
	ENGINE_API virtual void GetLightingGuidChain(bool bIncludeTextures, TArray<FGuid>& OutGuids) const override;

	void DumpDebugInfo() const;
	void SaveShaderStableKeys(const class ITargetPlatform* TP);
	ENGINE_API virtual void SaveShaderStableKeysInner(const class ITargetPlatform* TP, const struct FStableShaderKeyAndValue& SaveKeyVal) override;

#if WITH_EDITOR
	void BeginAllowCachingStaticParameterValues();
	void EndAllowCachingStaticParameterValues();
#endif // WITH_EDITOR

protected:

	/**
	* Copies the uniform parameters (scalar, vector and texture) from a material or instance hierarchy.
	* This will typically be faster than parsing all expressions but still slow as it must walk the full
	* material hierarchy as each parameter may be overridden at any level in the chain.
	* Note: This will not copy static or font parameters
	*/
	void CopyMaterialUniformParametersInternal(UMaterialInterface* Source);

	/**
	 * Updates parameter names on the material instance, returns true if parameters have changed.
	 */
	bool UpdateParameters();

	ENGINE_API void SetParentInternal(class UMaterialInterface* NewParent, bool RecacheShaders);

	void GetTextureExpressionValues(const FMaterialResource* MaterialResource, TArray<UTexture*>& OutTextures, TArray< TArray<int32> >* OutIndices = nullptr) const;

	UE_DEPRECATED(4.26, "Calling UpdatePermutationAllocations is no longer necessary")
	inline void UpdatePermutationAllocations(FMaterialResourceDeferredDeletionArray* ResourcesToFree = nullptr) {}

#if WITH_EDITOR
	/**
	* Refresh parameter names using the stored reference to the expression object for the parameter.
	*/
	ENGINE_API void UpdateParameterNames();

#endif // WITH_EDITOR

	/**
	 * Internal interface for setting / updating values for material instances.
	 */
	void SetVectorParameterValueInternal(const FMaterialParameterInfo& ParameterInfo, FLinearColor Value);
	bool SetVectorParameterByIndexInternal(int32 ParameterIndex, FLinearColor Value);
	bool SetScalarParameterByIndexInternal(int32 ParameterIndex, float Value);
	void SetScalarParameterValueInternal(const FMaterialParameterInfo& ParameterInfo, float Value);
#if WITH_EDITOR
	void SetScalarParameterAtlasInternal(const FMaterialParameterInfo& ParameterInfo, FScalarParameterAtlasInstanceData AtlasData);
#endif
	void SetTextureParameterValueInternal(const FMaterialParameterInfo& ParameterInfo, class UTexture* Value);
	void SetRuntimeVirtualTextureParameterValueInternal(const FMaterialParameterInfo& ParameterInfo, class URuntimeVirtualTexture* Value);
	void SetFontParameterValueInternal(const FMaterialParameterInfo& ParameterInfo, class UFont* FontValue, int32 FontPage);
	void ClearParameterValuesInternal(const bool bAllParameters = true);

	/** Initialize the material instance's resources. */
	ENGINE_API void InitResources();

	/** 
	 * Cache resource shaders for rendering on the given shader platform. 
	 * If a matching shader map is not found in memory or the DDC, a new one will be compiled.
	 * The results will be applied to this FMaterial in the renderer when they are finished compiling.
	 * Note: This modifies material variables used for rendering and is assumed to be called within a FMaterialUpdateContext!
	 */
	void CacheResourceShadersForRendering();
	void CacheResourceShadersForRendering(FMaterialResourceDeferredDeletionArray& OutResourcesToFree);

	void DeleteDeferredResources(FMaterialResourceDeferredDeletionArray& ResourcesToFree);

	/** Caches shader maps for an array of material resources. */
	void CacheShadersForResources(EShaderPlatform ShaderPlatform, const TArray<FMaterialResource*>& ResourcesToCache, const ITargetPlatform* TargetPlatform = nullptr);

	/** 
	 * Copies over material instance parameters from the base material given a material interface.
	 * This is a slow operation that is needed for the editor.
	 * @param Source silently ignores the case if 0
	 */
	ENGINE_API void CopyMaterialInstanceParameters(UMaterialInterface* Source);

	// to share code between PostLoad() and PostEditChangeProperty()
	void PropagateDataToMaterialProxy();

	/** Allow resource to access private members. */
	friend class FMaterialInstanceResource;
	/** Editor-only access to private members. */
	friend class UMaterialEditingLibrary;
	/** Class that knows how to update MI's */
	friend class FMaterialUpdateContext;
};

#if WITH_EDITOR
namespace MaterialInstance_Private
{
	/** Workaround - Similar to base call but evaluates all expressions found, not just the first */
	template<typename ExpressionType>
	void FindClosestExpressionByGUIDRecursive(const FName& InName, const FGuid& InGUID, const TArray<UMaterialExpression*>& InMaterialExpression, ExpressionType*& OutExpression)
	{
		for (int32 ExpressionIndex = 0; ExpressionIndex < InMaterialExpression.Num(); ExpressionIndex++)
		{
			UMaterialExpression* ExpressionPtr = InMaterialExpression[ExpressionIndex];
			UMaterialExpressionMaterialFunctionCall* MaterialFunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(ExpressionPtr);
			UMaterialExpressionMaterialAttributeLayers* MaterialLayers = Cast<UMaterialExpressionMaterialAttributeLayers>(ExpressionPtr);

			if (ExpressionPtr && ExpressionPtr->GetParameterExpressionId() == InGUID)
			{
				check(ExpressionPtr->bIsParameterExpression);
				if (ExpressionType* ParamExpression = Cast<ExpressionType>(ExpressionPtr))
				{
					// UE-57086, workaround - To deal with duplicated parameters with matching GUIDs we walk
					// through every parameter rather than taking the first. Either we return the first matching GUID
					// we encounter (as before), or if we find another with the same name that can take precedence.
					// Only taking the first parameter means we can incorrectly treat the parameter as a rename and
					// lose/move data when we encounter an illegal GUID duplicate.
					// Properly fixing duplicate GUIDs is beyond the scope of a hotfix, see UE-47863 for more info.
					// NOTE: The case where a parameter in a function is renamed but another function in the material
					// contains a duplicate GUID is still broken and may lose the data. This still leaves us in a
					// more consistent state than 4.18 and should minimize the impact to a rarer occurrence.
					if (!OutExpression || InName == ParamExpression->ParameterName)
					{
						OutExpression = ParamExpression;
					}
				}
			}
			else if (MaterialFunctionCall && MaterialFunctionCall->MaterialFunction)
			{
				if (const TArray<UMaterialExpression*>* FunctionExpressions = MaterialFunctionCall->MaterialFunction->GetFunctionExpressions())
				{
					FindClosestExpressionByGUIDRecursive<ExpressionType>(InName, InGUID, *FunctionExpressions, OutExpression);
				}
			}
			else if (MaterialLayers)
			{
				const TArray<UMaterialFunctionInterface*>& Layers = MaterialLayers->GetLayers();
				const TArray<UMaterialFunctionInterface*>& Blends = MaterialLayers->GetBlends();

				for (const auto* Layer : Layers)
				{
					if (Layer)
					{
						if (const TArray<UMaterialExpression*>* FunctionExpressions = Layer->GetFunctionExpressions())
						{
							FindClosestExpressionByGUIDRecursive<ExpressionType>(InName, InGUID, *FunctionExpressions, OutExpression);
						}
					}
				}

				for (const auto* Blend : Blends)
				{
					if (Blend)
					{
						if (const TArray<UMaterialExpression*>* FunctionExpressions = Blend->GetFunctionExpressions())
						{
							FindClosestExpressionByGUIDRecursive<ExpressionType>(InName, InGUID, *FunctionExpressions, OutExpression);
						}
					}
				}
			}
		}
	}

	template <typename ParameterType, typename ExpressionType>
	bool UpdateParameter_FullTraversal(ParameterType& Parameter, UMaterial* ParentMaterial)
	{
		for (UMaterialExpression* Expression : ParentMaterial->Expressions)
		{
			if (Expression->IsA<ExpressionType>())
			{
				ExpressionType* ParameterExpression = CastChecked<ExpressionType>(Expression);
				if (ParameterExpression->ParameterName == Parameter.ParameterInfo.Name)
				{
					Parameter.ExpressionGUID = ParameterExpression->ExpressionGUID;
					return true;
				}
			}
			else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
			{
				if (FunctionCall->MaterialFunction && FunctionCall->MaterialFunction->UpdateParameterSet<ParameterType, ExpressionType>(Parameter))
				{
					return true;
				}
			}
			else if (UMaterialExpressionMaterialAttributeLayers* LayersExpression = Cast<UMaterialExpressionMaterialAttributeLayers>(Expression))
			{
				const TArray<UMaterialFunctionInterface*> Layers = LayersExpression->GetLayers();
				const TArray<UMaterialFunctionInterface*> Blends = LayersExpression->GetBlends();

				for (auto* Layer : Layers)
				{
					if (Layer && Layer->UpdateParameterSet<ParameterType, ExpressionType>(Parameter))
					{
						return true;
					}
				}

				for (auto* Blend : Blends)
				{
					if (Blend && Blend->UpdateParameterSet<ParameterType, ExpressionType>(Parameter))
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	template <typename ParameterType, typename ExpressionType>
	bool UpdateParameterSet_FullTraversal(TArray<ParameterType>& Parameters, UMaterial* ParentMaterial)
	{
		bool bChanged = false;

		// Loop through all of the parameters and try to either establish a reference to the 
		// expression the parameter represents, or check to see if the parameter's name has changed.
		for (int32 ParameterIdx = 0; ParameterIdx < Parameters.Num(); ParameterIdx++)
		{
			bool bTryToFindByName = true;

			ParameterType& Parameter = Parameters[ParameterIdx];

			if (Parameter.ExpressionGUID.IsValid())
			{
				ExpressionType* Expression = nullptr;
				FindClosestExpressionByGUIDRecursive<ExpressionType>(Parameter.ParameterInfo.Name, Parameter.ExpressionGUID, ParentMaterial->Expressions, Expression);

				// Check to see if the parameter name was changed.
				if (Expression)
				{
					bTryToFindByName = false;

					if (Parameter.ParameterInfo.Name != Expression->ParameterName)
					{
						Parameter.ParameterInfo.Name = Expression->ParameterName;
						bChanged = true;
					}
				}
			}

			// No reference to the material expression exists, so try to find one in the material expression's array if we are in the editor.
			if (bTryToFindByName && GIsEditor && !FApp::IsGame())
			{
				if (UpdateParameter_FullTraversal<ParameterType, ExpressionType>(Parameter, ParentMaterial))
				{
					bChanged = true;
				}
			}
		}

		return bChanged;
	}


	template <typename ParameterType, typename ExpressionType>
	bool UpdateParameterSet_WithCachedData(EMaterialParameterType ParamTypeEnum, TArray<ParameterType>& Parameters, UMaterial* ParentMaterial)
	{
		bool bChanged = false;

		TArray<FMaterialParameterInfo> CachedParamInfos;
		TArray<FGuid> CachedParamGuids;
		ParentMaterial->GetCachedExpressionData().Parameters.GetAllParameterInfoOfType(ParamTypeEnum, false, CachedParamInfos, CachedParamGuids);
		int32 NumCachedParams = CachedParamGuids.Num();
		check(NumCachedParams == CachedParamInfos.Num());

		// Loop through all of the parameters and try to either establish a reference to the 
		// expression the parameter represents, or check to see if the parameter's name has changed.
		for (int32 ParameterIdx = 0; ParameterIdx < Parameters.Num(); ParameterIdx++)
		{
			bool bTryToFindByName = true;

			ParameterType& Parameter = Parameters[ParameterIdx];

			if (Parameter.ExpressionGUID.IsValid())
			{
				int32 CachedParamCandidate = INDEX_NONE;
				for (int32 CachedParamIdx = 0; CachedParamIdx < NumCachedParams; ++CachedParamIdx)
				{
					if (CachedParamGuids[CachedParamIdx] == Parameter.ExpressionGUID)
					{
						// UE-57086, workaround - To deal with duplicated parameters with matching GUIDs we walk
						// through every parameter rather than taking the first. Either we return the first matching GUID
						// we encounter (as before), or if we find another with the same name that can take precedence.
						// Only taking the first parameter means we can incorrectly treat the parameter as a rename and
						// lose/move data when we encounter an illegal GUID duplicate.
						// Properly fixing duplicate GUIDs is beyond the scope of a hotfix, see UE-47863 for more info.
						// NOTE: The case where a parameter in a function is renamed but another function in the material
						// contains a duplicate GUID is still broken and may lose the data. This still leaves us in a
						// more consistent state than 4.18 and should minimize the impact to a rarer occurrence.
						if ((CachedParamCandidate == INDEX_NONE) || Parameter.ParameterInfo.Name == CachedParamInfos[CachedParamIdx].Name)
						{
							CachedParamCandidate = CachedParamIdx;
						}
					}
				}

				// Check to see if the parameter name was changed.
				if (CachedParamCandidate != INDEX_NONE)
				{
					const FMaterialParameterInfo& CandidateParamInfo = CachedParamInfos[CachedParamCandidate];
					bTryToFindByName = false;

					if (Parameter.ParameterInfo.Name != CandidateParamInfo.Name)
					{
						Parameter.ParameterInfo.Name = CandidateParamInfo.Name;
						bChanged = true;
					}
				}
			}

			// No reference to the material expression exists, so try to find one in the material expression's array if we are in the editor.
			if (bTryToFindByName && GIsEditor && !FApp::IsGame())
			{
				if (UpdateParameter_FullTraversal<ParameterType, ExpressionType>(Parameter, ParentMaterial))
				{
					bChanged = true;
				}
			}
		}

		return bChanged;
	}
}

/**
 * This function takes a array of parameter structs and attempts to establish a reference to the expression object each parameter represents.
 * If a reference exists, the function checks to see if the parameter has been renamed.
 *
 * @param Parameters		Array of parameters to operate on.
 * @param ParentMaterial	Parent material to search in for expressions.
 *
 * @return Returns whether or not any of the parameters was changed.
 */
template <typename ParameterType, typename ExpressionType>
bool UpdateParameterSet(TArray<ParameterType>& Parameters, UMaterial* ParentMaterial) { return MaterialInstance_Private::UpdateParameterSet_FullTraversal<ParameterType, ExpressionType>(Parameters, ParentMaterial); }

/**
 * Overloads for UpdateParameterSet to use cached data for types that can leverage it 
 */
template <typename ParameterType, typename ExpressionType>
bool UpdateParameterSet(TArray<FScalarParameterValue>& Parameters, UMaterial* ParentMaterial)
{
	return MaterialInstance_Private::UpdateParameterSet_WithCachedData<FScalarParameterValue, ExpressionType>(EMaterialParameterType::Scalar, Parameters, ParentMaterial);
}
template <typename ParameterType, typename ExpressionType>
bool UpdateParameterSet(TArray<FVectorParameterValue>& Parameters, UMaterial* ParentMaterial)
{
	return MaterialInstance_Private::UpdateParameterSet_WithCachedData<FVectorParameterValue, ExpressionType>(EMaterialParameterType::Vector, Parameters, ParentMaterial);
}
template <typename ParameterType, typename ExpressionType>
bool UpdateParameterSet(TArray<FTextureParameterValue>& Parameters, UMaterial* ParentMaterial)
{
	return MaterialInstance_Private::UpdateParameterSet_WithCachedData<FTextureParameterValue, ExpressionType>(EMaterialParameterType::Texture, Parameters, ParentMaterial);
}
template <typename ParameterType, typename ExpressionType>
bool UpdateParameterSet(TArray<FFontParameterValue>& Parameters, UMaterial* ParentMaterial)
{
	return MaterialInstance_Private::UpdateParameterSet_WithCachedData<FFontParameterValue, ExpressionType>(EMaterialParameterType::Font, Parameters, ParentMaterial);
}
template <typename ParameterType, typename ExpressionType>
bool UpdateParameterSet(TArray<FRuntimeVirtualTextureParameterValue>& Parameters, UMaterial* ParentMaterial)
{
	return MaterialInstance_Private::UpdateParameterSet_WithCachedData<FRuntimeVirtualTextureParameterValue, ExpressionType>(EMaterialParameterType::RuntimeVirtualTexture, Parameters, ParentMaterial);
}
template <typename ParameterType, typename ExpressionType>
bool UpdateParameterSet(TArray<FStaticSwitchParameter>& Parameters, UMaterial* ParentMaterial)
{
	return MaterialInstance_Private::UpdateParameterSet_WithCachedData<FStaticSwitchParameter, ExpressionType>(EMaterialParameterType::StaticSwitch, Parameters, ParentMaterial);
}
template <typename ParameterType, typename ExpressionType>
bool UpdateParameterSet(TArray<FStaticComponentMaskParameter>& Parameters, UMaterial* ParentMaterial)
{
	return MaterialInstance_Private::UpdateParameterSet_WithCachedData<FStaticComponentMaskParameter, ExpressionType>(EMaterialParameterType::StaticComponentMask, Parameters, ParentMaterial);
}



#endif // WITH_EDITOR
