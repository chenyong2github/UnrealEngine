// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/IndirectArray.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "Templates/SubclassOf.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "RenderCommandFence.h"
#include "RenderResource.h"
#include "Serialization/BulkData.h"
#include "Engine/TextureDefines.h"
#include "MaterialShared.h"
#include "TextureResource.h"
#include "Engine/StreamableRenderAsset.h"
#include "PerPlatformProperties.h"
#include "Misc/FieldAccessor.h"
#include "Texture.generated.h"

class ITargetPlatform;
class UAssetUserData;
struct FPropertyChangedEvent;

// This needs to be mirrored in EditorFactories.cpp.
UENUM()
enum TextureCompressionSettings
{
	TC_Default					UMETA(DisplayName="Default (DXT1/5, BC1/3 on DX11)"),
	TC_Normalmap				UMETA(DisplayName="Normalmap (DXT5, BC5 on DX11)"),
	TC_Masks					UMETA(DisplayName="Masks (no sRGB)"),
	TC_Grayscale				UMETA(DisplayName="Grayscale (R8, RGB8 sRGB)"),
	TC_Displacementmap			UMETA(DisplayName="Displacementmap (8/16bit)"),
	TC_VectorDisplacementmap	UMETA(DisplayName="VectorDisplacementmap (RGBA8)"),
	TC_HDR						UMETA(DisplayName="HDR (RGB, no sRGB)"),
	TC_EditorIcon				UMETA(DisplayName="UserInterface2D (RGBA)"),
	TC_Alpha					UMETA(DisplayName="Alpha (no sRGB, BC4 on DX11)"),
	TC_DistanceFieldFont		UMETA(DisplayName="DistanceFieldFont (R8)"),
	TC_HDR_Compressed			UMETA(DisplayName="HDRCompressed (RGB, BC6H, DX11)"),
	TC_BC7						UMETA(DisplayName="BC7 (DX11, optional A)"),
	TC_HalfFloat				UMETA(DisplayName="Half Float (R16F)"),
	TC_EncodedReflectionCapture		UMETA(Hidden),
	TC_MAX,
};

UENUM()
enum TextureFilter
{
	TF_Nearest UMETA(DisplayName="Nearest"),
	TF_Bilinear UMETA(DisplayName="Bi-linear"),
	TF_Trilinear UMETA(DisplayName="Tri-linear"),
	/** Use setting from the Texture Group. */
	TF_Default UMETA(DisplayName="Default (from Texture Group)"),
	TF_MAX,
};

UENUM()
enum TextureAddress
{
	TA_Wrap UMETA(DisplayName="Wrap"),
	TA_Clamp UMETA(DisplayName="Clamp"),
	TA_Mirror UMETA(DisplayName="Mirror"),
	TA_MAX,
};

UENUM()
enum ECompositeTextureMode
{
	CTM_Disabled UMETA(DisplayName="Disabled"),
	/** CompositingTexture needs to be a normal map with the same or larger size. */
	CTM_NormalRoughnessToRed UMETA(DisplayName="Add Normal Roughness To Red"),
	/** CompositingTexture needs to be a normal map with the same or larger size. */
	CTM_NormalRoughnessToGreen UMETA(DisplayName="Add Normal Roughness To Green"),
	/** CompositingTexture needs to be a normal map with the same or larger size. */
	CTM_NormalRoughnessToBlue UMETA(DisplayName="Add Normal Roughness To Blue"),
	/** CompositingTexture needs to be a normal map with the same or larger size. */
	CTM_NormalRoughnessToAlpha UMETA(DisplayName="Add Normal Roughness To Alpha"),
	CTM_MAX,

	// Note: These are serialized as as raw values in the texture DDC key, so additional entries
	// should be added at the bottom; reordering or removing entries will require changing the GUID
	// in the texture compressor DDC key
};

UENUM()
enum ETextureMipCount
{
	TMC_ResidentMips,
	TMC_AllMips,
	TMC_AllMipsBiased,
	TMC_MAX,
};

UENUM()
enum ETextureSourceArtType
{
	/** FColor Data[SrcWidth * SrcHeight]. */
	TSAT_Uncompressed,
	/** PNG compresed version of FColor Data[SrcWidth * SrcHeight]. */
	TSAT_PNGCompressed,
	/** DDS file with header. */
	TSAT_DDSFile,
	TSAT_MAX,
};

UENUM()
enum ETextureSourceFormat
{
	TSF_Invalid,
	TSF_G8,
	TSF_BGRA8,
	TSF_BGRE8,
	TSF_RGBA16,
	TSF_RGBA16F,

	//@todo: Deprecated!
	TSF_RGBA8,
	//@todo: Deprecated!
	TSF_RGBE8,

	TSF_G16,

	TSF_MAX
};

UENUM()
enum ETextureCompressionQuality
{
	TCQ_Default = 0		UMETA(DisplayName="Default"),
	TCQ_Lowest = 1		UMETA(DisplayName="Lowest"),
	TCQ_Low = 2			UMETA(DisplayName="Low"),
	TCQ_Medium = 3		UMETA(DisplayName="Medium"),
	TCQ_High= 4			UMETA(DisplayName="High"),
	TCQ_Highest = 5		UMETA(DisplayName="Highest"),
	TCQ_MAX,
};

USTRUCT()
struct FTextureSourceBlock
{
	GENERATED_USTRUCT_BODY()

	ENGINE_API FTextureSourceBlock();

	UPROPERTY(VisibleAnywhere, Category = TextureSource)
	int32 BlockX;

	UPROPERTY(VisibleAnywhere, Category = TextureSource)
	int32 BlockY;

	UPROPERTY(VisibleAnywhere, Category = TextureSource)
	int32 SizeX;

	UPROPERTY(VisibleAnywhere, Category = TextureSource)
	int32 SizeY;

	UPROPERTY(VisibleAnywhere, Category = TextureSource)
	int32 NumSlices;

