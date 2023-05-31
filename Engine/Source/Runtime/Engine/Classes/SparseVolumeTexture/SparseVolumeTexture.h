// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/VolumeTexture.h"

#include "Serialization/EditorBulkData.h"
#include "Containers/Array.h"
#include "Containers/StaticArray.h"
#include "UnrealClient.h"
#include "UObject/ObjectSaveContext.h"
#include "Engine/TextureDefines.h"

#include "SparseVolumeTexture.generated.h"

namespace UE { namespace Shader	{ enum class EValueType : uint8; } }
namespace UE { namespace DerivedData { class FRequestOwner; } }

// SVT_TODO: Unify with macros in SparseVolumeTextureCommon.ush
#define SPARSE_VOLUME_TILE_RES 16
#define SPARSE_VOLUME_TILE_BORDER 1
#define SPARSE_VOLUME_TILE_RES_PADDED (SPARSE_VOLUME_TILE_RES + 2 * SPARSE_VOLUME_TILE_BORDER)

namespace UE
{
namespace SVT
{

struct FTextureData;
class FStreamingManager;

struct ENGINE_API FHeader
{
	static const uint32 kVersion = 0; // The current data format version for the header.
	uint32 Version = kVersion; // This version can be used to convert existing header to new version later.

	FIntVector3 VirtualVolumeResolution = FIntVector3(0, 0, 0);
	FIntVector3 VirtualVolumeAABBMin = FIntVector3(INT32_MAX, INT32_MAX, INT32_MAX);
	FIntVector3 VirtualVolumeAABBMax = FIntVector3(INT32_MIN, INT32_MIN, INT32_MIN);
	FIntVector3 PageTableVolumeResolution = FIntVector3(0, 0, 0);
	FIntVector3 PageTableVolumeAABBMin = FIntVector3(INT32_MAX, INT32_MAX, INT32_MAX);
	FIntVector3 PageTableVolumeAABBMax = FIntVector3(INT32_MIN, INT32_MIN, INT32_MIN);
	TStaticArray<EPixelFormat, 2> AttributesFormats = TStaticArray<EPixelFormat, 2>(InPlace, PF_Unknown);
	TStaticArray<FVector4f, 2> FallbackValues = TStaticArray<FVector4f, 2>(InPlace, FVector4f());

	FHeader() = default;
	FHeader(const FIntVector3& AABBMin, const FIntVector3& AABBMax, EPixelFormat FormatA, EPixelFormat FormatB, const FVector4f& FallbackValueA, const FVector4f& FallbackValueB);
	void Serialize(FArchive& Ar);
};

// Describes a mip level of a SVT frame in terms of the sizes and offsets of the data in the built bulk data.
struct FMipLevelStreamingInfo
{
	int32 BulkOffset;
	int32 BulkSize;
	int32 PageTableOffset; // relative to BulkOffset
	int32 PageTableSize;
	int32 TileDataAOffset; // relative to BulkOffset
	int32 TileDataASize;
	int32 TileDataBOffset; // relative to BulkOffset
	int32 TileDataBSize;
	int32 NumPhysicalTiles;
};

enum EResourceFlag : uint32
{
	EResourceFlag_StreamingDataInDDC = 1 << 0u, // FResources was cached, so MipLevelStreamingInfo can be streamed from DDC
};

// Represents the derived data of a SVT that is needed by the streaming manager.
struct FResources
{
public:
	FHeader Header;
	uint32 ResourceFlags = 0;
	// Info about sizes and offsets into the streamable mip level data. The last entry refers to the root mip level which is stored in RootData, not StreamableMipLevels.
	TArray<FMipLevelStreamingInfo> MipLevelStreamingInfo;
	// Data for the highest/"root" mip level
	TArray<uint8> RootData;
	// Data for all streamable mip levels
	FByteBulkData StreamableMipLevels;
#if WITH_EDITORONLY_DATA
	// FTextureData from which all the other data can be built with a call to Build()
	UE::Serialization::FEditorBulkData SourceData;
#endif

