// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Drawing/PreviewGeometryActor.h"
#include "InteractiveTool.h"
#include "MeshWireframeComponent.h"
#include "MeshElementsVisualizer.generated.h"

class FDynamicMesh3;

/**
 * Visualization settings UMeshElementsVisualizer
 */
UCLASS()
class MODELINGCOMPONENTS_API UMeshElementsVisualizerProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Should any be mesh elements be shown */
	UPROPERTY(EditAnywhere, Category = MeshElementVisualization)
	bool bVisible = true;

	/** Should mesh wireframe be shown */
	UPROPERTY(EditAnywhere, Category = MeshElementVisualization)
	bool bShowWireframe = false;

	/** Should mesh boundary edges be shown */
	UPROPERTY(EditAnywhere, Category = MeshElementVisualization)
	bool bShowBorders = true;

	/** Should mesh uv seam edges be shown */
	UPROPERTY(EditAnywhere, Category = MeshElementVisualization)
	bool bShowUVSeams = true;

	/** Should mesh normal seam edges be shown */
	UPROPERTY(EditAnywhere, Category = MeshElementVisualization)
	bool bShowNormalSeams = true;

	/** multiplier on edge thicknesses */
	UPROPERTY(EditAnywhere, Category = MeshElementVisualization, AdvancedDisplay, meta = (UIMin = 0.1, UIMax = 10.0))
	float ThicknessScale = 1.0;

	/** Color of mesh wireframe */
	UPROPERTY(EditAnywhere, Category = MeshElementVisualization, AdvancedDisplay)
	FColor WireframeColor = FColor(128, 128, 128);

	/** Color of mesh boundary edges */
	UPROPERTY(EditAnywhere, Category = MeshElementVisualization, AdvancedDisplay)
	FColor BoundaryEdgeColor = FColor(245, 15, 15);

	/** Color of mesh UV seam edges */
	UPROPERTY(EditAnywhere, Category = MeshElementVisualization, AdvancedDisplay)
	FColor UVSeamColor = FColor(240, 160, 15);

	/** Color of mesh normal seam edges */
	UPROPERTY(EditAnywhere, Category = MeshElementVisualization, AdvancedDisplay)
	FColor NormalSeamColor = FColor(128, 128, 240);

	/** depth bias used to slightly shift depth of lines */
	UPROPERTY(EditAnywhere, Category = MeshElementVisualization, AdvancedDisplay, meta = (UIMin = -2.0, UIMax = 2.0))
	float DepthBias = 0.2;
};


/**
 * UMeshElementsVisualizer is a subclass of UPreviewGeometry that displays mesh elements.
 * Currently supports wireframe, boundary edges, UV seams, and Normal seams.
 *
 * UMeshElementsVisualizer initializes an instance of UMeshElementsVisualizerProperties
 * as its .Settings value, and will watch for changes in these properties.
 *
 * Mesh is accessed via lambda callback provided by creator/client. See SetMeshAccessFunction() comments
 */
UCLASS(Transient)
class MODELINGCOMPONENTS_API UMeshElementsVisualizer : public UPreviewGeometry
{
	GENERATED_BODY()

public:
	/**
	 * UMeshElementsVisualizer must be provided with a callback function that can be used to
	 * access the mesh. UMeshElementsVisualizer will hold onto this callback, and wrap it in
	 * an API that is passed to child Components used to render mesh geometry. Those Components
	 * will access the mesh when creating their Scene Proxies/etc. The callback can return null
	 * if the mesh has become invalid/etc
	 */
	void SetMeshAccessFunction(TUniqueFunction<const FDynamicMesh3* (void)>&& MeshAccessFunction);

	/**
	 * Call if mesh provided by MeshAccessFunction has been modified, will cause a full recomputation
	 * of all rendering data structures.
	 */
	void NotifyMeshChanged();


	/**
	 * Client must call this every frame for changes to .Settings to be reflected in rendered result.
	 */
	void OnTick(float DeltaTime);


public:
	/** Visualization settings */
	UPROPERTY()
	UMeshElementsVisualizerProperties* Settings;

	/** Mesh Wireframe component, draws wireframe, boundaries, UV seams, normal seams */
	UPROPERTY()
	UMeshWireframeComponent* WireframeComponent;


protected:
	virtual void OnCreated() override;

	bool bSettingsModified = false;

	void UpdateVisibility();

	TSharedPtr<IMeshWireframeSourceProvider> WireframeSourceProvider;
};

