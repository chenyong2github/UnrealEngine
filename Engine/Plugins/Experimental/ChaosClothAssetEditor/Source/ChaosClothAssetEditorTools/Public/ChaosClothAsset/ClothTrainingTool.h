// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "ModelingOperators.h"
#include "MeshOpPreviewHelpers.h"
#include "Misc/DateTime.h"

#include "ClothTrainingTool.generated.h"

struct FCacheUserToken;
class FAsyncTaskNotification;
class UAnimSequence;
class UChaosClothComponent;
class UClothTrainingToolActionProperties;
class UClothTrainingToolProperties;
class UGeometryCache;

namespace UE::Chaos::ClothAsset
{
	class FClothSimulationDataGenerationProxy;
};

UENUM()
enum class EClothTrainingToolActions
{
	NoAction,
	StartTrain,
	TickTrain
};


UCLASS()
class CHAOSCLOTHASSETEDITORTOOLS_API UClothTrainingToolBuilder : public UInteractiveToolWithToolTargetsBuilder
{
	GENERATED_BODY()

protected:

	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;

public:

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;

	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


UCLASS()
class CHAOSCLOTHASSETEDITORTOOLS_API UClothTrainingTool : public USingleSelectionTool
{
	GENERATED_BODY()
public:
	UClothTrainingTool();
	UClothTrainingTool(FVTableHelper& Helper);
	~UClothTrainingTool();
	// UInteractiveTool
	virtual void Setup() override;
	virtual void OnTick(float DeltaTime) override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

private:
	class FClothSimulationDataGenerationProxy;
	class FLaunchSimsOp;
	struct FSimResource;
	struct FTaskResource;

	using FTaskType = UE::Geometry::TModelingOpTask<FLaunchSimsOp>;
	using FExecuterType = UE::Geometry::FAsyncTaskExecuterWithProgressCancel<FTaskType>;
	using FProxy = UE::Chaos::ClothAsset::FClothSimulationDataGenerationProxy;

	friend class UClothTrainingToolActionProperties;
	friend class UClothTrainingToolProperties;

	UPROPERTY()
	TObjectPtr<UClothTrainingToolProperties> ToolProperties;

	UPROPERTY()
	TObjectPtr<UClothTrainingToolActionProperties> ActionProperties;

	EClothTrainingToolActions PendingAction = EClothTrainingToolActions::NoAction;

	UPROPERTY()
	TObjectPtr<const UChaosClothComponent> ClothComponent;

	TUniquePtr<FTaskResource> TaskResource;

	void RequestAction(EClothTrainingToolActions ActionType);
	void StartTraining();
	void TickTraining();
	void RunTraining();
	void FreeTaskResource(bool bCancelled);
	bool IsClothComponentValid() const;
	UGeometryCache* GetCache() const;
};


