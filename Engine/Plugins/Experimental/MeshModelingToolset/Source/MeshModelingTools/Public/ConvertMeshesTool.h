// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "PropertySets/CreateMeshObjectTypeProperties.h"
#include "ConvertMeshesTool.generated.h"

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UConvertMeshesToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};

/**
 * Standard properties of the Transfer operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UConvertMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = Options)
	bool bTransferMaterials = true;
};



UCLASS()
class MESHMODELINGTOOLS_API UConvertMeshesTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:
	virtual void SetWorld(UWorld* World);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	UPROPERTY()
	TObjectPtr<UConvertMeshesToolProperties> BasicProperties;

	UPROPERTY()
	TObjectPtr<UCreateMeshObjectTypeProperties> OutputTypeProperties;

protected:
	UWorld* TargetWorld;
};
