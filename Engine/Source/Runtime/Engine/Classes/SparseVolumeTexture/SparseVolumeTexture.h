// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/VolumeTexture.h"

#include "Serialization/EditorBulkData.h"
#include "Containers/Array.h"
#include "UnrealClient.h"
#include "UObject/ObjectSaveContext.h"

#include "SparseVolumeTexture.generated.h"

namespace UE { namespace Shader	{ enum class EValueType : uint8; } }
namespace UE { namespace DerivedData { class FRequestOwner; } }

#define SPARSE_VOLUME_TILE_RES	16

uint32 SparseVolumeTexturePackPageTableEntry(const FIntVector3& Coord);
FIntVector3 SparseVolumeTextureUnpackPageTableEntry(uint32 Packed);

struct ENGINE_API FSparseVolumeAssetHeader
{
	FIntVector3 VirtualVolumeResolution = FIntVector3(0, 0, 0);
	FIntVector3 VirtualVolumeAABBMin = FIntVector3(INT32_MAX, INT32_MAX, INT32_MAX);
	FIntVector3 VirtualVolumeAABBMax = FIntVector3(INT32_MIN, INT32_MIN, INT32_MIN);
	FIntVector3 PageTableVolumeResolution = FIntVector3(0, 0, 0);
	FIntVector3 PageTableVolumeAABBMin = FIntVector3(INT32_MAX, INT32_MAX, INT32_MAX);
	FIntVector3 PageTableVolumeAABBMax = FIntVector3(INT32_MIN, INT32_MIN, INT32_MIN);
	FIntVector3 TileDataVolumeResolution = FIntVector3(0, 0, 0);
	TStaticArray<EPixelFormat, 2> AttributesFormats = TStaticArray<EPixelFormat, 2>(InPlace, PF_Unknown);
	int32 MipLevel = 0;
	bool bHasNullTile = false;

	// The current data format version for the header.
	static const uint32 kVersion = 0;

	// This version can be used to convert existing header to new version later.
	uint32 Version = kVersion;

	void Serialize(FArchive& Ar);
};

class ISparseVolumeRawSourceConstructionAdapter
{
public:
	struct FAttributesInfo
	{
		EPixelFormat Format;
		FVector4f FallbackValue;
		FVector4f NormalizeScale;
		FVector4f NormalizeBias;
		bool bNormalized;
	};

	virtual void GetAttributesInfo(FAttributesInfo& OutInfoA, FAttributesInfo& OutInfoB) const = 0;
	virtual FIntVector3 GetAABBMin() const = 0;
	virtual FIntVector3 GetAABBMax() const = 0;
	virtual FIntVector3 GetResolution() const = 0;
	virtual void IteratePhysicalSource(TFunctionRef<void(const FIntVector3& Coord, int32 AttributesIdx, int32 ComponentIdx, float VoxelValue)> OnVisit) const = 0;
	virtual ~ISparseVolumeRawSourceConstructionAdapter() = default;
};

// The structure represent the source asset in high quality. It is used to cook the runtime data
struct ENGINE_API FSparseVolumeRawSource
{
	FSparseVolumeAssetHeader Header;
	TArray<uint32> PageTable;
	TArray<uint8> PhysicalTileDataA;
	TArray<uint8> PhysicalTileDataB;

	// The current data format version for the raw source data.
	static const uint32 kVersion = 0;

	// This version can be used to convert existing source data to new version later.
	uint32 Version;

	void Serialize(FArchive& Ar);

	bool Construct(const ISparseVolumeRawSourceConstructionAdapter& Adapter);
	uint32 ReadPageTablePacked(const FIntVector3& PageTableCoord) const;
	FIntVector3 ReadPageTable(const FIntVector3& PageTableCoord) const;
	FVector4f ReadTileDataVoxel(const FIntVector3& TileDataCoord, int32 AttributesIdx) const;
	FVector4f Sample(const FIntVector3& VolumeCoord, int32 AttributesIdx) const;
	void Sample(const FIntVector3& VolumeCoord, FVector4f& OutAttributesA, FVector4f& OutAttributesB) const;
	void WriteTileDataVoxel(const FIntVector3& TileDataCoord, int32 AttributesIdx, const FVector4f& Value, int32 DstComponent = -1);
	void FillNullTile(const FVector4f& FallbackValueA, const FVector4f& FallbackValueB);
	FSparseVolumeRawSource GenerateMipMap() const;