	UPROPERTY(VisibleAnywhere, Category = TextureSource)
	int32 NumMips;
};

/**
 * Texture source data management.
 */
USTRUCT()
struct FTextureSource
{
	GENERATED_USTRUCT_BODY()

	/** Default constructor. */
	ENGINE_API FTextureSource();

#if WITH_EDITOR

	ENGINE_API static int32 GetBytesPerPixel(ETextureSourceFormat Format);
	FORCEINLINE static bool IsHDR(ETextureSourceFormat Format) { return (Format == TSF_BGRE8 || Format == TSF_RGBA16F); }

	ENGINE_API void InitBlocked(const ETextureSourceFormat* InLayerFormats,
		const FTextureSourceBlock* InBlocks,
		int32 InNumLayers,
		int32 InNumBlocks,
		const uint8** InDataPerBlock);

	ENGINE_API void InitLayered(
		int32 NewSizeX,
		int32 NewSizeY,
		int32 NewNumSlices,
		int32 NewNumLayers,
		int32 NewNumMips,
		const ETextureSourceFormat* NewLayerFormat,
		const uint8* NewData = NULL
		);

	/**
	 * Initialize the source data with the given size, number of mips, and format.
	 * @param NewSizeX - Width of the texture source data.
	 * @param NewSizeY - Height of the texture source data.
	 * @param NewNumSlices - The number of slices in the texture source data.
	 * @param NewNumMips - The number of mips in the texture source data.
	 * @param NewFormat - The format in which source data is stored.
	 * @param NewData - [optional] The new source data.
	 */
	ENGINE_API void Init(
		int32 NewSizeX,
		int32 NewSizeY,
		int32 NewNumSlices,
		int32 NewNumMips,
		ETextureSourceFormat NewFormat,
		const uint8* NewData = NULL
		);

	/**
	 * Initializes the source data for a 2D texture with a full mip chain.
	 * @param NewSizeX - Width of the texture source data.
	 * @param NewSizeY - Height of the texture source data.
	 * @param NewFormat - Format of the texture source data.
	 */
	ENGINE_API void Init2DWithMipChain(
		int32 NewSizeX,
		int32 NewSizeY,
		ETextureSourceFormat NewFormat
		);

	ENGINE_API void InitLayered2DWithMipChain(
		int32 NewSizeX,
		int32 NewSizeY,
		int32 NewNumLayers,
		const ETextureSourceFormat* NewFormat
	);

	/**
	 * Initializes the source data for a cubemap with a full mip chain.
	 * @param NewSizeX - Width of each cube map face.
	 * @param NewSizeY - Height of each cube map face.
	 * @param NewFormat - Format of the cube map source data.
	 */
	ENGINE_API void InitCubeWithMipChain(
		int32 NewSizeX,
		int32 NewSizeY,
		ETextureSourceFormat NewFormat
		);

	/** PNG Compresses the source art if possible or tells the bulk data to zlib compress when it saves out to disk. */
	ENGINE_API void Compress();

	/** Force the GUID to change even if mip data has not been modified. */
	void ForceGenerateGuid();

	/** Lock a mip for editing. */
	ENGINE_API uint8* LockMip(int32 BlockIndex, int32 LayerIndex, int32 MipIndex);

	/** Unlock a mip. */
	ENGINE_API void UnlockMip(int32 BlockIndex, int32 LayerIndex, int32 MipIndex);

	/** Retrieve a copy of the data for a particular mip. */
	ENGINE_API bool GetMipData(TArray64<uint8>& OutMipData, int32 BlockIndex, int32 LayerIndex, int32 MipIndex, class IImageWrapperModule* ImageWrapperModule = nullptr);

	/** Computes the size of a single mip. */
	ENGINE_API int64 CalcMipSize(int32 BlockIndex, int32 LayerIndex, int32 MipIndex) const;

	/** Computes the number of bytes per-pixel. */
	ENGINE_API int32 GetBytesPerPixel(int32 LayerIndex = 0) const;

	/** Return true if the source data is power-of-2. */
	ENGINE_API bool IsPowerOfTwo(int32 BlockIndex = 0) const;

	/** Returns true if source art is available. */
	ENGINE_API bool IsValid() const;

	/** Access the given block */
	ENGINE_API void GetBlock(int32 Index, FTextureSourceBlock& OutBlock) const;

	/** Logical size of the texture includes all blocks */
	ENGINE_API FIntPoint GetLogicalSize() const;

	/** Size of texture in blocks */
	ENGINE_API FIntPoint GetSizeInBlocks() const;

	/** Returns the unique ID string for this source art. */
	FString GetIdString() const;

	/** Trivial accessors. These will only give values for Block0 so may not be correct for UDIM/multi-block textures, use GetBlock() for this case. */
	FORCEINLINE FGuid GetId() const { return Id; }
	FORCEINLINE int32 GetSizeX() const { return SizeX; }
	FORCEINLINE int32 GetSizeY() const { return SizeY; }
	FORCEINLINE int32 GetNumSlices() const { return NumSlices; }
	FORCEINLINE int32 GetNumMips() const { return NumMips; }
	FORCEINLINE int32 GetNumLayers() const { return NumLayers; }
	FORCEINLINE int32 GetNumBlocks() const { return Blocks.Num() + 1; }
	FORCEINLINE ETextureSourceFormat GetFormat(int32 LayerIndex = 0) const { return (LayerIndex == 0) ? Format : LayerFormat[LayerIndex]; }
	FORCEINLINE bool IsPNGCompressed() const { return bPNGCompressed; }
	FORCEINLINE int64 GetSizeOnDisk() const { return BulkData.GetBulkDataSize(); }
	FORCEINLINE bool IsBulkDataLoaded() const { return BulkData.IsBulkDataLoaded(); }
	FORCEINLINE bool LoadBulkDataWithFileReader() { return BulkData.LoadBulkDataWithFileReader(); }
	FORCEINLINE void RemoveBulkData() { BulkData.RemoveBulkData(); }
	
