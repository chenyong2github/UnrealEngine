// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "MeshPaintingToolsetTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "IMeshPaintComponentAdapter.h"

class FMeshPaintParameters;
class UImportVertexColorOptions;
class UTexture2D;
class UStaticMeshComponent;
class UStaticMesh;
class USkeletalMesh;
class IMeshPaintComponentAdapter;
class UPaintBrushSettings;
class FEditorViewportClient;
class UMeshComponent;
class USkeletalMeshComponent;
class UViewportInteractor;
class FViewport;
class FPrimitiveDrawInterface;
class FSceneView;

struct FStaticMeshComponentLODInfo;
struct FPerComponentVertexColorData;

enum class EMeshPaintDataColorViewMode : uint8;

class MESHPAINTEDITORMODE_API UMeshPaintModeHelpers : public UBlueprintFunctionLibrary
{
public:
	/** Forces the Viewport Client to render using the given Viewport Color ViewMode */
	static void SetViewportColorMode(EMeshPaintDataColorViewMode ColorViewMode, FEditorViewportClient* ViewportClient);

	/** Sets whether or not the level viewport should be real time rendered move or viewport as parameter? */
	static void SetRealtimeViewport(bool bRealtime);


	/** Helper function to import Vertex Colors from a Texture to the specified MeshComponent (makes use of SImportVertexColorsOptions Widget) */
	static void ImportVertexColorsFromTexture(UMeshComponent* MeshComponent);

	/** Imports vertex colors from a Texture to the specified Skeletal Mesh according to user-set options */
	static void ImportVertexColorsToSkeletalMesh(USkeletalMesh* SkeletalMesh, const UImportVertexColorOptions* Options, UTexture2D* Texture);

	struct FPaintRay
	{
		FVector CameraLocation;
		FVector RayStart;
		FVector RayDirection;
		UViewportInteractor* ViewportInteractor;
	};


	static bool RetrieveViewportPaintRays(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI, TArray<FPaintRay>& OutPaintRays);

	/** Imports vertex colors from a Texture to the specified Static Mesh according to user-set options */
	static void ImportVertexColorsToStaticMesh(UStaticMesh* StaticMesh, const UImportVertexColorOptions* Options, UTexture2D* Texture);

	/** Imports vertex colors from a Texture to the specified Static Mesh Component according to user-set options */
	static void ImportVertexColorsToStaticMeshComponent(UStaticMeshComponent* StaticMeshComponent, const UImportVertexColorOptions* Options, UTexture2D* Texture);

	static void PropagateVertexColors(const TArray<UStaticMeshComponent *> StaticMeshComponents);
	static bool CanPropagateVertexColors(TArray<UStaticMeshComponent*>& StaticMeshComponents, TArray<UStaticMesh*>& StaticMeshes, int32 NumInstanceVertexColorBytes);
	static void CopyVertexColors(const TArray<UStaticMeshComponent*> StaticMeshComponents, TArray<FPerComponentVertexColorData>& CopiedVertexColors);
	static bool CanCopyInstanceVertexColors(const TArray<UStaticMeshComponent*>& StaticMeshComponents, int32 PaintingMeshLODIndex);
	static void PasteVertexColors(const TArray<UStaticMeshComponent*>& StaticMeshComponents, TArray<FPerComponentVertexColorData>& CopiedColorsByComponent);
	static bool CanPasteInstanceVertexColors(const TArray<UStaticMeshComponent*>& StaticMeshComponents, const TArray<FPerComponentVertexColorData>& CopiedColorsByComponent);
	static void RemovePerLODColors(const TArray<UMeshComponent*>& PaintableComponents);

	static void SwapVertexColors();
	static 	void SaveModifiedTextures();
	static bool CanSaveModifiedTextures();
};