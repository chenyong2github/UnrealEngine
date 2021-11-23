// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTools/MultiSelectionMeshEditingTool.h"
#include "InteractiveToolBuilder.h"
#include "TransferMeshTool.generated.h"

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UTransferMeshToolBuilder : public UMultiSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()
public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UMultiSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};

/**
 * Standard properties of the Transfer operation
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UTransferMeshToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = Options)
	bool bTransferMaterials = true;
};



UCLASS()
class MESHMODELINGTOOLSEXP_API UTransferMeshTool : public UMultiSelectionMeshEditingTool
{
	GENERATED_BODY()

public:
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	UPROPERTY()
	TObjectPtr<UTransferMeshToolProperties> BasicProperties;
};
