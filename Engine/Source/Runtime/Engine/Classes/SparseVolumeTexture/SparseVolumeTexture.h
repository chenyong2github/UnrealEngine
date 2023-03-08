// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/VolumeTexture.h"

#include "Serialization/EditorBulkData.h"
#include "Containers/Array.h"
#include "UnrealClient.h"
#include "UObject/ObjectSaveContext.h"
#include "Engine/TextureDefines.h"

#include "SparseVolumeTexture.generated.h"

namespace UE { namespace Shader	{ enum class EValueType : uint8; } }
namespace UE { namespace DerivedData { class FRequestOwner; } }

#define SPARSE_VOLUME_TILE_RES 16
#define SPARSE_VOLUME_TILE_BORDER 1
#define SPARSE_VOLUME_TILE_RES_PADDED (SPARSE_VOLUME_TILE_RES + 2 * SPARSE_VOLUME_TILE_BORDER)

struct FSparseVolumeTextureData;
class FSparseVolumeTextureSceneProxy;

struct ENGINE_API FSparseVolumeTextureHeader
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
	TStaticArray<FVector4f, 2> NullTileValues = TStaticArray<FVector4f, 2>(InPlace, FVector4f());

	void Serialize(FArchive& Ar);
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
	bool BuildDerivedData(const FIntVector3& VolumeResolution, TextureAddress AddressX, TextureAddress AddressY, TextureAddress AddressZ, FSparseVolumeTextureData* OutMippedTextureData);
	void Serialize(FArchive& Ar, class UStreamableSparseVolumeTexture* Owner, int32 FrameIndex);
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
	virtual TextureAddress GetTextureAddressX() const { return TA_Wrap; }
	virtual TextureAddress GetTextureAddressY() const { return TA_Wrap; }
	virtual TextureAddress GetTextureAddressZ() const { return TA_Wrap; }

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

	UPROPERTY(VisibleAnywhere, Category = "Texture")
	FIntVector VolumeResolution;

	UPROPERTY(VisibleAnywhere, Category = "Texture")
	int32 NumMipLevels;

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
	virtual TextureAddress GetTextureAddressX() const override { return AddressX; }
	virtual TextureAddress GetTextureAddressY() const override { return AddressY; }
	virtual TextureAddress GetTextureAddressZ() const override { return AddressZ; }
	virtual const FSparseVolumeTextureSceneProxy* GetSparseVolumeTextureSceneProxy() const override { return GetStreamedFrameProxyOrFallback(0 /*FrameIndex*/, 0 /*MipLevel*/); };
	//~ End USparseVolumeTexture Interface.

	const FSparseVolumeTextureSceneProxy* GetStreamedFrameProxyOrFallback(int32 FrameIndex, int32 MipLevel) const;
	TArrayView<const FSparseVolumeTextureFrame> GetFrames() const;

protected:

	TArray<FSparseVolumeTextureFrame> Frames;

#if WITH_EDITOR
	enum class ENotifyMaterialsEffectOnShaders
	{
		Default,
		DoesNotInvalidate
	};

	/** Notify any loaded material instances that the texture has changed. */
	void NotifyMaterials(const ENotifyMaterialsEffectOnShaders EffectOnShaders = ENotifyMaterialsEffectOnShaders::Default);
#endif // WITH_EDITOR

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

	void Initialize(const FSparseVolumeTextureSceneProxy* InSceneProxy, const FIntVector& InVolumeResolution, TextureAddress InAddressX, TextureAddress InAddressY, TextureAddress InAddressZ);

	//~ Begin USparseVolumeTexture Interface.
	virtual int32 GetNumFrames() const override { return 1; }
	virtual int32 GetNumMipLevels() const override { return 1; }
	virtual FIntVector GetVolumeResolution() const override { return VolumeResolution; };
	virtual TextureAddress GetTextureAddressX() const override { return AddressX; }
	virtual TextureAddress GetTextureAddressY() const override { return AddressY; }
	virtual TextureAddress GetTextureAddressZ() const override { return AddressZ; }
	virtual const FSparseVolumeTextureSceneProxy* GetSparseVolumeTextureSceneProxy() const override { return SceneProxy; };
	//~ End USparseVolumeTexture Interface.

private:
	FIntVector3 VolumeResolution;
	TEnumAsByte<enum TextureAddress> AddressX;
	TEnumAsByte<enum TextureAddress> AddressY;
	TEnumAsByte<enum TextureAddress> AddressZ;
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
