// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "BaseTools/SingleSelectionMeshEditingTool.h"
#include "BoneIndices.h"
#include "TransformTypes.h"

#include "ClothTransferSkinWeightsTool.generated.h"

class UChaosClothComponent;
class UClothTransferSkinWeightsTool;
class USkeletalMesh;
class UPreviewMesh;
class USkeletalMeshComponent;

UCLASS()
class CHAOSCLOTHASSETEDITORTOOLS_API UClothTransferSkinWeightsToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = Source)
	TObjectPtr<USkeletalMesh> SourceMesh;

	UPROPERTY(EditAnywhere, Category = Source)
	FTransform SourceMeshTransform;

	UPROPERTY(EditAnywhere, Category = Source)
	bool bHideSourceMesh = false;

	UPROPERTY(EditAnywhere, Category = Visualization, meta = (GetOptions = GetBoneNameList))
	FName BoneName;

	// Get the list of valid bone names
	UFUNCTION()
	TArray<FName> GetBoneNameList()
	{
		return BoneNameList;
	}

	UPROPERTY(meta = (TransientToolProperty))
	TArray<FName> BoneNameList;

};

UENUM()
enum class EClothTransferSkinWeightsToolActions
{
	NoAction,
	Transfer
};

UCLASS()
class CHAOSCLOTHASSETEDITORTOOLS_API UClothTransferSkinWeightsToolActionProperties : public UObject
{
	GENERATED_BODY()
public:

	TWeakObjectPtr<UClothTransferSkinWeightsTool> ParentTool;

	void Initialize(UClothTransferSkinWeightsTool* ParentToolIn) { ParentTool = ParentToolIn; }

	void PostAction(EClothTransferSkinWeightsToolActions Action);

	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayName = "Transfer weights", DisplayPriority = 1))
	void TransferWeights()
	{
		PostAction(EClothTransferSkinWeightsToolActions::Transfer);
	}

};


UCLASS()
class CHAOSCLOTHASSETEDITORTOOLS_API UClothTransferSkinWeightsToolBuilder : public USingleSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

protected:

	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;

public:

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;

	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
	virtual void PostSetupTool(UInteractiveTool* Tool, const FToolBuilderState& SceneState) const override;
};


UCLASS()
class CHAOSCLOTHASSETEDITORTOOLS_API UClothTransferSkinWeightsTool : public USingleSelectionMeshEditingTool
{
	GENERATED_BODY()
public:

	// UInteractiveTool
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnTick(float DeltaTime) override;

private:

	friend class UClothTransferSkinWeightsToolActionProperties;
	friend class UClothTransferSkinWeightsToolBuilder;

	UPROPERTY()
	TObjectPtr<UClothTransferSkinWeightsToolProperties> ToolProperties;

	UPROPERTY()
	TObjectPtr<UClothTransferSkinWeightsToolActionProperties> ActionProperties;

	EClothTransferSkinWeightsToolActions PendingAction = EClothTransferSkinWeightsToolActions::NoAction;

	UPROPERTY()
	TObjectPtr<const UChaosClothComponent> ClothComponent;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> SourceComponent;


	void RequestAction(EClothTransferSkinWeightsToolActions ActionType);

	void TransferWeights();

	UPROPERTY()
	TObjectPtr<UPreviewMesh> PreviewMesh;

	TMap<FName, FBoneIndexType> TargetMeshBoneNameToIndex;

	void UpdatePreviewMeshColor();
	void UpdatePreviewMesh();

	void UpdateSourceMeshRender();
};


