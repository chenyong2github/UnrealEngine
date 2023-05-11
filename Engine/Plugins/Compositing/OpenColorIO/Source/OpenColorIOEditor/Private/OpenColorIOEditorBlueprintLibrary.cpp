// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIOEditorBlueprintLibrary.h"

#include "Modules/ModuleManager.h"

#include "Engine/Texture2D.h"
#include "OpenColorIOEditorModule.h"
#include "OpenColorIOColorSpace.h"
#include "OpenColorIOConfiguration.h"
#include "TextureCompiler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OpenColorIOEditorBlueprintLibrary)


void UOpenColorIOEditorBlueprintLibrary::SetActiveViewportConfiguration(const FOpenColorIODisplayConfiguration& InConfiguration)
{
	FOpenColorIOEditorModule& OpenColorIOEditorModule = FModuleManager::LoadModuleChecked<FOpenColorIOEditorModule>("OpenColorIOEditor");

	OpenColorIOEditorModule.SetActiveViewportConfiguration(InConfiguration);
}

bool UOpenColorIOEditorBlueprintLibrary::ApplyColorSpaceTransformToTexture(const FOpenColorIOColorConversionSettings& ConversionSettings, UTexture* InOutTexture, bool bSynchronous)
{	
	if (IsValid(InOutTexture) && IsValid(ConversionSettings.ConfigurationSource))
	{
		FImage ImageMip0;
		if (InOutTexture->Source.GetMipImage(ImageMip0, 0))
		{
			bool bTransformSucceeded = ConversionSettings.ConfigurationSource->EditorTransformImage(ConversionSettings, ImageMip0);
			if (bTransformSucceeded)
			{
				void* TargetData = InOutTexture->Source.LockMip(0);
				FMemory::Memcpy(TargetData, ImageMip0.RawData.GetData(), ImageMip0.GetImageSizeBytes());
				InOutTexture->Source.UnlockMip(0);
				InOutTexture->Modify();
				InOutTexture->UpdateResource();

				if (bSynchronous)
				{
					FTextureCompilingManager::Get().FinishCompilation({ InOutTexture });
				}
				
				return true;
			}
		}
	}

	return false;
}