	// These are used for logging and retrieving StreamableMipLevels from DDC in FStreamingManager
#if WITH_EDITORONLY_DATA
	FString ResourceName;
	FIoHash DDCKeyHash;
	FIoHash DDCRawHash;
#endif

	// Called when serializing to/from DDC buffers and when serializing the owning USparseVolumeTextureFrame.
	void Serialize(FArchive& Ar, UObject* Owner, bool bCooked);
	// Returns true if there are streamable mip levels.
	bool HasStreamingData() const;
#if WITH_EDITORONLY_DATA
	// Removes the StreamableMipLevels bulk data if it was successfully cached to DDC.
	void DropBulkData();
	// Fills StreamableMipLevels with data from DDC. Returns true when done.
	bool RebuildBulkDataFromCacheAsync(const UObject* Owner, bool& bFailed);
	// Builds all the data from SourceData. Is called by Cache().
	bool Build(USparseVolumeTextureFrame* Owner);
	// Cache the built data to/from DDC.
	void Cache(USparseVolumeTextureFrame* Owner);
#endif

private:
#if WITH_EDITORONLY_DATA
	enum class EDDCRebuildState : uint8
	{
		Initial,
		Pending,
		Succeeded,
		Failed,
	};
	TDontCopy<TPimplPtr<UE::DerivedData::FRequestOwner>> DDCRequestOwner;
	std::atomic<EDDCRebuildState> DDCRebuildState;
	void BeginRebuildBulkDataFromCache(const UObject* Owner);
	void EndRebuildBulkDataFromCache();
#endif
};

// Encapsulates RHI resources needed to render a SparseVolumeTexture.
class ENGINE_API FTextureRenderResources : public ::FRenderResource
{
	friend class FStreamingManager;
public:
	const FHeader& GetHeader() const								{ check(IsInParallelRenderingThread()); return Header; }
	FIntVector3 GetTileDataTextureResolution() const				{ check(IsInParallelRenderingThread()); return TileDataTextureResolution; }
	int32 GetFrameIndex() const										{ check(IsInParallelRenderingThread()); return FrameIndex; }
	int32 GetNumLogicalMipLevels() const							{ check(IsInParallelRenderingThread()); return NumLogicalMipLevels; }
	FTextureRHIRef GetPageTableTextureRHI() const					{ check(IsInParallelRenderingThread()); return PageTableTextureRHI; }
	FTextureRHIRef GetPhysicalTileDataATextureRHI() const			{ check(IsInParallelRenderingThread()); return PhysicalTileDataATextureRHI; }
	FTextureRHIRef GetPhysicalTileDataBTextureRHI() const			{ check(IsInParallelRenderingThread()); return PhysicalTileDataBTextureRHI; }
	FShaderResourceViewRHIRef GetStreamingInfoBufferSRVRHI() const	{ check(IsInParallelRenderingThread()); return StreamingInfoBufferSRVRHI; }
	void GetPackedUniforms(FUintVector4& OutPacked0, FUintVector4& OutPacked1) const;
	// Updates the GlobalVolumeResolution member in a thread-safe way.
	void SetGlobalVolumeResolution_GameThread(const FIntVector3& GlobalVolumeResolution);

