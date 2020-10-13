// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "Image/ImageDimensions.h"
#include "Image/ImageBuilder.h"
#include "Engine/Classes/Engine/Texture2D.h"


/**
 * Texture2DBuilder is a utility class for creating/modifying various types of UTexture2D.
 * Use Initialize() functions to configure, can either generate a new UTexture2D (in the Transient package) or modify an existing UTexture2D.
 *
 * Currently the generated UTexture2D will only have Mip 0, and only Mip 0 can be edited.
 * The generated UTexture2D has format PF_B8G8R8A8.
 *
 * Use Commit() to lock and update the texture after editing is complete. LockForEditing() can be used to re-open.
 * By default textures are locked for editing on Initialize()
 *
 * If you have generated a UTexture2D by other means, you can use the static function ::CopyPlatformDataToSourceData() to populate the 
 * Source data from the PlatformData, which is required to save it as a UAsset. 
 */
class FTexture2DBuilder
{
public:

	/** Supported texture types */
	enum class ETextureType
	{
		Color,
		NormalMap,
		AmbientOcclusion
	};

protected:
	FImageDimensions Dimensions;
	ETextureType BuildType = ETextureType::Color;

	UTexture2D* RawTexture2D = nullptr;
	FColor* CurrentMipData = nullptr;

public:

	virtual ~FTexture2DBuilder()
	{
		check(IsEditable() == false);
	}

	const FImageDimensions& GetDimensions() const
	{
		return Dimensions;
	}

	const ETextureType GetTextureType() const
	{
		return BuildType;
	}

	/** @return the internal texture */
	UTexture2D* GetTexture2D() const
	{
		return RawTexture2D;
	}


	/**
	 * Create a new UTexture2D configured with the given BuildType and Dimensions
	 */
	bool Initialize(ETextureType BuildTypeIn, FImageDimensions DimensionsIn)
	{
		check(DimensionsIn.IsSquare());
		BuildType = BuildTypeIn;
		Dimensions = DimensionsIn;

		// create new texture
		RawTexture2D = UTexture2D::CreateTransient((int32)Dimensions.GetWidth(), (int32)Dimensions.GetHeight(), PF_B8G8R8A8);
		if (RawTexture2D == nullptr)
		{
			return false;
		}

		if (BuildType == ETextureType::NormalMap)
		{
			RawTexture2D->CompressionSettings = TC_Normalmap;
			RawTexture2D->SRGB = false;
			RawTexture2D->LODGroup = TEXTUREGROUP_WorldNormalMap;
			//RawTexture2D->bFlipGreenChannel = true;
#if WITH_EDITOR
			RawTexture2D->MipGenSettings = TMGS_NoMipmaps;
#endif
			RawTexture2D->UpdateResource();
		}

		// lock
		if (LockForEditing() == false)
		{
			return false;
		}

		if (IsEditable())
		{
			Clear();
		}

		return true;
	}


	/**
	 * Initialize the builder with an existing UTexture2D
	 */
	bool Initialize(UTexture2D* ExistingTexture, ETextureType BuildTypeIn, bool bLockForEditing = true)
	{
		if (! ensure(ExistingTexture != nullptr)) return false;
		if (! ensure(ExistingTexture->PlatformData != nullptr)) return false;
		if (! ensure(ExistingTexture->PlatformData->Mips.Num() > 0)) return false;

		int32 Width = ExistingTexture->PlatformData->Mips[0].SizeX;
		int32 Height = ExistingTexture->PlatformData->Mips[0].SizeY;
		Dimensions = FImageDimensions(Width, Height);
		BuildType = BuildTypeIn;
		RawTexture2D = ExistingTexture;

		// lock for editing
		if (bLockForEditing && LockForEditing() == false)
		{
			return false;
		}

		return true;
	}


