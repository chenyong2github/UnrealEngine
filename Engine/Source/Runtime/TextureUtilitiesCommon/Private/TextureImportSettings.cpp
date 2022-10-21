// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureImportSettings.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"

#if WITH_EDITOR
#include "Engine/Texture.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(TextureImportSettings)

UTextureImportSettings::UTextureImportSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SectionName = TEXT("Importing");
}

void UTextureImportSettings::PostInitProperties()
{
	Super::PostInitProperties();
#if WITH_EDITOR
	if (IsTemplate())
	{
		ImportConsoleVariableValues();
	}
#endif // #if WITH_EDITOR
}

#if WITH_EDITOR
void UTextureImportSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property)
	{
		ExportValuesToConsoleVariables(PropertyChangedEvent.Property);
	}
}
#endif // #if WITH_EDITOR



#if WITH_EDITOR
namespace UE::TextureUtilitiesCommon
{
	/* Set default properties on Texture for newly imported textures, or reimports.
	*  Should be called after all texture properties are set, before PostEditChange() 
	*/
	TEXTUREUTILITIESCOMMON_API void ApplyDefaultsForNewlyImportedTextures(UTexture * Texture, bool bIsReimport)
	{
	
		// things that are done for both fresh import and reimport :

		Texture->UpdateOodleTextureSdkVersionToLatest();

		if ( bIsReimport )
		{
			return;
		}

		// things that are for fresh import only :

		// here we can change values that must have different defaults for backwards compatibility
		// we set them to the new desired value here, the Texture constructor sets the legacy value
	
		const UTextureImportSettings* Settings = GetDefault<UTextureImportSettings>();

		if ( Settings->bEnableNormalizeNormals )
		{
			// cannot check for TC_Normalmap here
			//	because of the way NormalmapIdentification is delayed in Interchange
			//	it's harmless to just always turn it on
			//	it will be ignored if we are not TC_Normalmap
			//	OutBuildSettings.bNormalizeNormals = Texture.bNormalizeNormals && Texture.IsNormalMap();
			//if ( Texture->CompressionSettings == TC_Normalmap )
			{
				Texture->bNormalizeNormals = true;
			}
		}

		if ( Settings->bEnableFastMipFilter )
		{
			Texture->bUseNewMipFilter = true;
		}
	}

}
#endif