	/** Sets the GUID to use, and whether that GUID is actually a hash of some data. */
	ENGINE_API void SetId(const FGuid& InId, bool bInGuidIsHash);

	/** Legacy API that defaults to LayerIndex 0 */
	FORCEINLINE bool GetMipData(TArray64<uint8>& OutMipData, int32 MipIndex, class IImageWrapperModule* ImageWrapperModule = nullptr)
	{
		return GetMipData(OutMipData, 0, 0, MipIndex, ImageWrapperModule);
	}

	FORCEINLINE int64 CalcMipSize(int32 MipIndex) const { return CalcMipSize(0, 0, MipIndex); }
	FORCEINLINE uint8* LockMip(int32 MipIndex) { return LockMip(0, 0, MipIndex); }
	FORCEINLINE void UnlockMip(int32 MipIndex) { UnlockMip(0, 0, MipIndex); }

#endif

private:
	/** Allow UTexture access to internals. */
	friend class UTexture;
	friend class UTexture2D;
	friend class UTextureCube;
	friend class UVolumeTexture;
	friend class UTexture2DArray;

	/** The bulk source data. */
	FByteBulkData BulkData;
	/** Pointer to locked mip data, if any. */
	uint8* LockedMipData;
	/** Number of mips that are locked. */
	uint32 NumLockedMips;
#if WITH_EDITOR

	/** Return true if the source art is not png compressed but could be. */
	bool CanPNGCompress() const;
	/** Removes source data. */
	void RemoveSourceData();
	/** Retrieve the size and offset for a source mip. The size includes all slices. */
	int64 CalcMipOffset(int32 BlockIndex, int32 LayerIndex, int32 MipIndex) const;

	int64 CalcBlockSize(int32 BlockIndex) const;
	int64 CalcLayerSize(int32 BlockIndex, int32 LayerIndex) const;

public:
	/** Uses a hash as the GUID, useful to prevent creating new GUIDs on load for legacy assets. */
	ENGINE_API void UseHashAsGuid();

	void ReleaseSourceMemory(); // release the memory from the mips (does almost the same as remove source data except doesn't rebuild the guid)
	FORCEINLINE bool HasHadBulkDataCleared() const { return bHasHadBulkDataCleared; }
private:
	/** Used while cooking to clear out unneeded memory after compression */
	bool bHasHadBulkDataCleared;
#endif

#if WITH_EDITORONLY_DATA
	/** GUID used to track changes to the source data. */
	UPROPERTY(VisibleAnywhere, Category=TextureSource)
	FGuid Id;

	/** Position of texture block0, only relevant if source has multiple blocks */
	UPROPERTY(VisibleAnywhere, Category = TextureSource)
	int32 BaseBlockX;

	UPROPERTY(VisibleAnywhere, Category = TextureSource)
	int32 BaseBlockY;

	/** Width of the texture. */
	UPROPERTY(VisibleAnywhere, Category=TextureSource)
	int32 SizeX;

	/** Height of the texture. */
	UPROPERTY(VisibleAnywhere, Category=TextureSource)
	int32 SizeY;

	/** Depth (volume textures) or faces (cube maps). */
	UPROPERTY(VisibleAnywhere, Category=TextureSource)
	int32 NumSlices;

	/** Number of mips provided as source data for the texture. */
	UPROPERTY(VisibleAnywhere, Category=TextureSource)
	int32 NumMips;

	/** Number of layers (for multi-layered virtual textures) provided as source data for the texture. */
	UPROPERTY(VisibleAnywhere, Category = TextureSource)
	int32 NumLayers;

	/** RGBA8 source data is optionally compressed as PNG. */
	UPROPERTY(VisibleAnywhere, Category=TextureSource)
	bool bPNGCompressed;

	/** Uses hash instead of guid to identify content to improve DDC cache hit. */
	UPROPERTY(VisibleAnywhere, Category=TextureSource)
	bool bGuidIsHash;

	/** Format in which the source data is stored. */
	UPROPERTY(VisibleAnywhere, Category=TextureSource)
	TEnumAsByte<enum ETextureSourceFormat> Format;

	/** For multi-layered sources, each layer may have a different format (in this case LayerFormat[0] == Format) . */
	UPROPERTY(VisibleAnywhere, Category = TextureSource)
	TArray< TEnumAsByte<enum ETextureSourceFormat> > LayerFormat;

	/**
	 * All sources have 1 implicit block defined by BaseBlockXY/SizeXY members.  Textures imported as UDIM may have additional blocks defined here.
	 * These are stored sequentially in the source's bulk data.
	 */
	UPROPERTY(VisibleAnywhere, Category = TextureSource)
	TArray<FTextureSourceBlock> Blocks;

#endif // WITH_EDITORONLY_DATA
};

/**
 * Optional extra fields for texture platform data required by some platforms.
 * Data in this struct is only serialized if the struct's value is non-default.
 */
struct FOptTexturePlatformData
{
	/** Arbitrary extra data that the runtime may need. */
	uint32 ExtData;
	/** Number of mips making up the mip tail, which must always be resident */
	uint32 NumMipsInTail;

	FOptTexturePlatformData()
		: ExtData(0)
		, NumMipsInTail(0)
	{}

	inline bool operator == (FOptTexturePlatformData const& RHS) const 
	{
		return ExtData == RHS.ExtData
			&& NumMipsInTail == RHS.NumMipsInTail;
	}

	inline bool operator != (FOptTexturePlatformData const& RHS) const
	{
		return !(*this == RHS);
	}

	friend inline FArchive& operator << (FArchive& Ar, FOptTexturePlatformData& Data)
	{
		Ar << Data.ExtData;
		Ar << Data.NumMipsInTail;
		return Ar;
	}
};

/**
 * Platform-specific data used by the texture resource at runtime.
 */
