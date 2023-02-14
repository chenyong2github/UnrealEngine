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

struct ENGINE_API FSparseVolumeAssetHeader
{
	FIntVector3 PageTableVolumeResolution;
	FIntVector3 TileDataVolumeResolution;
	FIntVector3 SourceVolumeResolution;
	FIntVector3 SourceVolumeAABBMin;
	EPixelFormat AttributesAFormat;
	EPixelFormat AttributesBFormat;

	// The current data format version for the header.
	static const uint32 kVersion = 0;

	// This version can be used to convert existing header to new version later.
	uint32 Version;

	void Serialize(FArchive& Ar);

	FSparseVolumeAssetHeader()
		: PageTableVolumeResolution(FIntVector3(0, 0, 0))
		, TileDataVolumeResolution(FIntVector3(0, 0, 0))
		, SourceVolumeResolution(FIntVector3(0, 0, 0))
		, AttributesAFormat(PF_Unknown)
		, AttributesBFormat(PF_Unknown)
		, Version(kVersion)
	{
	}
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
	virtual int32 GetSizeX() const { return FMath::CeilToInt32(GetVolumeBounds().GetSize().X); }

	UFUNCTION(BlueprintCallable, Category = "Texture")
	virtual int32 GetSizeY() const { return FMath::CeilToInt32(GetVolumeBounds().GetSize().Y); }

	UFUNCTION(BlueprintCallable, Category = "Texture")
	virtual int32 GetSizeZ() const { return FMath::CeilToInt32(GetVolumeBounds().GetSize().Z); }

	UFUNCTION(BlueprintCallable, Category = "Texture")
	virtual int32 GetFrameCount() const { return 0; }
	virtual FBox GetVolumeBounds() const { return FBox(); }
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

	UPROPERTY(VisibleAnywhere, Category = "Rendering", meta = (DisplayName = "Volume Bounds"))
	FBox VolumeBounds;

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
	int32 GetFrameCount() const override { return Frames.Num(); }
	FBox GetVolumeBounds() const override { return VolumeBounds; };
	const FSparseVolumeTextureSceneProxy* GetSparseVolumeTextureSceneProxy() const override { return GetStreamedFrameProxyOrFallback(0); };
	//~ End USparseVolumeTexture Interface.

	const FSparseVolumeTextureSceneProxy* GetStreamedFrameProxyOrFallback(int32 FrameIndex) const;
	TArrayView<const FSparseVolumeTextureFrame> GetFrames() const;

protected:
	TArray<FSparseVolumeTextureFrame, TInlineAllocator<1u>> Frames;

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
	int32 GetFrameCount() const override { return 1; }
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
	const FSparseVolumeTextureSceneProxy* GetSparseVolumeTextureFrameSceneProxy(int32 FrameIndex) const;
	const FSparseVolumeAssetHeader* GetSparseVolumeTextureFrameHeader(int32 FrameIndex) const;

private:

#if WITH_EDITOR
	friend class USparseVolumeTextureFactory; // Importer
#endif
	
	int32 PreviewFrameIndex;
};

// USparseVolumeTextureFrame inherits from USparseVolumeTexture to be viewed using any given frame of a UAnimatedSparseVolumeTexture (or UStaticSparseVolumeTexture)
UCLASS(ClassGroup = Rendering, BlueprintType)//, hidecategories = (Object))
class ENGINE_API USparseVolumeTextureFrame : public USparseVolumeTexture
{
	GENERATED_UCLASS_BODY()

public:
	USparseVolumeTextureFrame();
	virtual ~USparseVolumeTextureFrame() = default;

	static USparseVolumeTextureFrame* CreateFrame(USparseVolumeTexture* Texture, int32 FrameIndex);

	void Initialize(const FSparseVolumeTextureSceneProxy* InSceneProxy, const FBox& InVolumeBounds);

	//~ Begin USparseVolumeTexture Interface.
	int32 GetFrameCount() const override { return 1; }
	FBox GetVolumeBounds() const override { return VolumeBounds; };
	const FSparseVolumeTextureSceneProxy* GetSparseVolumeTextureSceneProxy() const override { return SceneProxy; };
	//~ End USparseVolumeTexture Interface.

private:
	FBox VolumeBounds;
	const FSparseVolumeTextureSceneProxy* SceneProxy;
};

UCLASS(ClassGroup = Rendering, BlueprintType)//, hidecategories = (Object))
class ENGINE_API UAnimatedSparseVolumeTextureController : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	UPROPERTY(BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Frame Rate"))
	float FrameRate = 24.0f;

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
