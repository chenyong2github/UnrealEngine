// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "GameFramework/Actor.h"

#include "CameraCalibrationCheckerboard.generated.h"

class UCalibrationPointComponent;
class UStaticMesh;
class UMaterial;

/**
 * Dynamic checkerboad actor
 */
UCLASS()
class CAMERACALIBRATIONCORE_API ACameraCalibrationCheckerboard : public AActor
{
	GENERATED_BODY()

private:

	using Super = AActor;

public:

	ACameraCalibrationCheckerboard();

public:

	/** Rebuilds the instanced components that make up this checkerboard */
	UFUNCTION(BlueprintCallable, Category = "Calibration")
	void Rebuild();

	//~ Begin UObject Interface
	virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

public:

	/** Root component, gives the Actor a transform */
	UPROPERTY()
	USceneComponent* Root;

	/** TopLeft calibration point */
	UPROPERTY()
	UCalibrationPointComponent* TopLeft;

	/** TopRight calibration point */
	UPROPERTY()
	UCalibrationPointComponent* TopRight;

	/** BottomLeft calibration point */
	UPROPERTY()
	UCalibrationPointComponent* BottomLeft;

	/** BottomRight calibration point */
	UPROPERTY()
	UCalibrationPointComponent* BottomRight;

	/** Center calibration point */
	UPROPERTY()
	UCalibrationPointComponent* Center;


	/** Number of rows. It is one of the parameters cv::findChessboardCorners needs */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	int32 NumCornerRows = 2;

	/** Number of columns. It is one of the parameters cv::findChessboardCorners needs */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	int32 NumCornerCols = 2;

	/** Length of the side of each square. It is used when building the object points that cv::calibrateCamera needs */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	float SquareSideLength = 3.2089f;

	/** Thickness of checkerboard. Not used for calibration purposes. */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	float Thickness = 0.1f;

	/** The static mesh that we are going to use for all the cubes */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	UStaticMesh* CubeMesh;

	/** The material that we are going to use for all the odd cubes */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	UMaterial* OddCubeMaterial;

	/** The material that we are going to use for all the even cubes */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	UMaterial* EvenCubeMaterial;
};
