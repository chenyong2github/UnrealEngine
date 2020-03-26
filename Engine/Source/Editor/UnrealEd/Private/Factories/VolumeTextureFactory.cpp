// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/VolumeTextureFactory.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Engine/VolumeTexture.h"

#define LOCTEXT_NAMESPACE "VolumeTextureFactory"

UVolumeTextureFactory::UVolumeTextureFactory( const FObjectInitializer& ObjectInitializer )
 : Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UVolumeTexture::StaticClass();
}

FText UVolumeTextureFactory::GetDisplayName() const
{
	return LOCTEXT("VolumeTextureFactoryDescription", "Volume Texture");
}

bool UVolumeTextureFactory::ConfigureProperties()
{
	return true;
}

UObject* UVolumeTextureFactory::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	UVolumeTexture* NewVolumeTexture = NewObject<UVolumeTexture>(InParent, Name, Flags);

	if (InitialTexture)
	{
		NewVolumeTexture->SRGB = InitialTexture->SRGB;
		NewVolumeTexture->MipGenSettings = TMGS_FromTextureGroup;
		NewVolumeTexture->NeverStream = true;
		NewVolumeTexture->CompressionNone = false;
		NewVolumeTexture->Source2DTexture = InitialTexture;
		NewVolumeTexture->SetDefaultSource2DTileSize();
		NewVolumeTexture->UpdateSourceFromSourceTexture();
		NewVolumeTexture->UpdateResource();
	}

	return NewVolumeTexture;
}

#undef LOCTEXT_NAMESPACE
