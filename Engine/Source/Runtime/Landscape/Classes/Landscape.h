// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "LandscapeProxy.h"
#include "LandscapeBPCustomBrush.h"

#include "Landscape.generated.h"

class ULandscapeComponent;

UENUM()
enum ELandscapeSetupErrors
{
	LSE_None,
	/** No Landscape Info available. */
	LSE_NoLandscapeInfo,
	/** There was already component with same X,Y. */
	LSE_CollsionXY,
	/** No Layer Info, need to add proper layers. */
	LSE_NoLayerInfo,
	LSE_MAX,
};


enum class ERTDrawingType : uint8
{
	RTAtlas,
	RTAtlasToNonAtlas,
	RTNonAtlasToAtlas,
	RTNonAtlas,
	RTMips
};

enum EHeightmapRTType : uint8
{
	HeightmapRT_CombinedAtlas,
	HeightmapRT_CombinedNonAtlas,
	HeightmapRT_Scratch1,
	HeightmapRT_Scratch2,
	HeightmapRT_Scratch3,
	// Mips RT
	HeightmapRT_Mip1,
	HeightmapRT_Mip2,
	HeightmapRT_Mip3,
	HeightmapRT_Mip4,
	HeightmapRT_Mip5,
	HeightmapRT_Mip6,
	HeightmapRT_Mip7,
	HeightmapRT_Count
};

enum EWeightmapRTType : uint8
{
	WeightmapRT_Scratch_RGBA,
	WeightmapRT_Scratch1,
	WeightmapRT_Scratch2,
	WeightmapRT_Scratch3,

	// Mips RT
	WeightmapRT_Mip0,
	WeightmapRT_Mip1,
	WeightmapRT_Mip2,
	WeightmapRT_Mip3,
	WeightmapRT_Mip4,
	WeightmapRT_Mip5,
	WeightmapRT_Mip6,
	WeightmapRT_Mip7,
	
	WeightmapRT_Count
};

enum EProceduralContentUpdateFlag : uint32
{
	Heightmap_Setup					= 0x00000001u,
	Heightmap_Render				= 0x00000002u,
	Heightmap_BoundsAndCollision	= 0x00000004u,
	Heightmap_ResolveToTexture		= 0x00000008u,

	Weightmap_Setup					= 0x00000100u,
	Weightmap_Render				= 0x00000200u,
	Weightmap_Collision				= 0x00000400u,
	Weightmap_ResolveToTexture		= 0x00000800u,

	// Combinations
	Heightmap_All = Heightmap_Render | Heightmap_BoundsAndCollision | Heightmap_ResolveToTexture,
	Weightmap_All = Weightmap_Render | Weightmap_Collision | Weightmap_ResolveToTexture,

	All = Heightmap_All | Weightmap_All,
	All_Setup = Heightmap_Setup | Weightmap_Setup,
	All_Render = Heightmap_Render | Weightmap_Render,
};

USTRUCT()
struct FLandscapeProceduralLayerBrush
{
	GENERATED_USTRUCT_BODY()

	FLandscapeProceduralLayerBrush()
		: BPCustomBrush(nullptr)
	{}

	FLandscapeProceduralLayerBrush(ALandscapeBlueprintCustomBrush* InBrush)
		: BPCustomBrush(InBrush)
	{}

#if WITH_EDITOR
	UTextureRenderTarget2D* Render(bool InIsHeightmap, UTextureRenderTarget2D* InCombinedResult)
	{
		if (BPCustomBrush != nullptr)
		{
			TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
			return BPCustomBrush->Render(InIsHeightmap, InCombinedResult);
		}

		return nullptr;
	}

	bool IsInitialized() const 
	{
		return BPCustomBrush != nullptr ? BPCustomBrush->IsInitialized() : false;
	}

	void Initialize(const FIntRect& InBoundRect, const FIntPoint& InLandscapeRenderTargetSize)
	{
		if (BPCustomBrush != nullptr)
		{
			TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
			FIntPoint LandscapeSize = InBoundRect.Max - InBoundRect.Min;
			BPCustomBrush->Initialize(LandscapeSize, InLandscapeRenderTargetSize);
			BPCustomBrush->SetIsInitialized(true);
		}
	}
#endif

	UPROPERTY()
	ALandscapeBlueprintCustomBrush* BPCustomBrush;
};

USTRUCT()
struct FProceduralLayer
{
	GENERATED_USTRUCT_BODY()

	FProceduralLayer()
		: Guid(FGuid::NewGuid())
		, Name(NAME_None)
		, bVisible(true)
		, bLocked(false)
		, HeightmapAlpha(1.0f)
		, WeightmapAlpha(1.0f)
	{}

