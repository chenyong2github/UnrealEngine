// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "MeshPaintingToolsetTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "IMeshPaintComponentAdapter.h"
#include "InteractiveToolManager.h"
#include "InteractiveToolsContext.h"
#include "Engine/StaticMesh.h"
#include "MeshPaintHelpers.generated.h"

class FMeshPaintParameters;
class UImportVertexColorOptions;
class UTexture2D;
class UStaticMeshComponent;
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
class UMeshVertexPaintingToolProperties;

enum class EMeshPaintDataColorViewMode : uint8;

/** struct used to store the color data copied from mesh instance to mesh instance */
struct FPerLODVertexColorData
{
	TArray< FColor > ColorsByIndex;
	TMap<FVector, FColor> ColorsByPosition;
};

/** struct used to store the color data copied from mesh component to mesh component */
struct FPerComponentVertexColorData
{
	FPerComponentVertexColorData(const UStaticMesh* InStaticMesh, int32 InComponentIndex)
		: OriginalMesh(InStaticMesh)
		, ComponentIndex(InComponentIndex)
	{
	}

	/** We match up components by the mesh they use */
	TWeakObjectPtr<const UStaticMesh> OriginalMesh;

	/** We also match by component index */
	int32 ComponentIndex;

	/** Vertex colors by LOD */
	TArray<FPerLODVertexColorData> PerLODVertexColorData;
};

/** Struct to hold MeshPaint settings on a per mesh basis */
struct FInstanceTexturePaintSettings
{
	UTexture2D* SelectedTexture;
	int32 SelectedUVChannel;

	FInstanceTexturePaintSettings()
		: SelectedTexture(nullptr)
		, SelectedUVChannel(0)
	{}
	FInstanceTexturePaintSettings(UTexture2D* InSelectedTexture, int32 InSelectedUVSet)
		: SelectedTexture(InSelectedTexture)
		, SelectedUVChannel(InSelectedUVSet)
	{}

	void operator=(const FInstanceTexturePaintSettings& SrcSettings)
	{
		SelectedTexture = SrcSettings.SelectedTexture;
		SelectedUVChannel = SrcSettings.SelectedUVChannel;
	}
};


UENUM()
enum class ETexturePaintWeightTypes : uint8
{
	/** Lerp Between Two Textures using Alpha Value */
	AlphaLerp = 2 UMETA(DisplayName = "Alpha (Two Textures)"),

	/** Weighting Three Textures according to Channels*/
	RGB = 3 UMETA(DisplayName = "RGB (Three Textures)"),

	/**  Weighting Four Textures according to Channels*/
	ARGB = 4 UMETA(DisplayName = "ARGB (Four Textures)"),

	/**  Weighting Five Textures according to Channels */
	OneMinusARGB = 5 UMETA(DisplayName = "ARGB - 1 (Five Textures)")
};

UENUM()
enum class ETexturePaintWeightIndex : uint8
{
	TextureOne = 0,
	TextureTwo,
	TextureThree,
	TextureFour,
	TextureFive
};

/** Parameters for paint actions, stored together for convenience */
struct FPerVertexPaintActionArgs
{
	IMeshPaintComponentAdapter* Adapter;
	UMeshVertexPaintingToolProperties* BrushProperties;
	FVector CameraPosition;
	FHitResult HitResult;
	EMeshPaintModeAction Action;
};

/** Delegates used to call per-vertex/triangle actions */
DECLARE_DELEGATE_TwoParams(FPerVertexPaintAction, FPerVertexPaintActionArgs& /*Args*/, int32 /*VertexIndex*/);
DECLARE_DELEGATE_ThreeParams(FPerTrianglePaintAction, IMeshPaintComponentAdapter* /*Adapter*/, int32 /*TriangleIndex*/, const int32[3] /*Vertex Indices*/);