	FSparseVolumeRawSource()
		: Version(kVersion)
	{
	}
};

// The structure represent the runtime data cooked runtime data.
struct ENGINE_API FSparseVolumeTextureRuntime
{
	FSparseVolumeAssetHeader	Header;
	TArray<uint32>				PageTable;
	TArray<uint8>				PhysicalTileDataA;
	TArray<uint8>				PhysicalTileDataB;

	void SetAsDefaultTexture();

	// The current data format version for the raw source data.
	static const uint32 kVersion = 0;

	// This version can be used to convert existing source data to new version later.
	uint32 Version;

	void Serialize(FArchive& Ar);

	FSparseVolumeTextureRuntime()
		: Header()
		, Version(kVersion)
	{
	}
};


class FSparseVolumeTextureSceneProxy : public FRenderResource
{
public:

	FSparseVolumeTextureSceneProxy();
	virtual ~FSparseVolumeTextureSceneProxy() override;

	FSparseVolumeTextureRuntime& GetRuntimeData()
	{
		return SparseVolumeTextureRuntime;
	}

	const FSparseVolumeTextureRuntime& GetRuntimeData() const
	{
		return SparseVolumeTextureRuntime;
	}

	const FSparseVolumeAssetHeader& GetHeader() const 
	{
		return SparseVolumeTextureRuntime.Header;
	}

	FTextureRHIRef GetPhysicalTileDataATextureRHI() const
	{
		return PhysicalTileDataATextureRHI;
	}
	FTextureRHIRef GetPhysicalTileDataBTextureRHI() const
	{
		return PhysicalTileDataBTextureRHI;
	}
	FTextureRHIRef GetPageTableTextureRHI() const
	{
		return PageTableTextureRHI;
	}

	void GetMemorySize(SIZE_T* SizeCPU, SIZE_T* SizeGPU) const;

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

private:

	FSparseVolumeTextureRuntime			SparseVolumeTextureRuntime;

	FTextureRHIRef						PageTableTextureRHI;
	FTextureRHIRef						PhysicalTileDataATextureRHI;
	FTextureRHIRef						PhysicalTileDataBTextureRHI;
};


struct ENGINE_API FSparseVolumeTextureFrame
{
	// The frame data that can be streamed in when in game.
	FByteBulkData						RuntimeStreamedInData;

	// The render side proxy for the sparse volume texture asset.
	FSparseVolumeTextureSceneProxy*		SparseVolumeTextureSceneProxy;

#if WITH_EDITORONLY_DATA
	/** The raw data that can be loaded when we want to update cook the data with different settings or updated code without re importing. */
	UE::Serialization::FEditorBulkData	RawData;
#endif

	FSparseVolumeTextureFrame();
	virtual ~FSparseVolumeTextureFrame();
	bool BuildRuntimeData(FSparseVolumeTextureRuntime* OutRuntimeData);
	void Serialize(FArchive& Ar, UStreamableSparseVolumeTexture* Owner, int32 FrameIndex);
};

using FSparseVolumeTextureFrameMips = TArray<FSparseVolumeTextureFrame, TInlineAllocator<1u>>;


enum ESparseVolumeTextureShaderUniform
{
	ESparseVolumeTexture_TileSize,
	ESparseVolumeTexture_PageTableSize,
	ESparseVolumeTexture_UVScale,
	ESparseVolumeTexture_UVBias,
	ESparseVolumeTexture_Count,
};