	//~ Begin FRenderResource Interface.
	virtual void InitRHI() override { /* Managed by FStreamingManager */ }
	virtual void ReleaseRHI() override { /* Managed by FStreamingManager */ }
	//~ End FRenderResource Interface.

private:
	FHeader Header;
	FIntVector3 GlobalVolumeResolution = FIntVector3::ZeroValue; // The virtual resolution of the union of the AABBs of all frames. Needed for GetPackedUniforms().
	FIntVector3 TileDataTextureResolution = FIntVector3::ZeroValue;
	int32 FrameIndex = INDEX_NONE;
	int32 NumLogicalMipLevels = 0; // Might not all be resident in GPU memory
	FTextureRHIRef PageTableTextureRHI;
	FTextureRHIRef PhysicalTileDataATextureRHI;
	FTextureRHIRef PhysicalTileDataBTextureRHI;
	FShaderResourceViewRHIRef StreamingInfoBufferSRVRHI;
};

}
}

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

	UFUNCTION(BlueprintCallable, Category = "Texture")
	virtual int32 GetNumMipLevels() const { return 0; }

	virtual FIntVector GetVolumeResolution() const { return FIntVector(); }
	virtual EPixelFormat GetFormat(int32 AttributesIndex) const { return PF_Unknown; }
	virtual FVector4f GetFallbackValue(int32 AttributesIndex) const { return FVector4f(); }
	virtual TextureAddress GetTextureAddressX() const { return TA_Wrap; }
	virtual TextureAddress GetTextureAddressY() const { return TA_Wrap; }
	virtual TextureAddress GetTextureAddressZ() const { return TA_Wrap; }
	virtual const UE::SVT::FTextureRenderResources* GetTextureRenderResources() const { return nullptr; }

	/** Getter for the shader uniform parameters with index as ESparseVolumeTextureShaderUniform. */
	FVector4 GetUniformParameter(int32 Index) const { return FVector4(ForceInitToZero); } // SVT_TODO: This mechanism is no longer needed and can be removed

	/** Getter for the shader uniform parameter type with index as ESparseVolumeTextureShaderUniform. */
	static UE::Shader::EValueType GetUniformParameterType(int32 Index);

#if WITH_EDITOR
	enum class ENotifyMaterialsEffectOnShaders
	{
		Default,
		DoesNotInvalidate
	};

	/** Notify any loaded material instances that the texture has changed. */
	void NotifyMaterials(const ENotifyMaterialsEffectOnShaders EffectOnShaders = ENotifyMaterialsEffectOnShaders::Default);
#endif // WITH_EDITOR
};

UCLASS(ClassGroup = Rendering, BlueprintType)
class ENGINE_API USparseVolumeTextureFrame : public USparseVolumeTexture
{
	GENERATED_UCLASS_BODY()

	friend class UE::SVT::FStreamingManager;
	friend class UStreamableSparseVolumeTexture;

public:

	USparseVolumeTextureFrame();
	virtual ~USparseVolumeTextureFrame() = default;

	// Retrieves a frame from the given SparseVolumeTexture and also issues a streaming request for it. 
	// FrameIndex is of float type so that the streaming system can use the fractional part to more easily keep track of playback speed and direction (forward/reverse playback).
	// MipLevel is the lowest mip level that the caller intends to use but does not guarantee that the mip is actually resident.
	static USparseVolumeTextureFrame* GetFrameAndIssueStreamingRequest(USparseVolumeTexture* SparseVolumeTexture, float FrameIndex, int32 MipLevel);

	bool Initialize(USparseVolumeTexture* InOwner, int32 InFrameIndex, UE::SVT::FTextureData& UncookedFrame);
	int32 GetFrameIndex() const { return FrameIndex; }
	UE::SVT::FResources* GetResources() { return &Resources; }
	// Creates TextureRenderResources if they don't already exist. Returns false if they already existed.
	bool CreateTextureRenderResources();

#if WITH_EDITORONLY_DATA
	// Caches the derived data (FResources) of this frame to/from DDC and ensures that FTextureRenderResources exists.
	void Cache();
#endif

	//~ Begin UObject Interface.
	virtual void PostLoad() override;
	virtual void FinishDestroy() override;
	virtual void BeginDestroy() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
#if WITH_EDITOR
	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
	virtual bool IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform) override;
	virtual void WillNeverCacheCookedPlatformDataAgain() override;
	virtual void ClearCachedCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
	virtual void ClearAllCachedCookedPlatformData() override;