USTRUCT()
struct FTexturePlatformData
{
	GENERATED_USTRUCT_BODY()

	/** Width of the texture. */
	int32 SizeX;
	/** Height of the texture. */
	int32 SizeY;
	/** Packed bits [b31: CubeMap], [b30: HasOptData], [b29-0: NumSlices]. See bit masks below. */
	uint32 PackedData;
	/** Format in which mip data is stored. */
	EPixelFormat PixelFormat;
	/** Additional data required by some platforms.*/
	FOptTexturePlatformData OptData;
	/** Mip data or VT data. one or the other. */
	TIndirectArray<struct FTexture2DMipMap> Mips;
	struct FVirtualTextureBuiltData* VTData;

#if WITH_EDITORONLY_DATA
	/** The key associated with this derived data. */
	FString DerivedDataKey;
	/** Async cache task if one is outstanding. */
	struct FTextureAsyncCacheDerivedDataTask* AsyncTask;
#endif

	/** Default constructor. */
	ENGINE_API FTexturePlatformData();

	/** Destructor. */
	ENGINE_API ~FTexturePlatformData();

private:
	static constexpr uint32 BitMask_CubeMap    = 1u << 31u;
	static constexpr uint32 BitMask_HasOptData = 1u << 30u;
	static constexpr uint32 BitMask_NumSlices  = BitMask_HasOptData - 1u;

public:
	/** Return whether TryLoadMips() would stall because async loaded mips are not yet available. */
	bool IsReadyForAsyncPostLoad() const;

	/**
	 * Try to load mips from the derived data cache.
	 * @param FirstMipToLoad - The first mip index to load.
	 * @param OutMipData -	Must point to an array of pointers with at least
	 *						Texture.Mips.Num() - FirstMipToLoad + 1 entries. Upon
	 *						return those pointers will contain mip data.
	 * @param Texture - The texture to load mips for.
	 * @returns true if all requested mips have been loaded.
	 */
	bool TryLoadMips(int32 FirstMipToLoad, void** OutMipData, UTexture* Texture);

	/** Serialization. */
	void Serialize(FArchive& Ar, class UTexture* Owner);

	/** 
	 * Serialization for cooked builds.
	 *
	 * @param Ar Archive to serialize with
	 * @param Owner Owner texture
	 * @param bStreamable Store some mips inline, only used during cooking
	 */
	void SerializeCooked(FArchive& Ar, class UTexture* Owner, bool bStreamable);
	
	inline bool GetHasOptData() const
	{
		return (PackedData & BitMask_HasOptData) == BitMask_HasOptData;
	}

	inline void SetOptData(FOptTexturePlatformData Data)
	{
		// Set the opt data flag to true if the specified data is non-default.
		bool bHasOptData = Data != FOptTexturePlatformData();
		PackedData = (bHasOptData ? BitMask_HasOptData : 0) | (PackedData & (~BitMask_HasOptData));

		OptData = Data;
	}

	inline bool IsCubemap() const
	{
		return (PackedData & BitMask_CubeMap) == BitMask_CubeMap; 
	}

	inline void SetIsCubemap(bool bCubemap)
	{
		PackedData = (bCubemap ? BitMask_CubeMap : 0) | (PackedData & (~BitMask_CubeMap));
	}
	
	inline int32 GetNumSlices() const 
	{
		return (int32)(PackedData & BitMask_NumSlices);
	}

	inline void SetNumSlices(int32 NumSlices)
	{
		PackedData = (NumSlices & BitMask_NumSlices) | (PackedData & (~BitMask_NumSlices));
	}

	inline int32 GetNumMipsInTail() const
	{
		return OptData.NumMipsInTail;
	}

	inline int32 GetExtData() const
	{
		return OptData.ExtData;
	}

#if WITH_EDITOR
	bool IsAsyncWorkComplete() const;
	void Cache(
		class UTexture& InTexture,
		const struct FTextureBuildSettings* InSettingsPerLayer,
		uint32 InFlags,
		class ITextureCompressorModule* Compressor);
	void FinishCache();
	ENGINE_API bool TryInlineMipData(int32 FirstMipToLoad = 0, UTexture* Texture = nullptr);
	bool AreDerivedMipsAvailable() const;
	bool AreDerivedVTChunksAvailable() const;
#endif

	/** Return the number of mips that are not streamable. */
	int32 GetNumNonStreamingMips() const;
	/** Return the number of mips that streamable but not optional. */
	int32 GetNumNonOptionalMips() const;
	/** Return true if at least one mip can be loaded either from DDC or disk. */
	bool CanBeLoaded() const;

	// Only because we don't want to expose FVirtualTextureBuiltData
	ENGINE_API int32 GetNumVTMips() const;
	ENGINE_API EPixelFormat GetLayerPixelFormat(uint32 LayerIndex) const;
};

/**
 * Collection of values that contribute to pixel format chosen for texture
 */
USTRUCT()
struct FTextureFormatSettings
{
	GENERATED_USTRUCT_BODY()

	FTextureFormatSettings()
		: CompressionSettings(TC_Default)
		, CompressionNoAlpha(false)
		, CompressionNone(false)
		, CompressionYCoCg(false)
		, SRGB(false)
	{}

	UPROPERTY()
	TEnumAsByte<enum TextureCompressionSettings> CompressionSettings;

	UPROPERTY()
	uint8 CompressionNoAlpha : 1;

	UPROPERTY()
	uint8 CompressionNone : 1;

	UPROPERTY()
	uint8 CompressionYCoCg : 1;

	UPROPERTY()
	uint8 SRGB : 1;
};

UCLASS(abstract, MinimalAPI, BlueprintType)
class UTexture : public UStreamableRenderAsset, public IInterface_AssetUserData
{
	GENERATED_UCLASS_BODY()