// SparseVolumeTexture base interface to communicate with material graph and shader bindings.
UCLASS(ClassGroup = Rendering, BlueprintType)
class ENGINE_API USparseVolumeTexture : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	USparseVolumeTexture();
	virtual ~USparseVolumeTexture() = default;

	UFUNCTION(BlueprintCallable, Category = "Texture")
	virtual int32 GetSizeX() const { return GetVolumeResolution().X; }

	UFUNCTION(BlueprintCallable, Category = "Texture")
	virtual int32 GetSizeY() const { return GetVolumeResolution().Y; }

	UFUNCTION(BlueprintCallable, Category = "Texture")
	virtual int32 GetSizeZ() const { return GetVolumeResolution().Z; }

	UFUNCTION(BlueprintCallable, Category = "Texture")
	virtual int32 GetNumFrames() const { return 0; }
	virtual int32 GetNumMipLevels() const { return 0; }
	virtual FIntVector GetVolumeResolution() const { return FIntVector(); }
	virtual const FSparseVolumeTextureSceneProxy* GetSparseVolumeTextureSceneProxy() const { return nullptr; }

	/** Getter for the shader uniform parameters with index as ESparseVolumeTextureShaderUniform. */
	FVector4 GetUniformParameter(int32 Index) const;

	void GetPackedUniforms(FUintVector4& OutPacked0, FUintVector4& OutPacked1) const;

	/** In order to keep the contents of an animated SVT sequence stable in world space, we need to account for the fact that
		different frames of the sequence have different AABBs. We solve this by scaling and biasing UVs that are relative to
		the volume bounds into the UV space represented by the AABB of each animation frame.*/
	void GetFrameUVScaleBias(FVector* OutScale, FVector* OutBias) const;

	/** Getter for the shader uniform parameter type with index as ESparseVolumeTextureShaderUniform. */
	static UE::Shader::EValueType GetUniformParameterType(int32 Index);

private:
};

UCLASS(ClassGroup = Rendering, BlueprintType)//, hidecategories = (Object))
class UStreamableSparseVolumeTexture : public USparseVolumeTexture
{
	GENERATED_UCLASS_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category = "Rendering")
	FIntVector VolumeResolution;

	UStreamableSparseVolumeTexture();
	virtual ~UStreamableSparseVolumeTexture() = default;

	//~ Begin UObject Interface.
	virtual void PostLoad() override;
	virtual void FinishDestroy() override;
	virtual void BeginDestroy() override;
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	//~ End UObject Interface.

	//~ Begin USparseVolumeTexture Interface.
	int32 GetNumFrames() const override { return Frames.Num(); }
	int32 GetNumMipLevels() const override { return Frames.IsEmpty() ? 0 : Frames[0].Num(); }
	FIntVector GetVolumeResolution() const override { return VolumeResolution; };
	const FSparseVolumeTextureSceneProxy* GetSparseVolumeTextureSceneProxy() const override { return GetStreamedFrameProxyOrFallback(0 /*FrameIndex*/, 0 /*MipLevel*/); };
	//~ End USparseVolumeTexture Interface.

	const FSparseVolumeTextureSceneProxy* GetStreamedFrameProxyOrFallback(int32 FrameIndex, int32 MipLevel) const;
	TArrayView<const FSparseVolumeTextureFrameMips> GetFrames() const;

protected:
	TArray<FSparseVolumeTextureFrameMips> Frames;

	void GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy();
	void GenerateOrLoadDDCRuntimeDataForFrame(FSparseVolumeTextureFrame& Frame, UE::DerivedData::FRequestOwner& DDCRequestOwner);
};

UCLASS(ClassGroup = Rendering, BlueprintType)//, hidecategories = (Object))
class ENGINE_API UStaticSparseVolumeTexture : public UStreamableSparseVolumeTexture
{
	GENERATED_UCLASS_BODY()

public:

	UStaticSparseVolumeTexture();
	virtual ~UStaticSparseVolumeTexture() = default;