	UPROPERTY(meta = (IgnoreForMemberInitializationTest))
	FGuid Guid;

	UPROPERTY()
	FName Name;

	UPROPERTY(Transient)
	bool bVisible;

	UPROPERTY()
	bool bLocked;

	UPROPERTY()
	float HeightmapAlpha;

	UPROPERTY()
	float WeightmapAlpha;

	UPROPERTY()
	TArray<FLandscapeProceduralLayerBrush> Brushes;

	UPROPERTY()
	TArray<int8> HeightmapBrushOrderIndices;

	UPROPERTY()
	TArray<int8> WeightmapBrushOrderIndices;

	UPROPERTY()
	TMap<ULandscapeLayerInfoObject*, bool> WeightmapLayerAllocationBlend; // True -> Substractive, False -> Additive
};

UCLASS(MinimalAPI, showcategories=(Display, Movement, Collision, Lighting, LOD, Input), hidecategories=(Mobility))
class ALandscape : public ALandscapeProxy
{
	GENERATED_BODY()

public:
	ALandscape(const FObjectInitializer& ObjectInitializer);

	virtual void TickActor(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;

	//~ Begin ALandscapeProxy Interface
	LANDSCAPE_API virtual ALandscape* GetLandscapeActor() override;
#if WITH_EDITOR
	//~ End ALandscapeProxy Interface

	LANDSCAPE_API bool HasAllComponent(); // determine all component is in this actor
	
	// Include Components with overlapped vertices
	// X2/Y2 Coordinates are "inclusive" max values
	LANDSCAPE_API static void CalcComponentIndicesOverlap(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, const int32 ComponentSizeQuads, 
		int32& ComponentIndexX1, int32& ComponentIndexY1, int32& ComponentIndexX2, int32& ComponentIndexY2);

	// Exclude Components with overlapped vertices
	// X2/Y2 Coordinates are "inclusive" max values
	LANDSCAPE_API static void CalcComponentIndicesNoOverlap(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, const int32 ComponentSizeQuads,
		int32& ComponentIndexX1, int32& ComponentIndexY1, int32& ComponentIndexX2, int32& ComponentIndexY2);

	static void SplitHeightmap(ULandscapeComponent* Comp, bool bMoveToCurrentLevel = false, class FMaterialUpdateContext* InOutUpdateContext = nullptr, TArray<class FComponentRecreateRenderStateContext>* InOutRecreateRenderStateContext = nullptr, bool InReregisterComponent = true);
	
	//~ Begin UObject Interface.
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
	virtual bool ShouldImport(FString* ActorPropString, bool IsMovingLevel) override;
	virtual void PostEditImport() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#endif
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual void FinishDestroy() override;
	//~ End UObject Interface


	// Procedural stuff
#if WITH_EDITOR
	LANDSCAPE_API void RequestProceduralContentUpdate(uint32 InDataFlags, bool InUpdateAllMaterials = false);
	LANDSCAPE_API void CreateProceduralLayer(FName InName = NAME_None, bool bInUpdateProceduralContent = true);
	LANDSCAPE_API bool IsProceduralLayerNameUnique(const FName& InName) const;
	LANDSCAPE_API void SetProceduralLayerName(int32 InLayerIndex, const FName& InName);
	LANDSCAPE_API void SetProceduralLayerAlpha(int32 InLayerIndex, const float InAlpha, bool bInHeightmap);
	LANDSCAPE_API void SetProceduralLayerVisibility(int32 InLayerIndex, bool bInVisible);
	LANDSCAPE_API struct FProceduralLayer* GetProceduralLayer(int32 InLayerIndex);
	LANDSCAPE_API const struct FProceduralLayer* GetProceduralLayer(int32 InLayerIndex) const;
	LANDSCAPE_API void ClearProceduralLayer(int32 InLayerIndex);
	LANDSCAPE_API void ClearProceduralLayer(const FGuid& InLayerGuid);
	LANDSCAPE_API void DeleteProceduralLayer(int32 InLayerIndex);
	LANDSCAPE_API void SetCurrentEditingProceduralLayer(FGuid InLayerGuid = FGuid());
	LANDSCAPE_API void ShowOnlySelectedProceduralLayer(int32 InLayerIndex);
	LANDSCAPE_API void ShowAllProceduralLayers();

private:
	void TickProcedural(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction);
	void RegenerateProceduralContent();
	void RegenerateProceduralHeightmaps();
	void ResolveProceduralHeightmapTexture(const TArray<ALandscapeProxy*>& InAllLandscapes);
	void ResolveProceduralWeightmapTexture(const TArray<ALandscapeProxy*>& InAllLandscapes);
	void ResolveProceduralTexture(FLandscapeProceduralTexture2DCPUReadBackResource* InCPUReadBackTexture, UTexture2D* InOriginalTexture);
	void RegenerateProceduralWeightmaps();

	bool AreHeightmapTextureResourcesReady(const TArray<ALandscapeProxy*>& InAllLandscapes) const;
	bool AreWeightmapTextureResourcesReady(const TArray<ALandscapeProxy*>& InAllLandscapes) const;

	void UpdateProceduralMaterialInstances(const TArray<ULandscapeComponent*>& InComponentsToUpdate, const TMap<ULandscapeComponent*, TArray<ULandscapeLayerInfoObject*>>& InZeroAllocationsPerComponents);

	void PrepareProceduralComponentDataForExtractLayersCS(const FProceduralLayer& InProceduralLayer, int32 InCurrentWeightmapToProcessIndex, bool InOutputDebugName, const TArray<ALandscapeProxy*>& InAllLandscape, class FLandscapeTexture2DResource* InOutTextureData,
														  TArray<struct FLandscapeProceduralWeightmapExtractLayersComponentData>& OutComponentData, TMap<ULandscapeLayerInfoObject*, int32>& OutLayerInfoObjects);
	void PrepareProceduralComponentDataForPackLayersCS(int32 InCurrentWeightmapToProcessIndex, bool InOutputDebugName, const TArray<ULandscapeComponent*>& InAllLandscapeComponents, 
													   TArray<UTexture2D*>& InOutProcessedWeightmaps, TArray<class FLandscapeProceduralTexture2DCPUReadBackResource*>& InOutProcessedWeightmapCPUCopy, TArray<struct FLandscapeProceduralWeightmapPackLayersComponentData>& OutComponentData);
	void ReallocateProceduralWeightmaps(const TArray<ALandscapeProxy*>& InAllLandscape, const TArray<ULandscapeLayerInfoObject*>& InBrushRequiredAllocations, TArray<ULandscapeComponent*>& OutComponentThatNeedMaterialRebuild);
	void InitProceduralWeightmapResources(uint8 InLayerCount);
	bool GenerateZeroAllocationPerComponents(const TArray<ALandscapeProxy*>& InAllLandscape, const TMap<ULandscapeLayerInfoObject*, bool>& InWeightmapLayersBlendSubstractive, TMap<ULandscapeComponent*, TArray<ULandscapeLayerInfoObject*>>& OutZeroAllocationsPerComponents);

	void GenerateProceduralRenderQuad(const FIntPoint& InVertexPosition, float InVertexSize, const FVector2D& InUVStart, const FVector2D& InUVSize, TArray<struct FLandscapeProceduralTriangle>& OutTriangles) const;
	void GenerateProceduralRenderQuadsAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeProceduralTriangle>& OutTriangles) const;
	void GenerateProceduralRenderQuadsAtlasToNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeProceduralTriangle>& OutTriangles) const;
	void GenerateProceduralRenderQuadsNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeProceduralTriangle>& OutTriangles) const;
	void GenerateProceduralRenderQuadsNonAtlasToAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeProceduralTriangle>& OutTriangles) const;
	void GenerateProceduralRenderQuadsMip(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, uint8 InCurrentMip, TArray<FLandscapeProceduralTriangle>& OutTriangles) const;

	void ClearWeightmapTextureResource(const FString& InDebugName, FTextureRenderTargetResource* InTextureResourceToClear);
	void DrawHeightmapComponentsToRenderTarget(const FString& InDebugName, const TArray<ULandscapeComponent*>& InComponentsToDraw, UTexture* InHeightmapRTRead, UTextureRenderTarget2D* InOptionalHeightmapRTRead2, UTextureRenderTarget2D* InHeightmapRTWrite, ERTDrawingType InDrawType,
											   bool InClearRTWrite, struct FLandscapeHeightmapProceduralShaderParameters& InShaderParams, uint8 InMipRender = 0) const;

	void DrawWeightmapComponentsToRenderTarget(const FString& InDebugName, const TArray<ULandscapeComponent*>& InComponentsToDraw, UTexture* InWeightmapRTRead, UTextureRenderTarget2D* InOptionalWeightmapRTRead2, UTextureRenderTarget2D* InWeightmapRTWrite,
											   bool InClearRTWrite, struct FLandscapeWeightmapProceduralShaderParameters& InShaderParams, uint8 InMipRender) const;

	void DrawWeightmapComponentsToRenderTarget(const FString& InDebugName, const FIntPoint& InSectionBase, const FVector2D& InScaleBias, UTexture* InWeightmapRTRead, UTextureRenderTarget2D* InOptionalWeightmapRTRead2, UTextureRenderTarget2D* InWeightmapRTWrite,
									 		   bool InClearRTWrite, struct FLandscapeWeightmapProceduralShaderParameters& InShaderParams, uint8 InMipRender) const;

	void DrawHeightmapComponentsToRenderTargetMips(TArray<ULandscapeComponent*>& InComponentsToDraw, UTexture* InReadHeightmap, bool InClearRTWrite, struct FLandscapeHeightmapProceduralShaderParameters& InShaderParams) const;
	void DrawWeightmapComponentToRenderTargetMips(const FIntPoint& TopLeftTexturePosition, UTexture* InReadWeightmap, bool InClearRTWrite, struct FLandscapeWeightmapProceduralShaderParameters& InShaderParams) const;

	void CopyProceduralTexture(UTexture* InSourceTexture, UTexture* InDestTexture, FTextureResource* InDestCPUResource = nullptr, const FIntPoint& InFirstComponentSectionBase = FIntPoint(0, 0), uint8 InSourceCurrentMip = 0, uint8 InDestCurrentMip = 0,
							   uint32 InSourceArrayIndex = 0, uint32 InDestArrayIndex = 0) const;
	void CopyProceduralTexture(const FString& InSourceDebugName, FTextureResource* InSourceResource, const FString& InDestDebugName, FTextureResource* InDestResource, FTextureResource* InDestCPUResource = nullptr, const FIntPoint& InFirstComponentSectionBase = FIntPoint(0, 0),
							   uint8 InSourceCurrentMip = 0, uint8 InDestCurrentMip = 0, uint32 InSourceArrayIndex = 0, uint32 uInDestArrayIndex = 0) const;

	void PrintProceduralDebugRT(const FString& InContext, UTextureRenderTarget2D* InDebugRT, uint8 InMipRender = 0, bool InOutputHeight = true, bool InOutputNormals = false) const;
	void PrintProceduralDebugTextureResource(const FString& InContext, FTextureResource* InTextureResource, uint8 InMipRender = 0, bool InOutputHeight = true, bool InOutputNormals = false) const;
	void PrintProceduralDebugHeightData(const FString& InContext, const TArray<FColor>& InHeightmapData, const FIntPoint& InDataSize, uint8 InMipRender, bool InOutputNormals = false) const;
	void PrintProceduralDebugWeightData(const FString& InContext, const TArray<FColor>& InWeightmapData, const FIntPoint& InDataSize, uint8 InMipRender) const;
