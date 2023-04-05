// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "InterchangeGLTFMaterialInstance.generated.h"

namespace GLTF { 
	struct FMaterial; 
	struct FTexture;
}
class UInterchangeBaseNodeContainer;
class UInterchangeMaterialInstanceNode;
class UInterchangeShaderGraphNode;

// Note: Material Instances have the following known issues:
// - Translucent Unlit material config is not supported
// - Multi UV not supported
// - ClearCoat.NormalMapUVScale not supported

USTRUCT(BlueprintType)
struct FGLTFMaterialInstanceSettings
{
	GENERATED_USTRUCT_BODY()

public:
	FGLTFMaterialInstanceSettings();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Material", meta = (AllowedClasses = "/Script/Engine.MaterialFunction"))
	TMap<FString, FSoftObjectPath> MaterialParents;

	TArray<FString> ValidateMaterialInstancesAndParameters() const; //At the moment this is not used, because MaterialParents override is not exposed just yet.

	bool ProcessGltfMaterial(UInterchangeBaseNodeContainer& NodeContainer, const GLTF::FMaterial& GltfMaterial, const TArray<GLTF::FTexture>& Textures) const;

	bool AddGltfMaterialValuesToShaderGraphNode(const GLTF::FMaterial& GltfMaterial, const TArray<GLTF::FTexture>& Textures, UInterchangeShaderGraphNode* ShaderGraphNode) const;

	static void CreateMaterialInstanceFromShaderGraphNode(UInterchangeBaseNodeContainer& NodeContainer, UInterchangeShaderGraphNode* ShaderGraphNode);

private:
	TSet<FString> GenerateExpectedParametersList(const FString& Identifier) const;

	TArray<FString> ExpectedMaterialInstanceIdentifiers; //Default MaterialInstance' identifiers
};