	/**
	 * Lock the Mip 0 buffer for editing
	 * @return true if lock was successfull. Will return false if already locked.
	 */
	bool LockForEditing()
	{
		check(RawTexture2D);
		check(CurrentMipData == nullptr);
		if (RawTexture2D && CurrentMipData == nullptr)
		{
			CurrentMipData = reinterpret_cast<FColor*>(RawTexture2D->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
			check(CurrentMipData);
		}
		return IsEditable();
	}

	/** @return true if the texture data is currently locked and editable */
	bool IsEditable() const
	{
		return CurrentMipData != nullptr;
	}


	/**
	 * Unlock the Mip 0 buffer and update the texture rendering data.
	 * This does not PostEditChange() so any materials using this texture may not be updated, the caller must do that.
	 * @param bUpdateSourceData if true, UpdateSourceData() is called to copy the current PlatformData to the SourceData
	 */
	void Commit(bool bUpdateSourceData = true)
	{
		check(RawTexture2D);
		check(IsEditable());
		if (RawTexture2D)
		{
			if (bUpdateSourceData)
			{
				UpdateSourceData();
			}

			RawTexture2D->PlatformData->Mips[0].BulkData.Unlock();
			RawTexture2D->UpdateResource();

			CurrentMipData = nullptr;
		}
	}


	/**
	 * Copy the current PlatformData to the UTexture2D Source Data.
	 * This does not require the texture to be locked for editing, if it is not locked, a read-only lock will be acquired as needed
	 * @warning currently assumes both buffers are BGRA
	 */
	void UpdateSourceData()
	{
		// source data only exists in Editor
#if WITH_EDITOR
		check(RawTexture2D);

		bool bIsEditable = IsEditable();
		const FColor* SourceMipData = nullptr;
		if (bIsEditable)
		{
			SourceMipData = CurrentMipData;
		}
		else
		{
			SourceMipData = reinterpret_cast<const FColor*>(RawTexture2D->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_ONLY));
		}

		bool bIsValid = RawTexture2D->Source.IsValid();

		RawTexture2D->Source.Init2DWithMipChain(Dimensions.GetWidth(), Dimensions.GetHeight(), TSF_BGRA8);

		uint8* DestData = RawTexture2D->Source.LockMip(0);
		FMemory::Memcpy(DestData, SourceMipData, Dimensions.GetWidth() * Dimensions.GetHeight() * 4 * sizeof(uint8));
		RawTexture2D->Source.UnlockMip(0);

		if (bIsEditable == false)
		{
			RawTexture2D->PlatformData->Mips[0].BulkData.Unlock();
		}
#endif
	}


	void Cancel()
	{
		bool bIsEditable = IsEditable();
		if (bIsEditable)
		{
			RawTexture2D->PlatformData->Mips[0].BulkData.Unlock();
			CurrentMipData = nullptr;
		}
	}


	/**
	 * Clear all texels in the current Mip to the clear/default color for the texture build type
	 */
	void Clear()
	{
		check(IsEditable());
		if (IsEditable())
		{
			FColor ClearColor = GetClearColor();
			for (int64 k = 0; k < Dimensions.Num(); ++k)
			{
				CurrentMipData[k] = ClearColor;
			}
		}
	}


	/**
	 * Clear all texels in the current Mip to the given ClearColor
	 */
	void Clear(const FColor& ClearColor)
	{
		check(IsEditable());
		if (IsEditable())
		{
			for (int64 k = 0; k < Dimensions.Num(); ++k)
			{
				CurrentMipData[k] = ClearColor;
			}
		}
	}



	/**
	 * Get the texel at the given X/Y coordinates
	 */
	const FColor& GetTexel(const FVector2i& ImageCoords) const
	{
		checkSlow(IsEditable());
		int64 UseIndex = Dimensions.GetIndex(ImageCoords);
		return CurrentMipData[UseIndex];
	}


	/**
	 * Get the texel at the given linear index
	 */
	const FColor& GetTexel(int64 LinearIndex) const
	{
		checkSlow(IsEditable());
		return CurrentMipData[LinearIndex];
	}


	/**
	 * Set the texel at the given X/Y coordinates to the given FColor
	 */
	void SetTexel(const FVector2i& ImageCoords, const FColor& NewValue)
	{
		checkSlow(IsEditable());
		//int64 UseIndex = Dimensions.GetIndexMirrored(ImageCoords, false, false);
		int64 UseIndex = Dimensions.GetIndex(ImageCoords);
		CurrentMipData[UseIndex] = NewValue;
	}


	/**
	 * Set the texel at the given linear index to the given FColor
	 */
	void SetTexel(int64 LinearIndex, const FColor& NewValue)
	{
		checkSlow(IsEditable());
		SetTexel(Dimensions.GetCoords(LinearIndex), NewValue);
	}


