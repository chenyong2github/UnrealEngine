// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Classes/Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolQueryInterfaces.h" // for UInteractiveToolExclusiveToolAPI
#include "DynamicMesh/DynamicMesh3.h"
#include "PreviewMesh.h"
#include "BakeMeshAttributeToolCommon.h"
#include "BakeMeshAttributeTool.generated.h"

/**
 * Base Mesh Bake tool
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UBakeMeshAttributeTool : public UMultiSelectionTool, public IInteractiveToolExclusiveToolAPI
{
	GENERATED_BODY()

public:
	UBakeMeshAttributeTool() = default;

	// Begin UInteractiveTool interface
	virtual void Setup() override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }
	// End UInteractiveTool interface

	void SetWorld(UWorld* World);

protected:
	//
	// Preview materials
	//
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> WorkingPreviewMaterial;
	float SecondsBeforeWorkingMaterial = 0.75;

protected:
	//
	// Bake parameters
	//
	UPROPERTY()
	TObjectPtr<UWorld> TargetWorld = nullptr;

	EBakeOpState OpState = EBakeOpState::Evaluate;


protected:
	/**
	 * Given an array of textures associated with a material,
	 * use heuristics to identify the color/albedo texture.
	 * @param Textures array of textures associated with a material.
	 * @return integer index into the Textures array representing the color/albedo texture
	 */
	static int SelectColorTextureToBake(const TArray<UTexture*>& Textures);

	/**
	 * Iterate through a primitive component's textures by material ID.
	 * @param Component the component to query
	 * @param ProcessFunc enumeration function with signature: void(int MaterialID, const TArray<UTexture*>& Textures)
	 */
	template <typename ProcessFn>
	static void ProcessComponentTextures(const UPrimitiveComponent* Component, ProcessFn&& ProcessFunc);
};


template <typename ProcessFn>
void UBakeMeshAttributeTool::ProcessComponentTextures(const UPrimitiveComponent* Component, ProcessFn&& ProcessFunc)
{
	if (!Component)
	{
		return;
	}

	TArray<UMaterialInterface*> Materials;
	Component->GetUsedMaterials(Materials);
	
	for (int32 MaterialID = 0; MaterialID < Materials.Num(); ++MaterialID)	// TODO: This won't match MaterialIDs on the FDynamicMesh3 in general, will it?
	{
		UMaterialInterface* MaterialInterface = Materials[MaterialID];
		if (MaterialInterface == nullptr)
		{
			continue;
		}

		TArray<UTexture*> Textures;
		MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
		ProcessFunc(MaterialID, Textures);
	}
}