class MESHPAINTINGTOOLSET_API UMeshPaintingToolset : public UBlueprintFunctionLibrary
{
public:
	/** Removes vertex colors associated with the object */
	static void RemoveInstanceVertexColors(UObject* Obj);

	/** Removes vertex colors associated with the static mesh component */
	static void RemoveComponentInstanceVertexColors(UStaticMeshComponent* StaticMeshComponent);
	
	/** Propagates per-instance vertex colors to the underlying Static Mesh for the given LOD Index */
	static bool PropagateColorsToRawMesh(UStaticMesh* StaticMesh, int32 LODIndex, FStaticMeshComponentLODInfo& ComponentLODInfo);	

	/** Retrieves the Vertex Color buffer size for the given LOD level in the Static Mesh */
	static uint32 GetVertexColorBufferSize(UMeshComponent* MeshComponent, int32 LODIndex, bool bInstance);

	/** Retrieves the vertex positions from the given LOD level in the Static Mesh */
	static TArray<FVector> GetVerticesForLOD(const UStaticMesh* StaticMesh, int32 LODIndex);

	/** Retrieves the vertex colors from the given LOD level in the Static Mesh */
	static TArray<FColor> GetColorDataForLOD(const UStaticMesh* StaticMesh, int32 LODIndex);

	/** Retrieves the per-instance vertex colors from the given LOD level in the StaticMeshComponent */
	static TArray<FColor> GetInstanceColorDataForLOD(const UStaticMeshComponent* MeshComponent, int32 LODIndex);

	/** Sets the specific (LOD Index) per-instance vertex colors for the given StaticMeshComponent to the supplied Color array */
	static void SetInstanceColorDataForLOD(UStaticMeshComponent* MeshComponent, int32 LODIndex, const TArray<FColor>& Colors);	
	
	/** Sets the specific (LOD Index) per-instance vertex colors for the given StaticMeshComponent to a single Color value */
	static void SetInstanceColorDataForLOD(UStaticMeshComponent* MeshComponent, int32 LODIndex, const FColor FillColor, const FColor MaskColor);
	
	/** Fills all vertex colors for all LODs found in the given mesh component with Fill Color */
	static void FillStaticMeshVertexColors(UStaticMeshComponent* MeshComponent, int32 LODIndex, const FColor FillColor, const FColor MaskColor);
	static void FillSkeletalMeshVertexColors(USkeletalMeshComponent* MeshComponent, int32 LODIndex, const FColor FillColor, const FColor MaskColor);
	
	/** Sets all vertex colors for a specific LOD level in the SkeletalMesh to FillColor */
	static void SetColorDataForLOD(USkeletalMesh* SkeletalMesh, int32 LODIndex, const FColor FillColor, const FColor MaskColor);

	static void ApplyFillWithMask(FColor& InOutColor, const FColor& MaskColor, const FColor& FillColor);

		
	/** Checks whether or not the given Viewport Client is a VR editor viewport client */
	static bool IsInVRMode(const FEditorViewportClient* ViewportClient);

	/** Forces the component to render LOD level at LODIndex instead of the view-based LOD level ( X = 0 means do not force the LOD, X > 0 means force the lod to X - 1 ) */
	static void ForceRenderMeshLOD(UMeshComponent* Component, int32 LODIndex);

	/** Clears all texture overrides for this component. */
	static void ClearMeshTextureOverrides(const IMeshPaintComponentAdapter& GeometryInfo, UMeshComponent* InMeshComponent);

	/** Applies vertex color painting found on LOD 0 to all lower LODs. */
	static void ApplyVertexColorsToAllLODs(IMeshPaintComponentAdapter& GeometryInfo, UMeshComponent* InMeshComponent);

	/** Applies the vertex colors found in LOD level 0 to all contained LOD levels in the StaticMeshComponent */
	static void ApplyVertexColorsToAllLODs(IMeshPaintComponentAdapter& GeometryInfo, UStaticMeshComponent* StaticMeshComponent);

