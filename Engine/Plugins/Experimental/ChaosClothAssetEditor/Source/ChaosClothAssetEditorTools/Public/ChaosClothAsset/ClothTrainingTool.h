// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "Animation/AnimSequence.h"
#include "ModelingOperators.h"
#include "MeshOpPreviewHelpers.h"
#include "Misc/SlowTask.h"

#include "ClothTrainingTool.generated.h"

class UChaosCache;
class UChaosCacheCollection;
class UChaosClothComponent;
class UClothTrainingTool;


// @@@@@@@@@ TODO: Change this to whatever makes sense for output
struct FSkinnedMeshVertices
{
	TArray<FVector3f> Vertices;
};

UCLASS()
class CHAOSCLOTHASSETEDITORTOOLS_API UClothTrainingToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = Input)
	TObjectPtr<UAnimSequence> AnimationSequence;

	/* e.g. "0, 2, 5-10, 12-15". If left empty, all frames will be used */
	UPROPERTY(EditAnywhere, Category = Input)
	FString FramesToSimulate;

	UPROPERTY(EditAnywhere, Category = Output, meta = (DisplayName = "Generated Cache", EditCondition = "!bDebug"))
	TObjectPtr<UChaosCacheCollection> CacheCollection;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bDebug = false;

	UPROPERTY(EditAnywhere, Category = Output, meta = (Min = 0, EditCondition = "bDebug"))
	uint32 DebugFrame = 0;

	UPROPERTY(EditAnywhere, Category = Output, meta = (EditCondition = "bDebug", DisplayName = "Debug Cache"))
	TObjectPtr<UChaosCacheCollection> DebugCacheCollection;

	UPROPERTY(EditAnywhere, Category = "Simulation Settings", meta = (Min = 0))
	float TimeStep = 1.f / 30;

	UPROPERTY(EditAnywhere, Category = "Simulation Settings", meta = (Min = 0))
	int32 NumSteps = 200;

	UPROPERTY(EditAnywhere, Category = "Simulation Settings", meta = (Min = 1, EditCondition = "!bDebug"))
	int32 NumThreads = 1;
};

UENUM()
enum class EClothTrainingToolActions
{
	NoAction,
	Train
};

UCLASS()
class CHAOSCLOTHASSETEDITORTOOLS_API UClothTrainingToolActionProperties : public UObject
{
	GENERATED_BODY()
public:

	TWeakObjectPtr<UClothTrainingTool> ParentTool;

	void Initialize(UClothTrainingTool* ParentToolIn) { ParentTool = ParentToolIn; }

	void PostAction(EClothTrainingToolActions Action);

	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayName = "Begin Generating", DisplayPriority = 1))
		void StartGenerating()
	{
		PostAction(EClothTrainingToolActions::Train);
	}

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
	struct FSimResource;
	class FLaunchSimsOp;

	friend class UClothTrainingToolActionProperties;

	UPROPERTY()
	TObjectPtr<UClothTrainingToolProperties> ToolProperties;

	UPROPERTY()
	TObjectPtr<UClothTrainingToolActionProperties> ActionProperties;

	EClothTrainingToolActions PendingAction = EClothTrainingToolActions::NoAction;

	UPROPERTY()
	TObjectPtr<const UChaosClothComponent> ClothComponent;

	TUniquePtr<FCriticalSection> SimMutex;
	TArray<FSimResource> SimResources;

	void RequestAction(EClothTrainingToolActions ActionType);
	void RunTraining();
	bool IsClothComponentValid() const;
	UChaosCacheCollection* GetCacheCollection() const;
	bool SaveCacheCollection(UChaosCacheCollection* CacheCollection) const;
	void PrepareAnimationSequence();
	void RestoreAnimationSequence();
	bool AllocateSimResources_GameThread(int32 Num);
	void FreeSimResources_GameThread();

};