#endif
	//~ End UObject Interface.

	//~ Begin USparseVolumeTexture Interface.
	virtual int32 GetNumFrames() const override { return 1; }
	virtual int32 GetNumMipLevels() const override { return Owner->GetNumMipLevels(); }
	virtual FIntVector GetVolumeResolution() const override { return Owner->GetVolumeResolution(); }
	virtual EPixelFormat GetFormat(int32 AttributesIndex) const override { return Owner->GetFormat(AttributesIndex); }
	virtual FVector4f GetFallbackValue(int32 AttributesIndex) const override { return Owner->GetFallbackValue(AttributesIndex); }
	virtual TextureAddress GetTextureAddressX() const override { return Owner->GetTextureAddressX(); }
	virtual TextureAddress GetTextureAddressY() const override { return Owner->GetTextureAddressY(); }
	virtual TextureAddress GetTextureAddressZ() const override { return Owner->GetTextureAddressZ(); }
	virtual const UE::SVT::FTextureRenderResources* GetTextureRenderResources() const override { return TextureRenderResources; }
	//~ End USparseVolumeTexture Interface.

private:
	UPROPERTY()
	TObjectPtr<USparseVolumeTexture> Owner;
	int32 FrameIndex;
	UE::SVT::FResources Resources;
	UE::SVT::FTextureRenderResources* TextureRenderResources;
};

UCLASS(ClassGroup = Rendering, BlueprintType)//, hidecategories = (Object))
class UStreamableSparseVolumeTexture : public USparseVolumeTexture
{
	GENERATED_UCLASS_BODY()

public:

	enum class EInitState : uint8
	{
		Uninitialized,
		Pending,
		Done,
		Failed,
	};

	UPROPERTY(VisibleAnywhere, Category = "Texture")
	FIntVector VolumeResolution;

	UPROPERTY(VisibleAnywhere, Category = "Texture")
	int32 NumMipLevels;

	UPROPERTY(VisibleAnywhere, Category = "Texture")
	TEnumAsByte<enum EPixelFormat> FormatA;

	UPROPERTY(VisibleAnywhere, Category = "Texture")
	TEnumAsByte<enum EPixelFormat> FormatB;

	UPROPERTY(VisibleAnywhere, Category = "Texture")
	FVector4f FallbackValueA;

	UPROPERTY(VisibleAnywhere, Category = "Texture")
	FVector4f FallbackValueB;

	/** The addressing mode to use for the X axis.								*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture", meta = (DisplayName = "X-axis Tiling Method"), AssetRegistrySearchable, AdvancedDisplay)
	TEnumAsByte<enum TextureAddress> AddressX;

	/** The addressing mode to use for the Y axis.								*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture", meta = (DisplayName = "Y-axis Tiling Method"), AssetRegistrySearchable, AdvancedDisplay)
	TEnumAsByte<enum TextureAddress> AddressY;

	/** The addressing mode to use for the Z axis.								*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture", meta = (DisplayName = "Z-axis Tiling Method"), AssetRegistrySearchable, AdvancedDisplay)
	TEnumAsByte<enum TextureAddress> AddressZ;

	UStreamableSparseVolumeTexture();
	virtual ~UStreamableSparseVolumeTexture() = default;

	// Multi-phase initialization: Call BeginInitialize(), then call AppendFrame() for each frame to add and then finish initialization with a call to EndInitialize().
	// The NumExpectedFrames parameter on BeginInitialize() just serves as a potential optimization to reserve memory for the frames to be appended
	// and doesn't need to match the exact number if it is not known at the time.
	virtual bool BeginInitialize(int32 NumExpectedFrames);
	virtual bool AppendFrame(UE::SVT::FTextureData& UncookedFrame);
	virtual bool EndInitialize(int32 NumMipLevels = INDEX_NONE /*Create entire mip chain by default*/);

	// Convenience function wrapping the multi-phase initialization functions above
	virtual bool Initialize(const TArrayView<UE::SVT::FTextureData>& UncookedData, int32 NumMipLevels = INDEX_NONE /*Create entire mip chain by default*/);
	// Consider using USparseVolumeTextureFrame::GetFrameAndIssueStreamingRequest() if the frame should have streaming requests issued.
	USparseVolumeTextureFrame* GetFrame(int32 FrameIndex) const { return Frames.IsValidIndex(FrameIndex) ? Frames[FrameIndex] : nullptr; }

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
	virtual int32 GetNumFrames() const override { return Frames.Num(); }
	virtual int32 GetNumMipLevels() const override { return NumMipLevels; }
	virtual FIntVector GetVolumeResolution() const override { return VolumeResolution; };
	virtual EPixelFormat GetFormat(int32 AttributesIndex) const override { check(AttributesIndex >= 0 && AttributesIndex < 2) return AttributesIndex == 0 ? FormatA : FormatB; }
	virtual FVector4f GetFallbackValue(int32 AttributesIndex) const override { check(AttributesIndex >= 0 && AttributesIndex < 2) return AttributesIndex == 0 ? FallbackValueA : FallbackValueB; }
	virtual TextureAddress GetTextureAddressX() const override { return AddressX; }
	virtual TextureAddress GetTextureAddressY() const override { return AddressY; }
	virtual TextureAddress GetTextureAddressZ() const override { return AddressZ; }
	virtual const UE::SVT::FTextureRenderResources* GetTextureRenderResources() const override { return Frames.IsEmpty() ? nullptr : Frames[0]->GetTextureRenderResources(); }
	//~ End USparseVolumeTexture Interface.

