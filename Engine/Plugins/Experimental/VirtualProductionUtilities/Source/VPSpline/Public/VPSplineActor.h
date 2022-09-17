// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "GameFramework/Actor.h"
#include "CineCameraActor.h"

#include "VPSplineComponent.h"

#if WITH_EDITOR
#include "ILevelSequenceEditorToolkit.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"

#include "Channels/MovieSceneChannel.h"
#include "Channels/MovieSceneChannelEditorData.h"
#include "MovieSceneSection.h"

#endif

#include "VPSplineActor.generated.h"

UCLASS(Blueprintable, Category = "VirtualProduction")
class VPSPLINE_API AVPSplineActor : public AActor
{
	GENERATED_BODY()
	
public:

	AVPSplineActor(const FObjectInitializer& ObjectInitializer);

	/* VPSplineComponent attached to this actor */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "VPSpline")
	TObjectPtr<UVPSplineComponent> SplineComp;

	/* Component to define the attach point. Moves along the spline from CurrentPosition property*/
	UPROPERTY(EditDefaultsOnly, Category = "VPSpline")
	TObjectPtr<USceneComponent> SplineAttachment;

	/* Normalized current parameter value to drive spline attachment*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "VPSpline", meta = (UIMin = 0.0, UIMax = 1.0))
	float CurrentPosition;

	/* Adds or updates a keyframe position from the actor*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void SetPointFromActor(const AActor* Actor);

	/* Adds or updates a keyframe position by value*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void SetPointByValue(const FVPSplinePointData& Data);

	/* Removes a keyframe position when there is a spline point data at current position*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void RemoveCurrentPoint();

	/* Bake positions as keyframes in a current sequence*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void BakePointsToSequence();

	/* Updates CurrentPosition property if there is a next keyframe*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void GoToNextPosition();

	/* Updates CurrentPosition property if there is a previous keyframe*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void GoToPrevPosition();

	virtual void Tick(float DeltaTime) override;
	virtual bool ShouldTickIfViewportsOnly() const override;
	virtual class USceneComponent* GetDefaultAttachComponent() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
	virtual void PostEditMove(bool bFinished) override;
#endif

protected:
	void UpdateSplineAttachment();

#if WITH_EDITORONLY_DATA
	/** Preview mesh for visualization */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VPSpline")
	TObjectPtr<class UStaticMesh> PreviewMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PreviewMeshComp;

	/** Determines the scale of the preview mesh */
	UPROPERTY(Transient, EditAnywhere, Category = "VPSpline")
	float PreviewMeshScale;

	void UpdatePreviewMesh();

#endif

};
