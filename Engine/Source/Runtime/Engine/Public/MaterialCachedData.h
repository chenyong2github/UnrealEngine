// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MaterialTypes.h"
#include "Materials/MaterialLayersFunctions.h"
#include "Misc/Guid.h"
#include "UObject/ObjectMacros.h"
#include "SceneTypes.h"
#include "MaterialCachedData.generated.h"

class UTexture;
class UCurveLinearColor;
class UCurveLinearColorAtlas;
class UFont;
class UMaterialExpression;
class URuntimeVirtualTexture;
class ULandscapeGrassType;
class UMaterialFunctionInterface;
class UMaterialInterface;

/** Stores information about a function that this material references, used to know when the material needs to be recompiled. */
USTRUCT()
struct FMaterialFunctionInfo
{
	GENERATED_USTRUCT_BODY()

	/** Id that the function had when this material was last compiled. */
	UPROPERTY()
	FGuid StateId;

	/** The function which this material has a dependency on. */
	UPROPERTY()
	TObjectPtr<UMaterialFunctionInterface> Function = nullptr;
};

/** Stores information about a parameter collection that this material references, used to know when the material needs to be recompiled. */
USTRUCT()
struct FMaterialParameterCollectionInfo
{
	GENERATED_USTRUCT_BODY()

	/** Id that the collection had when this material was last compiled. */
	UPROPERTY()
	FGuid StateId;

	/** The collection which this material has a dependency on. */
	UPROPERTY()
	TObjectPtr<class UMaterialParameterCollection> ParameterCollection = nullptr;

	bool operator==(const FMaterialParameterCollectionInfo& Other) const
	{
		return StateId == Other.StateId && ParameterCollection == Other.ParameterCollection;
	}
};

USTRUCT()
struct FMaterialCachedParameterEditorInfo
{
	GENERATED_USTRUCT_BODY()

	FMaterialCachedParameterEditorInfo() = default;
	FMaterialCachedParameterEditorInfo(const FGuid& InGuid) : ExpressionGuid(InGuid) {}
	FMaterialCachedParameterEditorInfo(const FGuid& InGuid, const FString& InDescription, const FName& InGroup, int32 InSortPriority)
		: Description(InDescription)
		, Group(InGroup)
		, SortPriority(InSortPriority)
		, ExpressionGuid(InGuid)
	{}

	UPROPERTY()
	FString Description;

	UPROPERTY()
	FName Group;

	UPROPERTY()
	int32 SortPriority = 0;

	UPROPERTY()
	FGuid ExpressionGuid;
};

USTRUCT()
struct FMaterialCachedParameterEntry
{
	GENERATED_USTRUCT_BODY()

	ENGINE_API static const FMaterialCachedParameterEntry EmptyData;

	void Reset();

	// This is used to map FMaterialParameterInfos to indices, which are then used to index various TArrays containing values for each type of parameter
	// (ExpressionGuids and Overrides, along with ScalarValues, VectorValues, etc)
	UPROPERTY()
	TSet<FMaterialParameterInfo> ParameterInfoSet;

#if WITH_EDITORONLY_DATA
	// Editor-only information for each parameter
	UPROPERTY()
	TArray<FMaterialCachedParameterEditorInfo> EditorInfo;
#endif // WITH_EDITORONLY_DATA

};

USTRUCT()
struct FMaterialCachedParameters
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	inline const FMaterialCachedParameterEntry& GetParameterTypeEntry(EMaterialParameterType Type) const { return Type >= EMaterialParameterType::NumRuntime ? EditorOnlyEntries[static_cast<int32>(Type) - NumMaterialRuntimeParameterTypes] : RuntimeEntries[static_cast<int32>(Type)]; }
	inline FMaterialCachedParameterEntry& GetParameterTypeEntry(EMaterialParameterType Type) { return Type >= EMaterialParameterType::NumRuntime ? EditorOnlyEntries[static_cast<int32>(Type) - NumMaterialRuntimeParameterTypes] : RuntimeEntries[static_cast<int32>(Type)]; }