	/*--------------------------------------------------------------------------
		Editor only properties used to build the runtime texture data.
	--------------------------------------------------------------------------*/

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FTextureSource Source;
#endif

private:
	/** Unique ID for this material, used for caching during distributed lighting */
	UPROPERTY()
	FGuid LightingGuid;

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FString SourceFilePath_DEPRECATED;

	UPROPERTY(VisibleAnywhere, Instanced, Category=ImportSettings)
	class UAssetImportData* AssetImportData;

public:

	/** Static texture brightness adjustment (scales HSV value.)  (Non-destructive; Requires texture source art to be available.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Adjustments, meta=(DisplayName = "Brightness"))
	float AdjustBrightness;

	/** Static texture curve adjustment (raises HSV value to the specified power.)  (Non-destructive; Requires texture source art to be available.)  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Adjustments, meta=(DisplayName = "Brightness Curve"))
	float AdjustBrightnessCurve;

	/** Static texture "vibrance" adjustment (0 - 1) (HSV saturation algorithm adjustment.)  (Non-destructive; Requires texture source art to be available.)  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Adjustments, meta=(DisplayName = "Vibrance", ClampMin = "0.0", ClampMax = "1.0"))
	float AdjustVibrance;

	/** Static texture saturation adjustment (scales HSV saturation.)  (Non-destructive; Requires texture source art to be available.)  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Adjustments, meta=(DisplayName = "Saturation"))
	float AdjustSaturation;

	/** Static texture RGB curve adjustment (raises linear-space RGB color to the specified power.)  (Non-destructive; Requires texture source art to be available.)  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Adjustments, meta=(DisplayName = "RGBCurve"))
	float AdjustRGBCurve;

	/** Static texture hue adjustment (0 - 360) (offsets HSV hue by value in degrees.)  (Non-destructive; Requires texture source art to be available.)  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Adjustments, meta=(DisplayName = "Hue", ClampMin = "0.0", ClampMax = "360.0"))
	float AdjustHue;

	/** Remaps the alpha to the specified min/max range, defines the new value of 0 (Non-destructive; Requires texture source art to be available.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Adjustments, meta=(DisplayName = "Min Alpha"))
	float AdjustMinAlpha;

	/** Remaps the alpha to the specified min/max range, defines the new value of 1 (Non-destructive; Requires texture source art to be available.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Adjustments, meta=(DisplayName = "Max Alpha"))
	float AdjustMaxAlpha;

	/** If enabled, the texture's alpha channel will be discarded during compression */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Compression, meta=(DisplayName="Compress Without Alpha"))
	uint32 CompressionNoAlpha:1;

	UPROPERTY()
	uint32 CompressionNone:1;

	/** If enabled, defer compression of the texture until save. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Compression, meta=(NoResetToDefault))
	uint32 DeferCompression:1;

	/** How aggressively should any relevant lossy compression be applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Compression, AdvancedDisplay)
	TEnumAsByte<ETextureLossyCompressionAmount> LossyCompressionAmount;

	/** The maximum resolution for generated textures. A value of 0 means the maximum size for the format on each platform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Compression, meta=(DisplayName="Maximum Texture Size", ClampMin = "0.0"), AdvancedDisplay)
	int32 MaxTextureSize;

	/** The compression quality for generated textures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Compression, meta = (DisplayName = "Compression Quality"), AdvancedDisplay)
	TEnumAsByte<enum ETextureCompressionQuality> CompressionQuality;

	/** When true, the alpha channel of mip-maps and the base image are dithered for smooth LOD transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Texture, AdvancedDisplay)
	uint32 bDitherMipMapAlpha:1;

	/** Alpha values per channel to compare to when preserving alpha coverage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Texture, meta=(ClampMin = "0", ClampMax = "1"), AdvancedDisplay)
	FVector4 AlphaCoverageThresholds;

	/** When true the texture's border will be preserved during mipmap generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=LevelOfDetail, AdvancedDisplay)
	uint32 bPreserveBorder:1;

	/** When true the texture's green channel will be inverted. This is useful for some normal maps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Texture, AdvancedDisplay)
	uint32 bFlipGreenChannel:1;

	/** For DXT1 textures, setting this will cause the texture to be twice the size, but better looking, on iPhone */
	UPROPERTY(EditAnywhere, Category = Texture, AdvancedDisplay)
	uint32 bForcePVRTC4:1;

	/** How to pad the texture to a power of 2 size (if necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Texture)
	TEnumAsByte<enum ETexturePowerOfTwoSetting::Type> PowerOfTwoMode;

	/** The color used to pad the texture out if it is resized due to PowerOfTwoMode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Texture)
	FColor PaddingColor;

	/** Whether to chroma key the image, replacing any pixels that match ChromaKeyColor with transparent black */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Adjustments)
	bool bChromaKeyTexture;

	/** The threshold that components have to match for the texel to be considered equal to the ChromaKeyColor when chroma keying (<=, set to 0 to require a perfect exact match) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Adjustments, meta=(EditCondition="bChromaKeyTexture", ClampMin="0"))
	float ChromaKeyThreshold;

	/** The color that will be replaced with transparent black if chroma keying is enabled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Adjustments, meta=(EditCondition="bChromaKeyTexture"))
	FColor ChromaKeyColor;

	/** Per asset specific setting to define the mip-map generation properties like sharpening and kernel size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=LevelOfDetail)
	TEnumAsByte<enum TextureMipGenSettings> MipGenSettings;
	
	/**
	 * Can be defined to modify the roughness based on the normal map variation (mostly from mip maps).
	 * MaxAlpha comes in handy to define a base roughness if no source alpha was there.
	 * Make sure the normal map has at least as many mips as this texture.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Compositing)
	class UTexture* CompositeTexture;

	/* defines how the CompositeTexture is applied, e.g. CTM_RoughnessFromNormalAlpha */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Compositing, AdvancedDisplay)
	TEnumAsByte<enum ECompositeTextureMode> CompositeTextureMode;

	/**
	 * default 1, high values result in a stronger effect e.g 1, 2, 4, 8
	 * this is no slider because the texture update would not be fast enough
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Compositing, AdvancedDisplay)
	float CompositePower;

	/**
	 * Array of settings used to control the format of given layer
	 * If this array doesn't include an entry for a given layer, values from UTexture will be used
	 */
	UPROPERTY()
	TArray<FTextureFormatSettings> LayerFormatSettings;