#endif

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY(TextExportTransient)
	TArray<FProceduralLayer> ProceduralLayers;

	UPROPERTY(Transient)
	TArray<UTextureRenderTarget2D*> HeightmapRTList;

	UPROPERTY(Transient)
	TArray<UTextureRenderTarget2D*> WeightmapRTList;

	UPROPERTY(Transient)
	bool PreviousExperimentalLandscapeProcedural;

private:

	UPROPERTY(Transient)
	bool WasCompilingShaders;

	UPROPERTY(Transient)
	uint32 ProceduralContentUpdateFlags;

	UPROPERTY(Transient)
	bool ProceduralUpdateAllMaterials;

	// Represent all the resolved paint layer, from all procedural layer blended together (size of the landscape x paint layer count)
	class FLandscapeTexture2DArrayResource* CombinedProcLayerWeightmapAllLayersResource;
	
	// Represent all the resolved paint layer, from the current procedual layer only (size of the landscape x paint layer count)
	class FLandscapeTexture2DArrayResource* CurrentProcLayerWeightmapAllLayersResource;	
	
	// Used in extracting the paint layers data from procedural layer weightmaps (size of the landscape)
	class FLandscapeTexture2DResource* WeightmapScratchExtractLayerTextureResource;	
	
	// Used in packing the paint layers data contained into CombinedProcLayerWeightmapAllLayersResource to be set again for each component weightmap (size of the landscape)
	class FLandscapeTexture2DResource* WeightmapScratchPackLayerTextureResource;	
#endif

protected:
#if WITH_EDITOR
	FName GenerateUniqueProceduralLayerName(FName InName = NAME_None) const;
#endif
};

#if WITH_EDITOR
class LANDSCAPE_API FScopedSetLandscapeCurrentEditingProceduralLayer
{
public:
	FScopedSetLandscapeCurrentEditingProceduralLayer(ALandscape* InLandscape, const FGuid& InProceduralLayer, TFunction<void()> InCompletionCallback = TFunction<void()>());
	~FScopedSetLandscapeCurrentEditingProceduralLayer();

private:
	TWeakObjectPtr<ALandscape> Landscape;
	const FGuid& ProceduralLayer;
	TFunction<void()> CompletionCallback;
};
#endif
