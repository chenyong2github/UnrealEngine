// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeightfieldMinMaxTexture.h"

#include "Engine/Texture2D.h"
#include "HeightfieldMinMaxTextureNotify.h"
#include "RenderUtils.h"

UHeightfieldMinMaxTexture::UHeightfieldMinMaxTexture(const FObjectInitializer& ObjectInitializer)
	: UObject(ObjectInitializer)
	, Texture(nullptr)
	, MaxCPULevels(5)
{
}

#if WITH_EDITOR

void UHeightfieldMinMaxTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	static FName MaxCPULevelsName = GET_MEMBER_NAME_CHECKED(UHeightfieldMinMaxTexture, MaxCPULevels);
	if (PropertyChangedEvent.Property && (PropertyChangedEvent.Property->GetFName() == MaxCPULevelsName))
	{
		RebuildCPUTextureData();

		VirtualHeightfieldMesh::NotifyComponents(this);
	}
}

void UHeightfieldMinMaxTexture::BuildTexture(FHeightfieldMinMaxTextureBuildDesc const& InBuildDesc)
{
	// Build GPU Texture.
	Texture = NewObject<UTexture2D>(this, TEXT("Texture"));

	FTextureFormatSettings Settings;
	Settings.CompressionSettings = TC_EditorIcon;
	Settings.CompressionNone = true;
	Settings.SRGB = false;

	Texture->Filter = TF_Nearest;
	Texture->MipGenSettings = TMGS_LeaveExistingMips;
	Texture->MipLoadOptions = ETextureMipLoadOptions::AllMips;
	Texture->NeverStream = true;
	Texture->SetLayerFormatSettings(0, Settings);
	Texture->Source.Init(InBuildDesc.SizeX, InBuildDesc.SizeY, 1, InBuildDesc.NumMips, TSF_BGRA8, InBuildDesc.Data);

	Texture->PostEditChange();
	
	// Build CPU TextureData.
	RebuildCPUTextureData();

	// Notify all dependent components.
	VirtualHeightfieldMesh::NotifyComponents(this);
}

void UHeightfieldMinMaxTexture::RebuildCPUTextureData()
{
	TextureData.Reset();
	TextureDataMips.Reset();

	if (Texture != nullptr && Texture->Source.IsValid() && MaxCPULevels > 0)
	{
		const int32 NumTextureMips = Texture->Source.GetNumMips();
		const int32 NumCPUMips = FMath::Min(NumTextureMips, MaxCPULevels);
		const int32 BaseMipIndex = NumTextureMips - NumCPUMips;

		const int32 TextureSizeX = Texture->Source.GetSizeX();
		const int32 TextureSizeY = Texture->Source.GetSizeY();
		TextureDataSize.X = FMath::Max(TextureSizeX >> BaseMipIndex, 1);
		TextureDataSize.Y = FMath::Max(TextureSizeY >> BaseMipIndex, 1);

		// Reserve the expected entries assuming square mips. This may be an overestimate.
		TextureData.Reserve(((1 << (2 * NumCPUMips)) - 1) / 3);
		TextureDataMips.Reserve(NumCPUMips);

		// Iterate the Texture mips and extract min/max values to store in a flat array.
		for (int32 MipIndex = BaseMipIndex; MipIndex < NumTextureMips; ++MipIndex)
		{
			TextureDataMips.Add(TextureData.Num());

			TArray64<uint8> MipData;
			if (Texture->Source.GetMipData(MipData, MipIndex))
			{
				for (int32 Index = 0; Index < MipData.Num(); Index += 4)
				{
					float Min = (float)(MipData[Index + 2] * 256 + MipData[Index + 3]) / 65535.f;
					float Max = (float)(MipData[Index + 0] * 256 + MipData[Index + 1]) / 65535.f;
					TextureData.Add(FVector2D(Min, Max));
				}
			}
		}

		TextureData.Shrink();
	}
}

#endif
