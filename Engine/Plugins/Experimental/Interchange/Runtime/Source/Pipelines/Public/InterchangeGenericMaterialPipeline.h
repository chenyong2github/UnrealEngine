// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeGenericMaterialPipeline.generated.h"

class UInterchangeBaseMaterialFactoryNode;
class UInterchangeFunctionCallShaderNode;
class UInterchangeShaderGraphNode;
class UInterchangeShaderNode;
class UInterchangeMaterialFactoryNode;
class UInterchangeMaterialExpressionFactoryNode;
class UInterchangeMaterialFunctionFactoryNode;
class UInterchangeMaterialInstanceFactoryNode;
class UInterchangeResult;

UENUM(BlueprintType)
enum class EInterchangeMaterialImportOption : uint8
{
	DoNotImport,
	ImportAsMaterials,
	ImportAsMaterialInstances,
};

UCLASS(BlueprintType, hidedropdown, Experimental)
class INTERCHANGEPIPELINES_API UInterchangeGenericMaterialPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	EInterchangeMaterialImportOption MaterialImport = EInterchangeMaterialImportOption::ImportAsMaterials;

	/** Optional material used as the parent when importing materials as instances. If no parent material is specified, one will be automatically selected during the import process. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials", Meta= (EditCondition="MaterialImport==EInterchangeMaterialImportOption::ImportAsMaterialInstances", AllowedClasses="/Script/Engine.MaterialInterface"))
	FSoftObjectPath ParentMaterial;

	virtual void AdjustSettingsForContext(EInterchangePipelineContext ImportType, TObjectPtr<UObject> ReimportAsset) override;

protected:
	virtual void ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas) override;

	UPROPERTY()
	TObjectPtr<UInterchangeBaseNodeContainer> BaseNodeContainer;

	TArray<const UInterchangeSourceData*> SourceDatas;
	
private:
	/** Material translated assets nodes */
	TArray<UInterchangeShaderGraphNode*> MaterialNodes;
	
	/** Material factory assets nodes */
	TArray<UInterchangeBaseMaterialFactoryNode*> MaterialFactoryNodes;

	UInterchangeBaseMaterialFactoryNode* CreateBaseMaterialFactoryNode(const UInterchangeBaseNode* MaterialNode, TSubclassOf<UInterchangeBaseMaterialFactoryNode> NodeType);
	UInterchangeMaterialFactoryNode* CreateMaterialFactoryNode(const UInterchangeShaderGraphNode* ShaderGraphNode);
	UInterchangeMaterialFunctionFactoryNode* CreateMaterialFunctionFactoryNode(const UInterchangeShaderGraphNode* FunctionCallShaderNode);
	UInterchangeMaterialInstanceFactoryNode* CreateMaterialInstanceFactoryNode(const UInterchangeShaderGraphNode* ShaderGraphNode);

	/** True if the shader graph has a clear coat input. */
	bool IsClearCoatModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const;

	/** True if the shader graph has a sheen color input. */
	bool IsSheenModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const;

	/** True if the shader graph has a subsurface color input. */
	bool IsSubsurfaceModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const;

	/** True if the shader graph has a transmission color input. */
	bool IsThinTranslucentModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const;

	/** True if the shader graph has a base color input. */
	bool IsPBRModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const;

	/** True if the shader graph has diffuse color and specular color inputs. */
	bool IsPhongModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const;

	/** True if the shader graph has a diffuse color input. */
	bool IsLambertModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const;

	/** True if the shader graph has the standard surface's shader type name */
	bool IsStandardSurfaceModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const;

	bool HandlePhongModel(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode);
	bool HandleLambertModel(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode);
	bool HandlePBRModel(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode);
	bool HandleStandardSurfaceModel(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode);
	bool HandleClearCoat(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode);
	bool HandleSubsurface(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode);
	bool HandleSheen(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode);
	bool HandleThinTranslucent(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode);
	void HandleCommonParameters(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode);
	bool HandleBxDFInput(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode);

	void HandleFlattenNormalNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* FlattenNormalFactoryNode);
	void HandleMakeFloat3Node(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* MakeFloat3FactoryNode);
	void HandleTextureSampleNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* TextureSampleFactoryNode);
	void HandleTextureCoordinateNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode*& TextureSampleFactoryNode);
	void HandleLerpNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* LerpFactoryNode);
	void HandleMaskNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* MaskFactoryNode);

	UInterchangeMaterialExpressionFactoryNode* CreateMaterialExpressionForShaderNode(UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, const UInterchangeShaderNode* ShaderNode, const FString& ParentUid);
	TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> CreateMaterialExpressionForInput(UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, const UInterchangeShaderNode* ShaderNode, const FString& InputName, const FString& ParentUid);

	UInterchangeMaterialExpressionFactoryNode* CreateExpressionNode(const FString& ExpressionName, const FString& ParentUid, UClass* MaterialExpressionClass);
	UInterchangeMaterialExpressionFactoryNode* CreateScalarParameterExpression(const UInterchangeShaderNode* ShaderNode, const FString& InputName, const FString& ParentUid);
	UInterchangeMaterialExpressionFactoryNode* CreateVectorParameterExpression(const UInterchangeShaderNode* ShaderNode, const FString& InputName, const FString& ParentUid);
	UInterchangeMaterialExpressionFactoryNode* CreateVector2ParameterExpression(const UInterchangeShaderNode* ShaderNode, const FString& InputName, const FString& ParentUid);

	/**
	 * Visits a given shader node and its connections to find its strongest value.
	 * Only its first input is visited as it's assumed that it's the most impactful.
	 * The goal is to simplify a branch of a node graph to a single value, to be used for material instancing.
	 */
	TVariant<FString, FLinearColor, float> VisitShaderNode(const UInterchangeShaderNode* ShaderNode) const;
	TVariant<FString, FLinearColor, float> VisitShaderInput(const UInterchangeShaderNode* ShaderNode, const FString& InputName) const;

	/** 
	 * Returns the strongest value in a lerp.
	 * If we're lerping between scalars or colors, the lerp result will get computed and returned.
	 * If we're lerping between textures, the strongest one is returned based on the lerp factor.
	 */
	TVariant<FString, FLinearColor, float> VisitLerpNode(const UInterchangeShaderNode* ShaderNode) const;
	TVariant<FString, FLinearColor, float> VisitMultiplyNode(const UInterchangeShaderNode* ShaderNode) const;
	TVariant<FString, FLinearColor, float> VisitOneMinusNode(const UInterchangeShaderNode* ShaderNode) const;
	TVariant<FString, FLinearColor, float> VisitTextureSampleNode(const UInterchangeShaderNode* ShaderNode) const;

private:
	bool bParsingForNormalInput;
	bool bParsingForLinearInput; // True when parsing non-color inputs (metallic, roughness, specular, etc.)
};


