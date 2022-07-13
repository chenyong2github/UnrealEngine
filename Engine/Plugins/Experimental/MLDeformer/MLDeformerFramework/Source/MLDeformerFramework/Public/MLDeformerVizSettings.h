// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"
#include "MLDeformerVizSettings.generated.h"

class UMeshDeformer;
class UAnimSequence;

/** The visualization mode, which selects whether you want to view the training data, or test your already trained model. */
UENUM()
enum class EMLDeformerVizMode : uint8
{
	/** Preview the training data. */
	TrainingData = 0,

	/** Preview testing data, used on trained models. */
	TestData
};

/** The heat map mode which selects what the colors of the heatmap represent. */
UENUM()
enum class EMLDeformerHeatMapMode : uint8
{
	/** Visualize areas where the deformer is applying corrections. The color represents the size of the correction it applies. */
	Activations = 0,

	/** Visualize the error versus the ground truth model. Requires a ground truth model to be setup. */
	GroundTruth
};

/**
 * The vizualization settings.
 */
UCLASS()
class MLDEFORMERFRAMEWORK_API UMLDeformerVizSettings : public UObject
{
	GENERATED_BODY()

public:
	// UObject overrides.
	virtual bool IsEditorOnly() const override { return true; }
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) { GetOuter()->PostEditChangeProperty(PropertyChangedEvent); } // Forward to the UMLDeformerModel.
#endif
	// ~END UObject overrides.

#if WITH_EDITORONLY_DATA
	virtual bool HasTestGroundTruth() const { return false; }

	FVector GetMeshSpacingOffsetVector() const { return FVector(MeshSpacing, 0.0f, 0.0f); }
	float GetMeshSpacing() const { return MeshSpacing; }
	float GetLabelHeight() const { return LabelHeight; }
	bool GetDrawLabels() const { return bDrawLabels; }
	float GetLabelScale() const { return LabelScale; }
	EMLDeformerVizMode GetVisualizationMode() const { return VisualizationMode; }
	int32 GetTrainingFrameNumber() const { return TrainingFrameNumber; }
	int32 GetTestingFrameNumber() const { return TestingFrameNumber; }
	float GetAnimPlaySpeed() const { return AnimPlaySpeed; }
	const UAnimSequence* GetTestAnimSequence() const { return TestAnimSequence; }
	UAnimSequence* GetTestAnimSequence() { return TestAnimSequence; }
	bool GetDrawLinearSkinnedActor() const { return bDrawLinearSkinnedActor; }
	bool GetDrawMLDeformedActor() const { return bDrawMLDeformedActor; }
	bool GetDrawGroundTruthActor() const { return bDrawGroundTruthActor; }
	bool GetShowHeatMap() const { return bShowHeatMap; }
	EMLDeformerHeatMapMode GetHeatMapMode() const { return HeatMapMode; }
	float GetHeatMapMax() const { return HeatMapMax; }
	float GetGroundTruthLerp() const { return HeatMapMode == EMLDeformerHeatMapMode::GroundTruth ? GroundTruthLerp : 0.0f; }
	UMeshDeformer* GetDeformerGraph() const { return DeformerGraph; }
	void SetDeformerGraph(UMeshDeformer* InDeformerGraph) { DeformerGraph = InDeformerGraph; }
	float GetWeight() const { return Weight; }
	bool GetXRayDeltas() const { return bXRayDeltas; }
	bool GetDrawVertexDeltas() const { return bDrawDeltas; }
#endif

