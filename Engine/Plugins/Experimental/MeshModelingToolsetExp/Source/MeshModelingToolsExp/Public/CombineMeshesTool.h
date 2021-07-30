// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PropertySets/OnAcceptProperties.h"
#include "PropertySets/CreateMeshObjectTypeProperties.h"
#include "CombineMeshesTool.generated.h"

// predeclarations
struct FMeshDescription;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UCombineMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	bool bIsDuplicateTool = false;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};




UENUM()
enum class ECombineTargetType
{
	NewAsset,
	FirstInputAsset,
	LastInputAsset
};


/**
 * Standard properties
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UCombineMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (TransientToolProperty))
	bool bIsDuplicateMode = false;

	UPROPERTY(EditAnywhere, Category = AssetOptions, meta = (EditCondition = "bIsDuplicateMode == false", EditConditionHides, HideEditConditionToggle))
	ECombineTargetType WriteOutputTo = ECombineTargetType::NewAsset;

	/** Base name for newly-generated asset */
	UPROPERTY(EditAnywhere, Category = AssetOptions, meta = (TransientToolProperty, EditCondition = "bIsDuplicateMode || WriteOutputTo == ECombineTargetType::NewAsset", EditConditionHides))
	FString OutputName;

	/** Name of asset that will be updated */
	UPROPERTY(VisibleAnywhere, Category = AssetOptions, meta = (TransientToolProperty, EditCondition = "bIsDuplicateMode == false && WriteOutputTo != ECombineTargetType::NewAsset", EditConditionHides))
	FString OutputAsset;
};






/**
 * Simple tool to combine multiple meshes into a single mesh asset
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UCombineMeshesTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:
	virtual void SetDuplicateMode(bool bDuplicateMode);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

protected:

	UPROPERTY()
	TObjectPtr<UCombineMeshesToolProperties> BasicProperties;

	UPROPERTY()
	TObjectPtr<UCreateMeshObjectTypeProperties> OutputTypeProperties;

	UPROPERTY()
	TObjectPtr<UOnAcceptHandleSourcesProperties> HandleSourceProperties;

protected:
	UWorld* TargetWorld;

	bool bDuplicateMode;

	void CreateNewAsset();
	void UpdateExistingAsset();


	void BuildCombinedMaterialSet(TArray<UMaterialInterface*>& NewMaterialsOut, TArray<TArray<int32>>& MaterialIDRemapsOut);
};
