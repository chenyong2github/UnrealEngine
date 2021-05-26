// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialLayersFunctions.h"
#include "Misc/Guid.h"
#include "UObject/ObjectMacros.h"
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
	UMaterialFunctionInterface* Function = nullptr;
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
	class UMaterialParameterCollection* ParameterCollection = nullptr;

	bool operator==(const FMaterialParameterCollectionInfo& Other) const
	{
		return StateId == Other.StateId && ParameterCollection == Other.ParameterCollection;
	}
};

USTRUCT()
struct FParameterChannelNames
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText R;

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText G;

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText B;

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText A;
};

enum class EMaterialParameterType : int32
{
	Scalar,
	Vector,
	Texture,
	Font,
	RuntimeVirtualTexture,

	RuntimeCount, // Runtime parameter types must go above here, and editor-only ones below

#if WITH_EDITORONLY_DATA
	StaticSwitch = RuntimeCount,
	StaticComponentMask,
	// Excluding StaticMaterialLayer due to type specific complications

	Count,
#else
	Count = RuntimeCount,
#endif
};
static const int32 NumMaterialRuntimeParameterTypes = (int32)EMaterialParameterType::RuntimeCount;
#if WITH_EDITORONLY_DATA
static const int32 NumMaterialEditorOnlyParameterTypes = (int32)EMaterialParameterType::Count - (int32)EMaterialParameterType::RuntimeCount;
#endif

USTRUCT()
struct FMaterialCachedParameterEntry
{
	GENERATED_USTRUCT_BODY()

	void Reset();

	UPROPERTY()
	TArray<uint64> NameHashes;

	UPROPERTY()
	TArray<FMaterialParameterInfo> ParameterInfos;

	UPROPERTY()
	TArray<FGuid> ExpressionGuids; // editor-only?

};

USTRUCT()
struct FStaticComponentMaskValue
{
	GENERATED_USTRUCT_BODY();

	FStaticComponentMaskValue() : R(false), G(false), B(false), A(false) {}
	FStaticComponentMaskValue(bool InR, bool InG, bool InB, bool InA) : R(InR), G(InG), B(InB), A(InA) {}

	UPROPERTY()
	bool R = false;
	
	UPROPERTY()
	bool G = false;

	UPROPERTY()
	bool B = false;

	UPROPERTY()
	bool A = false;
};

USTRUCT()
struct FMaterialCachedParameters
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	inline const FMaterialCachedParameterEntry& GetParameterTypeEntry(EMaterialParameterType Type) const { return Type >= EMaterialParameterType::RuntimeCount ? EditorOnlyEntries[static_cast<int32>(Type) - static_cast<int32>(EMaterialParameterType::RuntimeCount)] : RuntimeEntries[static_cast<int32>(Type)]; }
#else
	inline const FMaterialCachedParameterEntry& GetParameterTypeEntry(EMaterialParameterType Type) const { return RuntimeEntries[static_cast<int32>(Type)]; }
#endif
	inline int32 GetNumParameters(EMaterialParameterType Type) const { return GetParameterTypeEntry(Type).ParameterInfos.Num(); }
	inline const FName& GetParameterName(EMaterialParameterType Type, int32 Index) const { return GetParameterTypeEntry(Type).ParameterInfos[Index].Name; }

	int32 FindParameterIndex(EMaterialParameterType Type, const FHashedMaterialParameterInfo& HashedParameterInfo) const;
	const FGuid& GetExpressionGuid(EMaterialParameterType Type, int32 Index) const;
	void GetAllParameterInfoOfType(EMaterialParameterType Type, bool bEmptyOutput, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const;
	void GetAllGlobalParameterInfoOfType(EMaterialParameterType Type, bool bEmptyOutput, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const;
	void Reset();

	UPROPERTY()
	FMaterialCachedParameterEntry RuntimeEntries[NumMaterialRuntimeParameterTypes];

	UPROPERTY()
	TArray<float> ScalarValues;

	UPROPERTY()
	TArray<FLinearColor> VectorValues;

	UPROPERTY()
	TArray<UTexture*> TextureValues;

	UPROPERTY()
	TArray<UFont*> FontValues;

	UPROPERTY()
	TArray<int32> FontPageValues;

	UPROPERTY()
	TArray<URuntimeVirtualTexture*> RuntimeVirtualTextureValues;

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
	TArray<UCurveLinearColor*> ScalarCurveValues;

	UPROPERTY()
	TArray<UCurveLinearColorAtlas*> ScalarCurveAtlasValues;

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
	FMaterialCachedExpressionContext() : bUpdateFunctionExpressions(true) {}

	bool bUpdateFunctionExpressions;
};

USTRUCT()
struct FMaterialCachedExpressionData
{
	GENERATED_USTRUCT_BODY()

	FMaterialCachedExpressionData()
		: bHasRuntimeVirtualTextureOutput(false)
		, bHasSceneColor(false)
	{}

#if WITH_EDITOR
	/** Returns 'false' if update is incomplete, due to missing expression data (stripped from non-editor build) */
	bool UpdateForExpressions(const FMaterialCachedExpressionContext& Context, const TArray<UMaterialExpression*>& Expressions, EMaterialParameterAssociation Association, int32 ParameterIndex);
	bool UpdateForFunction(const FMaterialCachedExpressionContext& Context, UMaterialFunctionInterface* Function, EMaterialParameterAssociation Association, int32 ParameterIndex);
	bool UpdateForLayerFunctions(const FMaterialCachedExpressionContext& Context, const FMaterialLayersFunctions& LayerFunctions);
#endif // WITH_EDITOR

	void Reset();

	UPROPERTY()
	FMaterialCachedParameters Parameters;

	/** Array of all texture referenced by this material */
	UPROPERTY()
	TArray<UObject*> ReferencedTextures;

	/** Array of all functions this material depends on. */
	UPROPERTY()
	TArray<FMaterialFunctionInfo> FunctionInfos;

	/** Array of all parameter collections this material depends on. */
	UPROPERTY()
	TArray<FMaterialParameterCollectionInfo> ParameterCollectionInfos;

	UPROPERTY()
	TArray<UMaterialFunctionInterface*> DefaultLayers;

	UPROPERTY()
	TArray<UMaterialFunctionInterface*> DefaultLayerBlends;

	UPROPERTY()
	TArray<ULandscapeGrassType*> GrassTypes;

	UPROPERTY()
	TArray<FName> DynamicParameterNames;

	UPROPERTY()
	TArray<bool> QualityLevelsUsed;

	UPROPERTY()
	uint32 bHasRuntimeVirtualTextureOutput : 1;

	UPROPERTY()
	uint32 bHasSceneColor : 1;
};