	/** Applies the vertex colors found in LOD level 0 to all contained LOD levels in the SkeletalMeshComponent */
	static void ApplyVertexColorsToAllLODs(IMeshPaintComponentAdapter& GeometryInfo, USkeletalMeshComponent* SkeletalMeshComponent);

	/** Returns the number of Mesh LODs for the given MeshComponent */
	static int32 GetNumberOfLODs(const UMeshComponent* MeshComponent);

	/** OutNumLODs is set to number of Mesh LODs for the given MeshComponent and returns true, or returns false of given mesh component has no valid LODs */
	static bool TryGetNumberOfLODs(const UMeshComponent* MeshComponent, int32& OutNumLODs);
	
	/** Returns the number of Texture Coordinates for the given MeshComponent */
	static int32 GetNumberOfUVs(const UMeshComponent* MeshComponent, int32 LODIndex);

	/** Checks whether or not the mesh components contains per lod colors (for all LODs)*/
	static bool DoesMeshComponentContainPerLODColors(const UMeshComponent* MeshComponent);

	/** Retrieves the number of bytes used to store the per-instance LOD vertex color data from the static mesh component */
	static void GetInstanceColorDataInfo(const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex, int32& OutTotalInstanceVertexColorBytes);

	/** Given arguments for an action, and an action - retrieves influences vertices and applies Action to them */
	static bool ApplyPerVertexPaintAction(FPerVertexPaintActionArgs& InArgs, FPerVertexPaintAction Action);

	static bool GetPerVertexPaintInfluencedVertices(FPerVertexPaintActionArgs& InArgs, TSet<int32>& InfluencedVertices);

	/** Given the adapter, settings and view-information retrieves influences triangles and applies Action to them */
	static bool ApplyPerTrianglePaintAction(IMeshPaintComponentAdapter* Adapter, const FVector& CameraPosition, const FVector& HitPosition, const UMeshVertexPaintingToolProperties* Settings, FPerTrianglePaintAction Action);

	/** Applies vertex painting to InOutvertexColor according to the given parameters  */
	static bool PaintVertex(const FVector& InVertexPosition, const FMeshPaintParameters& InParams, FColor& InOutVertexColor);

	/** Applies Vertex Color Painting according to the given parameters */
	static void ApplyVertexColorPaint(const FMeshPaintParameters &InParams, const FLinearColor &OldColor, FLinearColor &NewColor, const float PaintAmount);

	/** Applies Vertex Blend Weight Painting according to the given parameters */
	static void ApplyVertexWeightPaint(const FMeshPaintParameters &InParams, const FLinearColor &OldColor, FLinearColor &NewColor, const float PaintAmount);

	/** Generate texture weight color for given number of weights and the to-paint index */
	static FLinearColor GenerateColorForTextureWeight(const int32 NumWeights, const int32 WeightIndex);

	/** Computes the Paint power multiplier value */
	static float ComputePaintMultiplier(float SquaredDistanceToVertex2D, float BrushStrength, float BrushInnerRadius, float BrushRadialFalloff, float BrushInnerDepth, float BrushDepthFallof, float VertexDepthToBrush);

	/** Checks whether or not a point is influenced by the painting brush according to the given parameters*/
	static bool IsPointInfluencedByBrush(const FVector& InPosition, const FMeshPaintParameters& InParams, float& OutSquaredDistanceToVertex2D, float& OutVertexDepthToBrush);

	static bool IsPointInfluencedByBrush(const FVector2D& BrushSpacePosition, const float BrushRadiusSquared, float& OutInRangeValue);

	template<typename T>
	static void ApplyBrushToVertex(const FVector& VertexPosition, const FMatrix& InverseBrushMatrix, const float BrushRadius, const float BrushFalloffAmount, const float BrushStrength, const T& PaintValue, T& InOutValue);

public:



	/** Helper function to retrieve vertex color from a UTexture given a UVCoordinate */
	static FColor PickVertexColorFromTextureData(const uint8* MipData, const FVector2D& UVCoordinate, const UTexture2D* Texture, const FColor ColorMask);	
};

template<typename T>
void UMeshPaintingToolset::ApplyBrushToVertex(const FVector& VertexPosition, const FMatrix& InverseBrushMatrix, const float BrushRadius, const float BrushFalloffAmount, const float BrushStrength, const T& PaintValue, T& InOutValue)
{
	const FVector BrushSpacePosition = InverseBrushMatrix.TransformPosition(VertexPosition);
	const FVector2D BrushSpacePosition2D(BrushSpacePosition.X, BrushSpacePosition.Y);
		
	float InfluencedValue = 0.0f;
	if (IsPointInfluencedByBrush(BrushSpacePosition2D, BrushRadius * BrushRadius, InfluencedValue))
	{
		float InnerBrushRadius = BrushFalloffAmount * BrushRadius;
		float PaintStrength = UMeshPaintingToolset::ComputePaintMultiplier(BrushSpacePosition2D.SizeSquared(), BrushStrength, InnerBrushRadius, BrushRadius - InnerBrushRadius, 1.0f, 1.0f, 1.0f);

		const T OldValue = InOutValue;
		InOutValue = FMath::LerpStable(OldValue, PaintValue, PaintStrength);
	}	
}

UCLASS()
class MESHPAINTINGTOOLSET_API UMeshToolsContext : public UInteractiveToolsContext
{
	GENERATED_BODY()

public:
	UMeshToolsContext();
};

UCLASS()
class MESHPAINTINGTOOLSET_API UMeshToolManager : public UInteractiveToolManager
{
	GENERATED_BODY()

public:
	UMeshToolManager();
	virtual void Shutdown() override;

	/** Map of geometry adapters for each selected mesh component */
	TMap<UMeshComponent*, TSharedPtr<IMeshPaintComponentAdapter>> GetComponentToAdapterMap() const;
	TSharedPtr<IMeshPaintComponentAdapter> GetAdapterForComponent(UMeshComponent* InComponent);
	void AddToComponentToAdapterMap(UMeshComponent* InComponent, TSharedPtr<IMeshPaintComponentAdapter> InAdapter);

	TArray<UMeshComponent*> GetSelectedMeshComponents() const;
	void AddSelectedMeshComponents(const TArray<UMeshComponent*>& InComponents);
	bool FindHitResult(const FRay Ray, FHitResult& BestTraceResult);
	void ClearSelectedMeshComponents();
	TArray<UMeshComponent*> GetPaintableMeshComponents() const;
	void AddPaintableMeshComponent(UMeshComponent* InComponent);
	void ClearPaintableMeshComponents();
	bool SelectionContainsValidAdapters() const;
	TArray<FPerComponentVertexColorData> GetCopiedColorsByComponent() const;
	void SetCopiedColorsByComponent(TArray<FPerComponentVertexColorData>& InCopiedColors);
	void CacheSelectionData(const int32 PaintLODIndex, const int32 UVChannel);
	void ResetState();
	void Refresh();
	bool SelectionContainsPerLODColors() const { return bSelectionContainsPerLODColors; }
	void ClearSelectionLODColors() { bSelectionContainsPerLODColors = false; }

public:
	bool bNeedsRecache;


private:
	void CleanUp();

private:
	/** Map of geometry adapters for each selected mesh component */
	TMap<UMeshComponent*, TSharedPtr<IMeshPaintComponentAdapter>> ComponentToAdapterMap;
	TArray<UMeshComponent*> SelectedMeshComponents;
	/** Mesh components within the current selection which are eligible for painting */
	TArray<UMeshComponent*> PaintableComponents;
	/** Contains copied vertex color data */
	TArray<FPerComponentVertexColorData> CopiedColorsByComponent;
	bool bSelectionContainsPerLODColors;
};