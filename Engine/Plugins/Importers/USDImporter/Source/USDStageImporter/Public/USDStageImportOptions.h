// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnrealUSDWrapper.h"
#include "USDStageOptions.h"

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

	/** Whether to try importing UAnimSequence skeletal animation assets for each encountered UsdSkelAnimQuery */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "DataToImport", meta = (EditCondition = bImportGeometry, DisplayName = "Skeletal Animations"))
	bool bImportSkeletalAnimations;

	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "DataToImport", meta = (DisplayName = "Materials & Textures"))
	bool bImportMaterials;



	/** Only import prims with these specific purposes from the USD file */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category= "USD options", meta = (Bitmask, BitmaskEnum=EUsdPurpose))
	int32 PurposesToImport;

	/** Specifies which set of shaders to use, defaults to universal. */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category= "USD options")
	FName RenderContextToImport;

	/** Time to evaluate the USD Stage for import */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category= "USD options", meta = (DisplayName = "Time"))
	float ImportTime;

	/** Whether to use the specified StageOptions instead of the stage's own settings */
	UPROPERTY( BlueprintReadWrite, config, EditAnywhere, Category = "USD options" )
	bool bOverrideStageOptions;

	/** Custom StageOptions to use for the stage */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "USD options", meta = ( EditCondition = bOverrideStageOptions ) )
	FUsdStageOptions StageOptions;



	/**
	 * If enabled, whenever two different prims import into identical assets, only one of those assets will be kept and reused.
	 */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "Collision", meta=(EditCondition=bImportGeometry))
	bool bReuseIdenticalAssets;

	/** What should happen when imported actors and components try to overwrite existing actors and components */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "Collision", meta=(EditCondition=bImportActors))
	EReplaceActorPolicy ExistingActorPolicy;

	/** What should happen when imported assets try to overwrite existing assets */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "Collision")
	EReplaceAssetPolicy ExistingAssetPolicy;



	/**
	 * When enabled, assets will be imported into a content folder structure according to their prim path. When disabled,
	 * assets are imported into content folders according to asset type (e.g. 'Materials', 'StaticMeshes', etc).
	 */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "Processing")
	bool bPrimPathFolderStructure;

	/** Attempt to combine assets and components whenever possible */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category = "Processing", meta=(DisplayName="Collapse assets and components"))
	bool bCollapse;

	/** When true, if a prim has a "LOD" variant set with variants named "LOD0", "LOD1", etc. where each contains a UsdGeomMesh, the importer will attempt to parse the meshes as separate LODs of a single UStaticMesh. When false, only the selected variant will be parsed as LOD0 of the UStaticMesh.  */
	UPROPERTY(BlueprintReadWrite, config, EditAnywhere, Category="Processing", meta=(DisplayName="Interpret LOD variant sets", EditCondition=bImportGeometry) )
	bool bInterpretLODs;

public:
	void EnableActorImport(bool bEnable);
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
};
