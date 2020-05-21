// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnrealUSDWrapper.h"

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Factories/MaterialImportHelpers.h"
#include "UObject/ObjectMacros.h"

#include "USDStageImportOptions.generated.h"

UENUM(BlueprintType)
enum class EReplaceActorPolicy : uint8
{
	/** Spawn new actors and components alongside the existing ones */
	Append,

	/** Replaces existing actors and components with new ones */
	Replace,

	/** Update transforms on existing actors but do not replace actors or components */
	UpdateTransform,

	/** Ignore any conflicting new assets and components, keeping the old ones */
	Ignore,
};

UENUM(BlueprintType)
enum class EReplaceAssetPolicy : uint8
{
	/** Create new assets with numbered suffixes */
	Append,

	/** Replaces existing asset with new asset */
	Replace,

	/** Ignores the new asset and keeps the existing asset */
	Ignore,
};

UCLASS(config = EditorPerProjectUserSettings)
class USDSTAGEIMPORTER_API UUsdStageImportOptions : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "DataToImport", meta = (DisplayName = "Actors"))
	bool bImportActors;

	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "DataToImport", meta = (DisplayName = "Geometry"))
	bool bImportGeometry;

	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "DataToImport", meta = (DisplayName = "Materials & Textures"))
	bool bImportMaterials;

	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "DataToImport", meta = (DisplayName = "Lights", EditCondition=bImportActors))
	bool bImportLights;

	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "DataToImport", meta = (DisplayName = "Cameras", EditCondition=bImportActors))
	bool bImportCameras;

	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "DataToImport", meta = (DisplayName = "Animations", EditCondition=bImportActors))
	bool bImportAnimations;

	/** Whether or not to import custom properties and set their unreal equivalent on spawned actors */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "DataToImport", meta = (DisplayName = "Custom Properties", EditCondition=bImportActors))
	bool bImportProperties;



	/** Only import prims with these specific purposes from the USD file */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category= "ImportSettings", meta = (Bitmask, BitmaskEnum=EUsdPurpose))
	int32 PurposesToImport;

	/** Time to evaluate the USD Stage for import */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category= "ImportSettings", meta = (DisplayName = "Time"))
	float ImportTime;

	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "ImportSettings", meta = (ClampMin = 0.001f, ClampMax = 1000.0f, DisplayName = "Meters per unit"))
	float MetersPerUnit;



	/**
	 * If checked, To enforce unique asset paths, all assets will be created in directories that match with their prim path
	 * e.g a USD path /root/myassets/myprim_mesh will generate the path in the game directory "/Game/myassets/" with a mesh asset called "myprim_mesh" within that path.
	 */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "AssetCollision")
	bool bGenerateUniquePathPerUSDPrim;

	/**
	 * This setting determines what to do if more than one USD prim is found with the same name.  If this setting is true a unique name will be generated and a unique asset will be imported
	 * If this is false, the first asset found is generated. Assets will be reused when spawning actors into the world.
	 */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "AssetCollision", meta=(EditCondition=bImportGeometry))
	bool bGenerateUniqueMeshes;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, config, Category = "AssetCollision")
	EMaterialSearchLocation MaterialSearchLocation;

	/** What should happen when imported actors and components try to overwrite existing actors and components */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "AssetCollision", meta=(EditCondition=bImportActors))
	EReplaceActorPolicy ExistingActorPolicy;

	/** What should happen when imported assets try to overwrite existing assets */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "AssetCollision")
	EReplaceAssetPolicy ExistingAssetPolicy;



	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "PostProcess", meta=(EditCondition=bImportGeometry))
	bool bApplyWorldTransformToGeometry;

	/** If checked, all actors generated will have a world space transform and will not have any attachment hierarchy */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "PostProcess", meta=(EditCondition=bImportActors))
	bool bFlattenHierarchy;

	/** Attempt to combine assets and components whenever possible */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "PostProcess", meta=(DisplayName="Collapse assets and components"))
	bool bCollapse;

public:
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
};