protected:

	UPROPERTY()
	TArray<TObjectPtr<USparseVolumeTextureFrame>> Frames;
#if WITH_EDITORONLY_DATA
	FIntVector VolumeBoundsMin;
	FIntVector VolumeBoundsMax;
	EInitState InitState = EInitState::Uninitialized;

	// Ensures all frames have derived data (based on the source data and the current settings like TextureAddress modes etc.) cached to DDC and are ready for rendering.
	// Disconnects this SVT from the streaming manager, calls Cache() on all frames and finally connects to FStreamingManager again.
	void RecacheFrames();
#endif // WITH_EDITORONLY_DATA
};

UCLASS(ClassGroup = Rendering, BlueprintType)//, hidecategories = (Object))
class ENGINE_API UStaticSparseVolumeTexture : public UStreamableSparseVolumeTexture
{
	GENERATED_UCLASS_BODY()

public:

	UStaticSparseVolumeTexture();
	virtual ~UStaticSparseVolumeTexture() = default;

	// Override AppendFrame() to ensure that there is never more than a single frame in a static SVT
	virtual bool AppendFrame(UE::SVT::FTextureData& UncookedFrame) override;

	//~ Begin USparseVolumeTexture Interface.
	int32 GetNumFrames() const override { return 1; }
	//~ End USparseVolumeTexture Interface.

private:
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
	virtual const UE::SVT::FTextureRenderResources* GetTextureRenderResources() const override { return Frames.IsValidIndex(PreviewFrameIndex) ? Frames[PreviewFrameIndex]->GetTextureRenderResources() : nullptr; }
	//~ End USparseVolumeTexture Interface.

private:
	int32 PreviewFrameIndex;
};

UCLASS(ClassGroup = Rendering, BlueprintType)//, hidecategories = (Object))
class ENGINE_API UAnimatedSparseVolumeTextureController : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	TObjectPtr<USparseVolumeTexture> SparseVolumeTexture;
	
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float Time;

	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	bool bIsPlaying;

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
	void Update(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	float GetFractionalFrameIndex();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	USparseVolumeTextureFrame* GetFrameByIndex(int32 FrameIndex);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	USparseVolumeTextureFrame* GetCurrentFrame();

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void GetCurrentFramesForInterpolation(USparseVolumeTextureFrame*& Frame0, USparseVolumeTextureFrame*& Frame1, float& LerpAlpha);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	float GetDuration();
};