#endif // WITH_EDITORONLY_DATA

	/*--------------------------------------------------------------------------
		Properties needed at runtime below.
	--------------------------------------------------------------------------*/

	/** A bias to the index of the top mip level to use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=LevelOfDetail, meta=(DisplayName="LOD Bias"), AssetRegistrySearchable)
	int32 LODBias;

	/** Compression settings to use when building the texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Compression, AssetRegistrySearchable)
	TEnumAsByte<enum TextureCompressionSettings> CompressionSettings;

	/** The texture filtering mode to use when sampling this texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Texture, AssetRegistrySearchable, AdvancedDisplay)
	TEnumAsByte<enum TextureFilter> Filter;

	/** The texture mip load options. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Texture, AssetRegistrySearchable, AdvancedDisplay)
	ETextureMipLoadOptions MipLoadOptions;

	/** Texture group this texture belongs to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=LevelOfDetail, meta=(DisplayName="Texture Group"), AssetRegistrySearchable)
	TEnumAsByte<enum TextureGroup> LODGroup;

	/** Downscale source texture, applied only to textures without mips 
	 * 0.0 - use scale value from texture group
	 * 1.0 - do not scale texture
	 * > 1.0 - scale texure
	 */
	UPROPERTY(EditAnywhere, Category=LevelOfDetail, AdvancedDisplay, meta=(ClampMin="0.0", ClampMax="8.0"))
	FPerPlatformFloat Downscale;

	/** Texture downscaling options */
	UPROPERTY(EditAnywhere, Category=LevelOfDetail, AdvancedDisplay)
	ETextureDownscaleOptions DownscaleOptions;

	
	/** This should be unchecked if using alpha channels individually as masks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Texture, meta=(DisplayName="sRGB"), AssetRegistrySearchable)
	uint8 SRGB:1;

#if WITH_EDITORONLY_DATA

	/** A flag for using the simplified legacy gamma space e.g pow(color,1/2.2) for converting from FColor to FLinearColor, if we're doing sRGB. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Texture, AdvancedDisplay)
	uint8 bUseLegacyGamma:1;

#endif // WITH_EDITORONLY_DATA

	/** If true, the RHI texture will be created using TexCreate_NoTiling */
	UPROPERTY()
	uint8 bNoTiling:1;

	/** Is this texture streamed in using VT								*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Texture, AssetRegistrySearchable, AdvancedDisplay)
	uint8 VirtualTextureStreaming : 1;

	/** If true the texture stores YCoCg. Blue channel will be filled with a precision scale during compression. */
	UPROPERTY()
	uint8 CompressionYCoCg : 1;

	/** If true, the RHI texture will be created without TexCreate_OfflineProcessed. */
	UPROPERTY(transient)
	uint8 bNotOfflineProcessed : 1;

private:
	/** Whether the async resource release process has already been kicked off or not */
	UPROPERTY(transient)
	uint8 bAsyncResourceReleaseHasBeenStarted : 1;

protected:
	/** Array of user data stored with the asset */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Instanced, Category = Texture)
	TArray<UAssetUserData*> AssetUserData;

private:
	/** The texture's resource, can be NULL */
	class FTextureResource* PrivateResource;
	/** Value updated and returned by the render-thread to allow
	  * fenceless update from the game-thread without causing
	  * potential crash in the render thread.
	  */
	class FTextureResource* PrivateResourceRenderThread;

public:
	TFieldPtrAccessor<FTextureResource> Resource;

	/** Set texture's resource, can be NULL */
	ENGINE_API void SetResource(FTextureResource* Resource);

	/** Get the texture's resource, can be NULL */
	ENGINE_API FTextureResource* GetResource();

	/** Get the const texture's resource, can be NULL */
	ENGINE_API const FTextureResource* GetResource() const;

	/** Stable RHI texture reference that refers to the current RHI texture. Note this is manually refcounted! */
	FTextureReference TextureReference;

	/** Release fence to know when resources have been freed on the rendering thread. */
	FRenderCommandFence ReleaseFence;

	/** delegate type for texture save events ( Params: UTexture* TextureToSave ) */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnTextureSaved, class UTexture*);
	/** triggered before a texture is being saved */
	ENGINE_API static FOnTextureSaved PreSaveEvent;

	/**
	 * Resets the resource for the texture.
	 */
	ENGINE_API void ReleaseResource();

	/**
	 * Creates a new resource for the texture, and updates any cached references to the resource.
	 */
	ENGINE_API virtual void UpdateResource();

	/**
	 * Implemented by subclasses to create a new resource for the texture.
	 */
	virtual class FTextureResource* CreateResource() PURE_VIRTUAL(UTexture::CreateResource,return NULL;);

	/** Cache the combined LOD bias based on texture LOD group and LOD bias. */
	ENGINE_API void UpdateCachedLODBias();

	/**
	 * @return The material value type of this texture.
	 */
	virtual EMaterialValueType GetMaterialType() const PURE_VIRTUAL(UTexture::GetMaterialType,return MCT_Texture;);

	/**
	 * Returns if the texture is actually being rendered using virtual texturing right now.
	 * Unlike the 'VirtualTextureStreaming' property which reflects the user's desired state
	 * this reflects the actual current state on the renderer depending on the platform, VT
	 * data being built, project settings, ....
	 */
	virtual bool IsCurrentlyVirtualTextured() const
	{
		return false;
	}

	/** Returns the virtual texture build settings. */
	ENGINE_API virtual void GetVirtualTextureBuildSettings(struct FVirtualTextureBuildSettings& OutSettings) const;

	/**
	 * Textures that use the derived data cache must override this function and
	 * provide a pointer to the linked list of platform data.
	 */
	virtual FTexturePlatformData** GetRunningPlatformData() { return NULL; }
	virtual TMap<FString, FTexturePlatformData*>* GetCookedPlatformData() { return NULL; }

	void CleanupCachedRunningPlatformData();

	/**
	 * Serializes cooked platform data.
	 */
	ENGINE_API void SerializeCookedPlatformData(class FArchive& Ar);

	//~ Begin IInterface_AssetUserData Interface
	ENGINE_API virtual void AddAssetUserData(UAssetUserData* InUserData) override;
	ENGINE_API virtual void RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	ENGINE_API virtual UAssetUserData* GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	ENGINE_API virtual const TArray<UAssetUserData*>* GetAssetUserDataArray() const override;
	//~ End IInterface_AssetUserData Interface

