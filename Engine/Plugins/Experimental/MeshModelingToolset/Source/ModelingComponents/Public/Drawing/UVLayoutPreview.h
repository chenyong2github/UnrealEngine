// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PreviewMesh.h"
#include "TriangleSetComponent.h"
#include "InteractiveTool.h"
#include "UVLayoutPreview.generated.h"

class FDynamicMesh3;



/**
 * Where should in-viewport UVLayoutPreview be shown, relative to target object
 */
UENUM()
enum class EUVLayoutPreviewSide
{
	Left = 0,
	Right = 1
};


/**
 * Visualization settings for UV layout preview
 */
UCLASS()
class MODELINGCOMPONENTS_API UUVLayoutPreviewProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Should be UV Layout be shown */
	UPROPERTY(EditAnywhere, Category = UVLayoutPreview)
	bool bVisible = true;

	/** World-space scaling factor on the UV Layout */
	UPROPERTY(EditAnywhere, Category = UVLayoutPreview, meta = (UIMin = "0.1", UIMax = "10.0", ClampMin = "0.0001", ClampMax = "1000"))
	float ScaleFactor = 1.0;

	/** Where should the UV layout be positioned relative to the target object, relative to camera */
	UPROPERTY(EditAnywhere, Category = UVLayoutPreview)
	EUVLayoutPreviewSide WhichSide = EUVLayoutPreviewSide::Right;

	/** If true, wireframe is shown for the UV layout */
	UPROPERTY(EditAnywhere, Category = UVLayoutPreview)
	bool bShowWireframe = true;

	UPROPERTY(EditAnywhere, Category = UVLayoutPreview)
	FVector2D Shift = FVector2D(1.0, 0.5);
};




/**
 * UUVLayoutPreview is a utility object that creates and manages a 3D plane on which a UV layout
 * for a 3D mesh is rendered. The UV layout
 */
UCLASS(Transient)
class MODELINGCOMPONENTS_API UUVLayoutPreview : public UObject
{
	GENERATED_BODY()

public:
	virtual ~UUVLayoutPreview();


	/**
	 * Create preview mesh in the World with the given transform
	 */
	void CreateInWorld(UWorld* World);

	/**
	 * Remove and destroy preview mesh
	 */
	void Disconnect();


	/**
	 * Configure material set for UV-space preview mesh
	 */
	void SetSourceMaterials(const FComponentMaterialSet& MaterialSet);

	/**
	 * Specify the current world transform/bounds for the target object. 
	 * UV layout preview is positioned relative to this box
	 */
	void SetSourceWorldPosition(FTransform WorldTransform, FBox WorldBounds);

	/**
	 * Update the current camera state, used to auto-position the UV layout preview
	 */
	void SetCurrentCameraState(const FViewCameraState& CameraState);

	/**
	 * Notify the UV Layout Preview that the source UVs have been modified
	 */
	void UpdateUVMesh(const FDynamicMesh3* SourceMesh, int32 SourceUVLayer = 0);

	/**
	 * Tick the UV Layout Preview, allowing it to upodate various settings
	 */
	void OnTick(float DeltaTime);

	/**
	 * Render the UV Layout Preview, allowing it to upodate various settings
	 */
	void Render(IToolsContextRenderAPI* RenderAPI);


	/**
	 * Set the transform on the UV Layout preview mesh
	 */
	void SetTransform(const FTransform& UseTransform);

	/**
	 * Set the visibility of the UV Layout  preview mesh
	 */
	void SetVisible(bool bVisible);



public:
	/** Visualization settings */
	UPROPERTY()
	UUVLayoutPreviewProperties* Settings;

	/** PreviewMesh is initialized with a copy of an input mesh with UVs mapped to position, ie such that (X,Y,Z) = (U,V,0) */
	UPROPERTY()
	UPreviewMesh* PreviewMesh;

	/** Set of additional triangles to draw, eg for backing rectangle, etc */
	UPROPERTY()
	UTriangleSetComponent* TriangleComponent;

	/** Configure whether the backing rectangle should be shown */
	UPROPERTY()
	bool bShowBackingRectangle = true;

	/** Configure the backing rectangle material */
	UPROPERTY()
	UMaterialInterface* BackingRectangleMaterial = nullptr;


protected:
	void RecalculatePosition();

	FComponentMaterialSet SourceMaterials;

	FFrame3d SourceObjectFrame;
	FAxisAlignedBox3d SourceObjectWorldBounds;

	FFrame3d CurrentWorldFrame;

	FViewCameraState CameraState;

	bool bSettingsModified = false;

	float GetCurrentScale();
};