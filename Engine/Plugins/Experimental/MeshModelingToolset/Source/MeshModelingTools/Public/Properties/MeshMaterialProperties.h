// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InteractiveToolBuilder.h"

#include "MeshMaterialProperties.generated.h"


// predeclarations
class FDynamicMesh3;
class UMaterialInterface;
class UMaterialInstanceDynamic;


// Standard material property settings for tools that generate new meshes
UCLASS()
class MESHMODELINGTOOLS_API UNewMeshMaterialProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UNewMeshMaterialProperties();

	/** Material for new mesh*/
	UPROPERTY(EditAnywhere, NonTransactional, Category = Material, meta = (DisplayName = "Material"))
	UMaterialInterface* Material;

	/** Scale factor for generated UVs */
	UPROPERTY(EditAnywhere, Category = Material, meta = (DisplayName = "UV Scale"))
	float UVScale = 1.0;

	/** If set, UV scales will be relative to world space so different objects created with the same UV scale should have the same average texel size */
	UPROPERTY(EditAnywhere, Category = Material, meta = (DisplayName = "UV Scale Relative to World Space"))
	bool bWorldSpaceUVScale = false;

	/** Overlay wireframe on preview */
	UPROPERTY(EditAnywhere, Category = Material, meta = (DisplayName = "Show Wireframe"))
	bool bWireframe = false;


	//
	// save/restore support
	//
	virtual void SaveProperties(UInteractiveTool* SaveFromTool) override;
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool) override;
};



/** Standard material modes for tools that need to set custom materials for visualization */
UENUM()
enum class ESetMeshMaterialMode : uint8
{
	/** Input material */
	KeepOriginal,

	/** Checkerboard material */
	Checkerboard,

	/** Override material */
	Override
};

// Standard material property settings for tools that visualize materials on existing meshes (e.g. to help show UVs)
UCLASS()
class MESHMODELINGTOOLS_API UExistingMeshMaterialProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** Material that will be used on the mesh */
	UPROPERTY(EditAnywhere, Category = MaterialPreview)
	ESetMeshMaterialMode MaterialMode = ESetMeshMaterialMode::KeepOriginal;

	UPROPERTY(EditAnywhere, Category = MaterialPreview, meta = (UIMin = "1.0", UIMax = "40.0", ClampMin = "0.01", ClampMax = "1000.0", EditCondition = "MaterialMode == ESetMeshMaterialMode::Checkerboard"))
	float CheckerDensity = 20.0f;

	UPROPERTY(EditAnywhere, Category = MaterialPreview, meta = (EditCondition = "MaterialMode == ESetMeshMaterialMode::Override"))
	UMaterialInterface* OverrideMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* CheckerMaterial = nullptr;

	void Setup();

	void UpdateMaterials();
	void SetMaterialIfChanged(UMaterialInterface* OriginalMaterial, UMaterialInterface* CurrentMaterial, TFunctionRef<void(UMaterialInterface* Material)> SetMaterialFn);
};






UENUM()
enum class EMeshEditingMaterialModes
{
	ExistingMaterial = 0,
	MeshFocusMaterial = 1
};


UCLASS()
class MESHMODELINGTOOLS_API UMeshEditingViewProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Toggle drawing of wireframe overlay on/off [Alt+W] */
	UPROPERTY(EditAnywhere, Category = ViewOptions)
	bool bShowWireframe = false;

	/** Set which material to use on object */
	UPROPERTY(EditAnywhere, Category = ViewOptions)
	EMeshEditingMaterialModes MaterialMode = EMeshEditingMaterialModes::MeshFocusMaterial;

	virtual void SaveProperties(UInteractiveTool* SaveFromTool) override;
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool) override;
};