#if WITH_EDITOR
	/**
	 * Caches platform data for the texture.
	 * 
	 * @param bAsyncCache spawn a thread to cache the platform data 
	 * @param bAllowAsyncBuild allow building the DDC file in the thread if missing.
	 * @param bAllowAsyncLoading allow loading source data in the thread if missing (the data won't be reusable for later use though)
	 * @param Compressor optional compressor as the texture compressor can not be get from an async thread.
	 */
	void CachePlatformData(bool bAsyncCache = false, bool bAllowAsyncBuild = false, bool bAllowAsyncLoading = false, class ITextureCompressorModule* Compressor = nullptr);

	/**
	 * Begins caching platform data in the background for the platform requested
	 */
	ENGINE_API virtual void BeginCacheForCookedPlatformData(  const ITargetPlatform* TargetPlatform ) override;

	/**
	 * Have we finished loading all the cooked platform data for the target platforms requested in BeginCacheForCookedPlatformData
	 * 
	 * @param	TargetPlatform target platform to check for cooked platform data
	 */
	ENGINE_API virtual bool IsCachedCookedPlatformDataLoaded( const ITargetPlatform* TargetPlatform ) override;


	/**
	 * Clears cached cooked platform data for specific platform
	 * 
	 * @param	TargetPlatform	target platform to cache platform specific data for
	 */
	ENGINE_API virtual void ClearCachedCookedPlatformData( const ITargetPlatform* TargetPlatform ) override;

	/**
	 * Clear all cached cooked platform data
	 * 
	 * @param	TargetPlatform	target platform to cache platform specific data for
	 */
	ENGINE_API virtual void ClearAllCachedCookedPlatformData() override;


	/**
	 * Begins caching platform data in the background.
	 */
	ENGINE_API void BeginCachePlatformData();

	/**
	 * Returns true if all async caching has completed.
	 */
	ENGINE_API bool IsAsyncCacheComplete();

	/**
	 * Blocks on async cache tasks and prepares platform data for use.
	 */
	ENGINE_API void FinishCachePlatformData();

	/**
	 * Forces platform data to be rebuilt.
	 */
	ENGINE_API void ForceRebuildPlatformData();

	/**
	 * Marks platform data as transient. This optionally removes persistent or cached data associated with the platform.
	 */
	ENGINE_API void MarkPlatformDataTransient();

	/**
	* Return maximum dimension for this texture type.
	*/
	ENGINE_API virtual uint32 GetMaximumDimension() const;

	/**
	 * Gets settings used to choose format for the given layer
	 */
	ENGINE_API void GetLayerFormatSettings(int32 LayerIndex, FTextureFormatSettings& OutSettings) const;
	ENGINE_API void SetLayerFormatSettings(int32 LayerIndex, const FTextureFormatSettings& InSettings);

	ENGINE_API void GetDefaultFormatSettings(FTextureFormatSettings& OutSettings) const;
#endif

	/** @return the width of the surface represented by the texture. */
	virtual float GetSurfaceWidth() const PURE_VIRTUAL(UTexture::GetSurfaceWidth,return 0;);

	/** @return the height of the surface represented by the texture. */
	virtual float GetSurfaceHeight() const PURE_VIRTUAL(UTexture::GetSurfaceHeight,return 0;);

	/**
	 * Access the GUID which defines this texture's resources externally through FExternalTextureRegistry
	 */
	virtual FGuid GetExternalTextureGuid() const
	{
		return FGuid();
	}

	//~ Begin UObject Interface.
#if WITH_EDITOR
	ENGINE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	ENGINE_API virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif // WITH_EDITOR
	ENGINE_API virtual void Serialize(FArchive& Ar) override;
	ENGINE_API virtual void PostInitProperties() override;
	ENGINE_API virtual void PostLoad() override;
	ENGINE_API virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	ENGINE_API virtual void BeginDestroy() override;
	ENGINE_API virtual bool IsReadyForFinishDestroy() override;
	ENGINE_API virtual void FinishDestroy() override;
	ENGINE_API virtual void PostCDOContruct() override;
#if WITH_EDITORONLY_DATA
	ENGINE_API virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