public:
#if WITH_EDITORONLY_DATA
	/** The data to visualize. */
	UPROPERTY()
	EMLDeformerVizMode VisualizationMode = EMLDeformerVizMode::TrainingData;

	/** The animation sequence to play on the skeletal mesh. */
	UPROPERTY(EditAnywhere, Category = "Test Assets")
	TObjectPtr<UAnimSequence> TestAnimSequence = nullptr;

	/** The deformer graph to use on the asset editor's deformed test actor. */
	UPROPERTY(EditAnywhere, Category = "Test Assets")
	TObjectPtr<UMeshDeformer> DeformerGraph = nullptr;

	/** The play speed factor of the test anim sequence. */
	UPROPERTY(EditAnywhere, Category = "Live Settings", meta = (ClampMin = "0.0", ClampMax = "2.0", ForceUnits="Multiplier"))
	float AnimPlaySpeed = 1.0f;

	/** The frame number of the training data to visualize. */
	UPROPERTY(EditAnywhere, Category = "Training Meshes", meta = (ClampMin = "0"))
	uint32 TrainingFrameNumber = 0;

	/** Draw the text labels above each actor? */
	UPROPERTY(EditAnywhere, Category = "Shared Settings")
	bool bDrawLabels = true;

	/** The height in units to draw the labels at. */
	UPROPERTY(EditAnywhere, Category = "Shared Settings", meta = (EditCondition = "bDrawLabels", ForceUnits="cm"))
	float LabelHeight = 200.0f;

	/** The scale of the label text. */
	UPROPERTY(EditAnywhere, Category = "Shared Settings", meta = (ClampMin = "0.001", EditCondition = "bDrawLabels", ForceUnits="Multiplier"))
	float LabelScale = 1.0f;

	/** The spacing between meshes. */
	UPROPERTY(EditAnywhere, Category = "Shared Settings", meta = (ClampMin = "0", ForceUnits="cm"))
	float MeshSpacing = 125.0f;

	/** The frame number of the test data to visualize. */
	UPROPERTY(EditAnywhere, Category = "Live Settings", meta = (ClampMin = "0"))
	uint32 TestingFrameNumber = 0;

	/** Show the heat map? This will visualize the active areas of the deformer. */
	UPROPERTY(EditAnywhere, Category = "Live Settings")
	bool bShowHeatMap = false;

	/** What should the heatmap visualize? */
	UPROPERTY(EditAnywhere, Category = "Live Settings", meta = (EditCondition = "bShowHeatMap"))
	EMLDeformerHeatMapMode HeatMapMode = EMLDeformerHeatMapMode::Activations;

	/** How many centimeters does the most intense color of the heatmap represent? */
	UPROPERTY(EditAnywhere, Category = "Live Settings", meta = (EditCondition = "bShowHeatMap", ClampMin = "0.001", ForceUnits="cm"))
	float HeatMapMax = 1.0f;

	/** Lerp from ML deformed model to ground truth model when in heat map mode. */
	UPROPERTY(EditAnywhere, Category = "Live Settings", meta = (EditCondition = "HeatMapMode==EMLDeformerHeatMapMode::GroundTruth && bShowHeatMap", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float GroundTruthLerp = 0.0f;

	/** Draw the linear skinned actor? */
	UPROPERTY(EditAnywhere, Category = "Live Settings")
	bool bDrawLinearSkinnedActor = true;

	/** Draw the ML Deformed actor? */
	UPROPERTY(EditAnywhere, Category = "Live Settings", DisplayName = "Draw ML Deformed Actor")
	bool bDrawMLDeformedActor = true;

	/** Draw the ground truth actor? */
	UPROPERTY(EditAnywhere, Category = "Live Settings")
	bool bDrawGroundTruthActor = true;

	/** The scale factor of the ML deformer deltas being applied on top of the linear skinned results. */
	UPROPERTY(EditAnywhere, Category = "Live Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.0f;

	/** Draw the vertex deltas? */
	UPROPERTY(EditAnywhere, Category = "Training Meshes")
	bool bDrawDeltas = true;

	/** Enable this to draw the deltas in xray mode? */
	UPROPERTY(EditAnywhere, Category = "Training Meshes", meta = (EditCondition = "bDrawDeltas"))
	bool bXRayDeltas = true;
#endif
};