	//~ Begin USparseVolumeTexture Interface.
	int32 GetNumFrames() const override { return 1; }
	//~ End USparseVolumeTexture Interface.

private:

#if WITH_EDITOR
	friend class USparseVolumeTextureFactory; // Importer
#endif
};

// UAnimatedSparseVolumeTexture inherit from USparseVolumeTexture to be viewed using the first frame by default.
UCLASS(ClassGroup = Rendering, BlueprintType)//, hidecategories = (Object))
class ENGINE_API UAnimatedSparseVolumeTexture : public UStreamableSparseVolumeTexture
{
	GENERATED_UCLASS_BODY()

public:

	UAnimatedSparseVolumeTexture();
	virtual ~UAnimatedSparseVolumeTexture() = default;

	//~ Begin USparseVolumeTexture Interface.
	const FSparseVolumeTextureSceneProxy* GetSparseVolumeTextureSceneProxy() const override;
	//~ End USparseVolumeTexture Interface.

	// Used for debugging a specific frame of an animated sequence.
	const FSparseVolumeTextureSceneProxy* GetSparseVolumeTextureFrameSceneProxy(int32 FrameIndex, int32 MipLevel) const;
	const FSparseVolumeAssetHeader* GetSparseVolumeTextureFrameHeader(int32 FrameIndex, int32 MipLevel) const;

private:

#if WITH_EDITOR
	friend class USparseVolumeTextureFactory; // Importer
#endif
	
	int32 PreviewFrameIndex;
	int32 PreviewMipLevel;
};

// USparseVolumeTextureFrame inherits from USparseVolumeTexture to be viewed using any given frame of a UAnimatedSparseVolumeTexture (or UStaticSparseVolumeTexture)
UCLASS(ClassGroup = Rendering, BlueprintType)//, hidecategories = (Object))
class ENGINE_API USparseVolumeTextureFrame : public USparseVolumeTexture
{
	GENERATED_UCLASS_BODY()

public:
	USparseVolumeTextureFrame();
	virtual ~USparseVolumeTextureFrame() = default;

	static USparseVolumeTextureFrame* CreateFrame(USparseVolumeTexture* Texture, int32 FrameIndex, int32 MipLevel);

	void Initialize(const FSparseVolumeTextureSceneProxy* InSceneProxy, const FIntVector& InVolumeResolution);

	//~ Begin USparseVolumeTexture Interface.
	int32 GetNumFrames() const override { return 1; }
	int32 GetNumMipLevels() const override { return 1; }
	FIntVector GetVolumeResolution() const override { return VolumeResolution; };
	const FSparseVolumeTextureSceneProxy* GetSparseVolumeTextureSceneProxy() const override { return SceneProxy; };
	//~ End USparseVolumeTexture Interface.

private:
	FIntVector3 VolumeResolution;
	const FSparseVolumeTextureSceneProxy* SceneProxy;
};

UCLASS(ClassGroup = Rendering, BlueprintType)//, hidecategories = (Object))
class ENGINE_API UAnimatedSparseVolumeTextureController : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float FrameRate = 24.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Rendering")
	int32 MipLevel = 0;

	UAnimatedSparseVolumeTextureController();
	virtual ~UAnimatedSparseVolumeTextureController() = default;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void Play();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void Pause();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void Stop();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	bool IsPlaying();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void Update(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetSparseVolumeTexture(USparseVolumeTexture* Texture);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetTime(float Time);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetFractionalFrameIndex(float Frame);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	USparseVolumeTexture* GetSparseVolumeTexture();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	float GetTime();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	float GetFractionalFrameIndex();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	USparseVolumeTextureFrame* GetCurrentFrame();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void GetLerpFrames(USparseVolumeTextureFrame*& Frame0, USparseVolumeTextureFrame*& Frame1, float& LerpAlpha);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	float GetDuration();

private:
	TObjectPtr<USparseVolumeTexture> SparseVolumeTexture;
	float Time;
	bool bIsPlaying;
};