#endif
	ENGINE_API virtual bool IsPostLoadThreadSafe() const override;
	//~ End UObject Interface.

	//~ Begin UStreamableRenderAsset Interface
	virtual int32 GetLODGroupForStreaming() const final override { return static_cast<int32>(LODGroup); }
	virtual EStreamableRenderAssetType GetRenderAssetType() const final override { return EStreamableRenderAssetType::Texture; }
	ENGINE_API virtual FIoFilenameHash GetMipIoFilenameHash(const int32 MipIndex) const final override;
	ENGINE_API virtual bool DoesMipDataExist(const int32 MipIndex) const final override;
	ENGINE_API virtual bool HasPendingRenderResourceInitialization() const final override;
	ENGINE_API virtual bool HasPendingLODTransition() const final override;
	ENGINE_API virtual void InvalidateLastRenderTimeForStreaming() final override;
	ENGINE_API virtual float GetLastRenderTimeForStreaming() const final override;
	ENGINE_API virtual bool ShouldMipLevelsBeForcedResident() const final override;
	//~ End UStreamableRenderAsset Interface

	/**
	 *	Gets the average brightness of the texture (in linear space)
	 *
	 *	@param	bIgnoreTrueBlack		If true, then pixels w/ 0,0,0 rgb values do not contribute.
	 *	@param	bUseGrayscale			If true, use gray scale else use the max color component.
	 *
	 *	@return	float					The average brightness of the texture
	 */
	ENGINE_API virtual float GetAverageBrightness(bool bIgnoreTrueBlack, bool bUseGrayscale);
	
	// @todo document
	ENGINE_API static const TCHAR* GetTextureGroupString(TextureGroup InGroup);

	// @todo document
	ENGINE_API static const TCHAR* GetMipGenSettingsString(TextureMipGenSettings InEnum);

	// @param	bTextureGroup	true=TexturGroup, false=Texture otherwise
	ENGINE_API static TextureMipGenSettings GetMipGenSettingsFromString(const TCHAR* InStr, bool bTextureGroup);

	/**
	 * Forces textures to recompute LOD settings and stream as needed.
	 * @returns true if the settings were applied, false if they couldn't be applied immediately.
	 */
	ENGINE_API static bool ForceUpdateTextureStreaming();

	/**
	 * Checks whether this texture has a high dynamic range (HDR) source.
	 *
	 * @return true if the texture has an HDR source, false otherwise.
	 */
	bool HasHDRSource(int32 LayerIndex = 0) const
	{
#if WITH_EDITOR
		return FTextureSource::IsHDR(Source.GetFormat(LayerIndex));
#else
		return false;
#endif // WITH_EDITOR
	}


	/** @return true if the compression type is a normal map compression type */
	bool IsNormalMap() const
	{
		return (CompressionSettings == TC_Normalmap);
	}

	/**
	 * Calculates the size of this texture if it had MipCount miplevels streamed in.
	 *
	 * @param	Enum	Which mips to calculate size for.
	 * @return	Total size of all specified mips, in bytes
	 */
	virtual uint32 CalcTextureMemorySizeEnum( ETextureMipCount Enum ) const
	{
		return 0;
	}

	/** Returns a unique identifier for this texture. Used by the lighting build and texture streamer. */
	const FGuid& GetLightingGuid() const
	{
		return LightingGuid;
	}

	/** 
	 * Assigns a new GUID to a texture. This will be called whenever a texture is created or changes. 
	 * In game, the GUIDs are only used by the texture streamer to link build data to actual textures,
	 * that means new textures don't actually need GUIDs (see FStreamingTextureLevelContext)
	 */
	void SetLightingGuid()
	{
#if WITH_EDITORONLY_DATA
		LightingGuid = FGuid::NewGuid();
#else
		LightingGuid = FGuid(0, 0, 0, 0);
#endif // WITH_EDITORONLY_DATA
	}

	void SetLightingGuid(const FGuid& Guid)
	{
		LightingGuid = Guid;
	}

	/** Generates a deterministic GUID for the texture based on the full name of the object.
	  * Used to ensure that assets created during cook can be deterministic
	  */
	ENGINE_API void SetDeterministicLightingGuid();

	/**
	 * Retrieves the pixel format enum for enum <-> string conversions.
	 */
	ENGINE_API static class UEnum* GetPixelFormatEnum();

	/** Returns the minimum number of mips that must be resident in memory (cannot be streamed). */
	static FORCEINLINE int32 GetStaticMinTextureResidentMipCount()
	{
		return GMinTextureResidentMipCount;
	}

	/** Sets the minimum number of mips that must be resident in memory (cannot be streamed). */
	static void SetMinTextureResidentMipCount(int32 InMinTextureResidentMipCount);

protected:

	/** The minimum number of mips that must be resident in memory (cannot be streamed). */
	static ENGINE_API int32 GMinTextureResidentMipCount;

#if WITH_EDITOR

	enum class ENotifyMaterialsEffectOnShaders
	{
		Default,
		DoesNotInvalidate
	};

	/** Notify any loaded material instances that the texture has changed. */
	ENGINE_API void NotifyMaterials(const ENotifyMaterialsEffectOnShaders EffectOnShaders = ENotifyMaterialsEffectOnShaders::Default);

#endif //WITH_EDITOR

	void BeginFinalReleaseResource();

	/**
	 * Calculates the render resource initial state, expected to be used in InitResource() for derived classes implementing streaming.
	 *
	 * @param	PlatformData - the asset platform data.
	 * @param	bAllowStreaming - where streaming is allowed, might still be disabled based on asset settings.
	 * @param	MaxMipCount - optional limitation on the max mip count.
	 * @return  The state to be passed to FStreamableTextureResource.
	 */
	FStreamableRenderResourceState GetResourcePostInitState(FTexturePlatformData* PlatformData, bool bAllowStreaming, int32 MinRequestMipCount = 0, int32 MaxMipCount = 0) const;
};

/** 
* Replaces the RHI reference of one texture with another.
* Allows one texture to be replaced with another at runtime and have all existing references to it remain valid.
*/
struct FTextureReferenceReplacer
{
	FTextureReferenceRHIRef OriginalRef;

	FTextureReferenceReplacer(UTexture* OriginalTexture)
	{
		if (OriginalTexture)
		{
			OriginalTexture->ReleaseResource();
			OriginalRef = OriginalTexture->TextureReference.TextureReferenceRHI;
		}
		else
		{
			OriginalRef = nullptr;
		}
	}

	void Replace(UTexture* NewTexture)
	{
		if (OriginalRef)
		{
			NewTexture->TextureReference.TextureReferenceRHI = OriginalRef;
		}
	}
};