	/**
	 * Set the texel at the given linear index to the clear/default color
	 */
	void ClearTexel(int64 LinearIndex)
	{
		checkSlow(IsEditable());
		SetTexel(Dimensions.GetCoords(LinearIndex), GetClearColor());
	}


	/**
	 * Copy texel value from one linear index to another
	 */
	void CopyTexel(int64 FromLinearIndex, int64 ToLinearIndex)
	{
		checkSlow(IsEditable());
		CurrentMipData[ToLinearIndex] = CurrentMipData[FromLinearIndex];
	}


	/**
	 * populate texel values from floating-point SourceImage
	 */
	bool Copy(const TImageBuilder<FVector3f>& SourceImage, const bool bSRGB = false)
	{
		if (ensure(SourceImage.GetDimensions() == Dimensions) == false)
		{
			return false;
		}
		int64 Num = Dimensions.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			FVector3f Pixel = SourceImage.GetPixel(i);
			Pixel.X = FMathf::Clamp(Pixel.X, 0.0, 1.0);
			Pixel.Y = FMathf::Clamp(Pixel.Y, 0.0, 1.0);
			Pixel.Z = FMathf::Clamp(Pixel.Z, 0.0, 1.0);
			FColor Texel = ((FLinearColor)Pixel).ToFColor(bSRGB);
			SetTexel(i, Texel);
		}
		return true;
	}

	/**
	 * populate texel values from floating-point SourceImage
	 */
	bool Copy(const TImageBuilder<FVector4f>& SourceImage, const bool bSRGB = false)
	{
		if (ensure(SourceImage.GetDimensions() == Dimensions) == false)
		{
			return false;
		}
		int64 Num = Dimensions.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			FVector4f Pixel = SourceImage.GetPixel(i);
			Pixel.X = FMathf::Clamp(Pixel.X, 0.0, 1.0);
			Pixel.Y = FMathf::Clamp(Pixel.Y, 0.0, 1.0);
			Pixel.Z = FMathf::Clamp(Pixel.Z, 0.0, 1.0);
			Pixel.W = FMathf::Clamp(Pixel.W, 0.0, 1.0);
			FColor Texel = ((FLinearColor)Pixel).ToFColor(bSRGB);
			SetTexel(i, Texel);
		}
		return true;
	}


	/**
	 * copy existing texel values to floating-point DestImage
	 */
	bool CopyTo(TImageBuilder<FVector4f>& DestImage) const
	{
		if (ensure(DestImage.GetDimensions() == Dimensions) == false)
		{
			return false;
		}
		int64 Num = Dimensions.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			FColor ByteColor = GetTexel(i);
			FLinearColor FloatColor(ByteColor);
			DestImage.SetPixel(i, FVector4f(FloatColor));
		}
		return true;
	}



	/**
	 * @return current locked Mip data. Nullptr if IsEditable() == false.
	 * @warning this point is invalid after the texture is Committed!
	 */
	const FColor* GetRawTexelBufferUnsafe() const
	{
		return CurrentMipData;
	}


	/**
	 * @return current locked Mip data. Nullptr if IsEditable() == false.
	 * @warning this point is invalid after the texture is Committed!
	 */
	FColor* GetRawTexelBufferUnsafe()
	{
		return CurrentMipData;
	}


	/**
	 * @return the default color for the current texture build type
	 */
	const FColor& GetClearColor() const
	{
		static const FColor DefaultColor = FColor::Black;
		static const FColor DefaultNormalColor(128, 128, 255);
		static const FColor DefaultAOColor = FColor::White;

		switch (BuildType)
		{
		default:
		case ETextureType::Color:
			return DefaultColor;
		case ETextureType::NormalMap:
			return DefaultNormalColor;
		case ETextureType::AmbientOcclusion:
			return DefaultAOColor;
		}
	}


	/**
	 * Use a FTexture2DBuilder to copy the PlatformData to the UTexture2D Source data, so it can be saved as an Asset
	 */
	static bool CopyPlatformDataToSourceData(UTexture2D* Texture, ETextureType TextureType)
	{
		FTexture2DBuilder Builder;
		bool bOK = Builder.Initialize(Texture, TextureType, false);
		if (bOK)
		{
			Builder.UpdateSourceData();
		}
		return bOK;
	}

};