#else
	inline const FMaterialCachedParameterEntry& GetParameterTypeEntry(EMaterialParameterType Type) const { return Type >= EMaterialParameterType::NumRuntime ? FMaterialCachedParameterEntry::EmptyData : RuntimeEntries[static_cast<int32>(Type)]; }
#endif

	inline int32 GetNumParameters(EMaterialParameterType Type) const { return GetParameterTypeEntry(Type).ParameterInfoSet.Num(); }
	int32 FindParameterIndex(EMaterialParameterType Type, const FMemoryImageMaterialParameterInfo& HashedParameterInfo) const;
	bool GetParameterValue(EMaterialParameterType Type, const FMemoryImageMaterialParameterInfo& ParameterInfo, FMaterialParameterMetadata& OutResult) const;
	void GetParameterValueByIndex(EMaterialParameterType Type, int32 ParameterIndex, FMaterialParameterMetadata& OutResult) const;
#if WITH_EDITORONLY_DATA
	const FGuid& GetExpressionGuid(EMaterialParameterType Type, int32 Index) const;
#endif
	void GetAllParametersOfType(EMaterialParameterType Type, TMap<FMaterialParameterInfo, FMaterialParameterMetadata>& OutParameters) const;
	void GetAllParameterInfoOfType(EMaterialParameterType Type, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const;
	void GetAllGlobalParametersOfType(EMaterialParameterType Type, TMap<FMaterialParameterInfo, FMaterialParameterMetadata>& OutParameters) const;
	void GetAllGlobalParameterInfoOfType(EMaterialParameterType Type, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const;
	void Reset();

	void AddReferencedObjects(FReferenceCollector& Collector);

	UPROPERTY()
	FMaterialCachedParameterEntry RuntimeEntries[NumMaterialRuntimeParameterTypes];

	UPROPERTY()
	TArray<int32> ScalarPrimitiveDataIndexValues;

	UPROPERTY()
	TArray<int32> VectorPrimitiveDataIndexValues;

	UPROPERTY()
	TArray<float> ScalarValues;

	UPROPERTY()
	TArray<FLinearColor> VectorValues;

	UPROPERTY()
	TArray<FVector4d> DoubleVectorValues;

	UPROPERTY()
	TArray<TObjectPtr<UTexture>> TextureValues;

	UPROPERTY()
	TArray<TObjectPtr<UFont>> FontValues;

	UPROPERTY()
	TArray<int32> FontPageValues;

	UPROPERTY()
	TArray<TObjectPtr<URuntimeVirtualTexture>> RuntimeVirtualTextureValues;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FMaterialCachedParameterEntry EditorOnlyEntries[NumMaterialEditorOnlyParameterTypes];

	UPROPERTY()
	TArray<bool> StaticSwitchValues;

	UPROPERTY()
	TArray<FStaticComponentMaskValue> StaticComponentMaskValues;

	UPROPERTY()
	TArray<FVector2D> ScalarMinMaxValues;

	UPROPERTY()
	TArray<TObjectPtr<UCurveLinearColor>> ScalarCurveValues;

	UPROPERTY()
	TArray<TObjectPtr<UCurveLinearColorAtlas>> ScalarCurveAtlasValues;

	UPROPERTY()
	TArray<FParameterChannelNames> VectorChannelNameValues;

	UPROPERTY()
	TArray<bool> VectorUsedAsChannelMaskValues;

	UPROPERTY()
	TArray<FParameterChannelNames> TextureChannelNameValues;
#endif // WITH_EDITORONLY_DATA
};

struct FMaterialCachedExpressionContext
{
	const UMaterialFunctionInterface* CurrentFunction = nullptr;
	const FMaterialLayersFunctions* LayerOverrides = nullptr;
	bool bUpdateFunctionExpressions = true;
};

USTRUCT()
struct FMaterialCachedExpressionData
{
	GENERATED_USTRUCT_BODY()
	
	ENGINE_API static const FMaterialCachedExpressionData EmptyData;

	FMaterialCachedExpressionData()
		: bHasMaterialLayers(false)
		, bHasRuntimeVirtualTextureOutput(false)
		, bHasSceneColor(false)
		, bHasPerInstanceCustomData(false)
		, bHasPerInstanceRandom(false)
		, bHasVertexInterpolator(false)
	{}

#if WITH_EDITOR
	void UpdateForExpressions(const FMaterialCachedExpressionContext& Context, const TArray<TObjectPtr<UMaterialExpression>>& Expressions, EMaterialParameterAssociation Association, int32 ParameterIndex);
	void UpdateForFunction(const FMaterialCachedExpressionContext& Context, UMaterialFunctionInterface* Function, EMaterialParameterAssociation Association, int32 ParameterIndex);
	void UpdateForLayerFunctions(const FMaterialCachedExpressionContext& Context, const FMaterialLayersFunctions& LayerFunctions);
#endif // WITH_EDITOR

	void Reset();

	void AddReferencedObjects(FReferenceCollector& Collector);

	/** Returns an array of the guids of functions used, with the call hierarchy flattened. */
	void AppendReferencedFunctionIdsTo(TArray<FGuid>& OutIds) const;

	/** Returns an array of the guids of parameter collections used. */
	void AppendReferencedParameterCollectionIdsTo(TArray<FGuid>& OutIds) const;

	bool IsMaterialAttributePropertyConnected(EMaterialProperty Property) const
	{
		return ((MaterialAttributesPropertyConnectedBitmask >> (uint32)Property) & 0x1) != 0;
	}

	void SetMaterialAttributePropertyConnected(EMaterialProperty Property, bool bIsConnected)
	{
		MaterialAttributesPropertyConnectedBitmask = bIsConnected ? MaterialAttributesPropertyConnectedBitmask | (1 << (uint32)Property) : MaterialAttributesPropertyConnectedBitmask & ~(1 << (uint32)Property);
	}

	UPROPERTY()
	FMaterialCachedParameters Parameters;

	/** Array of all texture referenced by this material */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> ReferencedTextures;

	/** Array of all functions this material depends on. */
	UPROPERTY()
	TArray<FMaterialFunctionInfo> FunctionInfos;

	/** Array of all parameter collections this material depends on. */
	UPROPERTY()
	TArray<FMaterialParameterCollectionInfo> ParameterCollectionInfos;

	UPROPERTY()
	FMaterialLayersFunctions MaterialLayers;

	UPROPERTY()
	TArray<TObjectPtr<ULandscapeGrassType>> GrassTypes;

	UPROPERTY()
	TArray<FName> DynamicParameterNames;

	UPROPERTY()
	TArray<bool> QualityLevelsUsed;

	UPROPERTY()
	uint32 bHasMaterialLayers : 1;

	UPROPERTY()
	uint32 bHasRuntimeVirtualTextureOutput : 1;

	UPROPERTY()
	uint32 bHasSceneColor : 1;

	UPROPERTY()
	uint32 bHasPerInstanceCustomData : 1;

	UPROPERTY()
	uint32 bHasPerInstanceRandom : 1;

	UPROPERTY()
	uint32 bHasVertexInterpolator : 1;

	/** Each bit corresponds to EMaterialProperty connection status. */
	UPROPERTY()
	uint32 MaterialAttributesPropertyConnectedBitmask = 0;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FName> LandscapeLayerNames;
#endif // WITH_EDITORONLY_DATA
};

USTRUCT()
struct FMaterialInstanceCachedData
{
	GENERATED_USTRUCT_BODY()

	ENGINE_API static const FMaterialInstanceCachedData EmptyData;

#if WITH_EDITOR
	void InitializeForConstant(const FMaterialLayersFunctions* Layers, const FMaterialLayersFunctions* ParentLayers);
#endif // WITH_EDITOR
	void InitializeForDynamic(const FMaterialLayersFunctions* ParentLayers);

	UPROPERTY()
	TArray<int32> ParentLayerIndexRemap;
};
