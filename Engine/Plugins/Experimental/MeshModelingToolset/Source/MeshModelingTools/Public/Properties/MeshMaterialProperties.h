// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Classes/Engine/Texture2D.h"
#include "InteractiveToolBuilder.h"
#include "MeshMaterialProperties.generated.h"


// predeclarations
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
	TWeakObjectPtr<UMaterialInterface> Material;

	/** Scale factor for generated UVs */
	UPROPERTY(EditAnywhere, Category = Material, meta = (DisplayName = "UV Scale", HideEditConditionToggle, EditConditionHides, EditCondition = "bShowExtendedOptions"))
	float UVScale = 1.0;

	/** If set, UV scales will be relative to world space so different objects created with the same UV scale should have the same average texel size */
	UPROPERTY(EditAnywhere, Category = Material, meta = (DisplayName = "UV Scale Relative to World Space", HideEditConditionToggle, EditConditionHides, EditCondition = "bShowExtendedOptions"))
	bool bWorldSpaceUVScale = false;

	/** Overlay wireframe on preview */
	UPROPERTY(EditAnywhere, Category = Material, meta = (DisplayName = "Show Wireframe", HideEditConditionToggle, EditConditionHides, EditCondition = "bShowExtendedOptions"))
	bool bWireframe = false;

	// controls visibility of UV/etc properties
	UPROPERTY()
	bool bShowExtendedOptions = true;
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

	UPROPERTY(EditAnywhere, Category = MaterialPreview, meta = (UIMin = "1.0", UIMax = "40.0", ClampMin = "0.01", ClampMax = "1000.0", EditConditionHides, EditCondition = "MaterialMode == ESetMeshMaterialMode::Checkerboard"))
	float CheckerDensity = 20.0f;

	UPROPERTY(EditAnywhere, Category = MaterialPreview, meta = (DisplayName = "Show UV Channel", EditConditionHides, EditCondition = "MaterialMode == ESetMeshMaterialMode::Checkerboard"))
	int UVChannel = 0;

	UPROPERTY(EditAnywhere, Category = MaterialPreview, meta = (EditCondition = "MaterialMode == ESetMeshMaterialMode::Override"))
	TObjectPtr<UMaterialInterface> OverrideMaterial = nullptr;

	UPROPERTY(meta = (TransientToolProperty))
	TObjectPtr<UMaterialInstanceDynamic> CheckerMaterial = nullptr;

	// Needs custom restore in order to call setup
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool, const FString& CacheIdentifier = TEXT("")) override;

	void Setup();

	void UpdateMaterials();
	UMaterialInterface* GetActiveOverrideMaterial() const;
};

UENUM()
enum class EMeshEditingMaterialModes
{
	ExistingMaterial,
	Diffuse,
	Grey,
	Soft,
	Transparent,
	TangentNormal,
	VertexColor,
	CustomImage,
	Custom
};


UCLASS()
class MESHMODELINGTOOLS_API UMeshEditingViewProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Toggle drawing of wireframe overlay on/off [Alt+W] */
	UPROPERTY(EditAnywhere, Category = Rendering)
	bool bShowWireframe = false;

	/** Set which material to use on object */
	UPROPERTY(EditAnywhere, Category = Rendering)
	EMeshEditingMaterialModes MaterialMode = EMeshEditingMaterialModes::Diffuse;

	/** Toggle flat shading on/off */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (EditConditionHides, 
		EditCondition = "MaterialMode != EMeshEditingMaterialModes::ExistingMaterial && MaterialMode != EMeshEditingMaterialModes::Transparent && MaterialMode != EMeshEditingMaterialModes::Custom") )
	bool bFlatShading = true;

	/** Main Color of Material */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (EditConditionHides, EditCondition = "MaterialMode == EMeshEditingMaterialModes::Diffuse"))
	FLinearColor Color = FLinearColor(0.4f, 0.4f, 0.4f);

	/** Image used in Image-Based Material */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (EditConditionHides, EditCondition = "MaterialMode == EMeshEditingMaterialModes::CustomImage", TransientToolProperty) )
	TObjectPtr<UTexture2D> Image;

	//~ Could have used the same property as Color, above, but the user may want different saved values for the two
	UPROPERTY(EditAnywhere, Category = Rendering, AdvancedDisplay, meta = (EditConditionHides, EditCondition = "MaterialMode == EMeshEditingMaterialModes::Transparent", DisplayName = "Color"))
	FLinearColor TransparentMaterialColor = FLinearColor(0.0606, 0.309, 0.842);

	UPROPERTY(EditAnywhere, Category = Rendering, meta = (EditConditionHides, EditCondition = "MaterialMode == EMeshEditingMaterialModes::Transparent", ClampMin = "0", ClampMax = "1.0"))
	double Opacity = 0.65;

	/** Although a two-sided transparent material causes rendering issues with overlapping faces, it is still frequently useful to see the shape when sculpting around other objects. */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (EditConditionHides, EditCondition = "MaterialMode == EMeshEditingMaterialModes::Transparent"))
	bool bTwoSided = true;

	UPROPERTY(EditAnywhere, Category = Rendering, meta = (EditConditionHides, EditCondition = "MaterialMode == EMeshEditingMaterialModes::Custom"))
	TWeakObjectPtr<UMaterialInterface> CustomMaterial;
};
