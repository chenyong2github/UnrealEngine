// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"
#include "Engine/Texture.h"

#include "FractureToolBitmap.generated.h"


UCLASS()
class UFractureBitmapSettings
	: public UObject
{
	GENERATED_BODY()
public:

	UFractureBitmapSettings() 
	: Transform(FTransform::Identity)
	, Scale(FVector2D(-1, -1))
	, IsRelativeTransform(true)
	, SnapThreshold(4.0f)
	, SegmentationErrorThreshold(0.001f)
	{}

	/**
	Transform for initial pattern position and orientation.
	By default 2d pattern lies in XY plane (Y is up) the center of pattern is (0, 0)
	*/
	UPROPERTY(EditAnywhere, Category = Bitmap)
	FTransform Transform;

	/**
	Scale for pattern. Unscaled pattern has size (1, 1).
	For negative scale pattern will be placed at the center of chunk and scaled with max distance between points of its AABB
	*/
	UPROPERTY(EditAnywhere, Category = Bitmap)
	FVector2D Scale;

	/**
	If relative transform is set - position will be displacement vector from chunk's center. Otherwise from global origin.
	*/
	UPROPERTY(EditAnywhere, Category = Bitmap)
	bool IsRelativeTransform;

	/**
	The pixel distance at which neighboring Bitmapvertices and segments may be snapped into alignment. By default set it to 1
	*/
	UPROPERTY(EditAnywhere, Category = Bitmap)
	float SnapThreshold;

	/**
	Reduce the number of vertices on curve until segmentation error is smaller than this value. By default set it to 0.001
	*/
	UPROPERTY(EditAnywhere, Category = Bitmap)
	float SegmentationErrorThreshold;

	/** Cutout bitmap */
	UPROPERTY(EditAnywhere, Category = Bitmap)
	TWeakObjectPtr<UTexture2D> CutoutTexture;

};


UCLASS(DisplayName="Bitmap", Category="FractureTools")
class UFractureToolBitmap : public UFractureTool
{
public:
	GENERATED_BODY()

	UFractureToolBitmap(const FObjectInitializer& ObjInit);//  : Super(ObjInit) {}

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;

	virtual void RegisterUICommand( FFractureEditorCommands* BindingContext );
	
	virtual TArray<UObject*> GetSettingsObjects() const override;// { return TArray<UObject*>(); }
	// virtual void ExecuteFracture() {}
	// virtual bool CanExecuteFracture() { return true; }
	virtual bool CanExecuteFracture() const override;

};