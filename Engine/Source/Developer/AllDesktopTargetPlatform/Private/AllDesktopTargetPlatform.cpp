// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AllDesktopTargetPlatform.cpp: Implements the FDesktopTargetPlatform class.
=============================================================================*/

#include "AllDesktopTargetPlatform.h"

#if WITH_ENGINE
	#include "Sound/SoundWave.h"
#endif




/* FAllDesktopTargetPlatform structors
 *****************************************************************************/

FAllDesktopTargetPlatform::FAllDesktopTargetPlatform()
{
#if WITH_ENGINE
	// use non-platform specific settings
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, NULL);
	StaticMeshLODSettings.Initialize(EngineSettings);
#endif // #if WITH_ENGINE

}


FAllDesktopTargetPlatform::~FAllDesktopTargetPlatform()
{
}



/* ITargetPlatform interface
 *****************************************************************************/

#if WITH_ENGINE


void FAllDesktopTargetPlatform::GetAllPossibleShaderFormats( TArray<FName>& OutFormats ) const
{
	static FName NAME_PCD3D_SM5(TEXT("PCD3D_SM5"));
	static FName NAME_GLSL_430(TEXT("GLSL_430"));

#if PLATFORM_WINDOWS
	// right now, only windows can properly compile D3D shaders (this won't corrupt the DDC, but it will
	// make it so that packages cooked on Mac/Linux will only run on Windows with -opengl)
	OutFormats.AddUnique(NAME_PCD3D_SM5);
#endif
	OutFormats.AddUnique(NAME_GLSL_430);
}


void FAllDesktopTargetPlatform::GetAllTargetedShaderFormats( TArray<FName>& OutFormats ) const
{
	GetAllPossibleShaderFormats(OutFormats);
}

void FAllDesktopTargetPlatform::GetTextureFormats( const UTexture* Texture, TArray< TArray<FName> >& OutFormats) const
{
	// just use the standard texture format name for this texture (without DX11 texture support)
	GetDefaultTextureFormatNamePerLayer(OutFormats.AddDefaulted_GetRef(), this, Texture, EngineSettings, false);
}

void FAllDesktopTargetPlatform::GetAllTextureFormats(TArray<FName>& OutFormats) const
{
	GetAllDefaultTextureFormats(this, OutFormats, false);
}


FName FAllDesktopTargetPlatform::GetWaveFormat( const class USoundWave* Wave ) const
{
	static FName NAME_OGG(TEXT("OGG"));
	static FName NAME_OPUS(TEXT("OPUS"));
	static FName NAME_ADPCM(TEXT("ADPCM"));

	// Seekable streams need to pick a codec which allows fixed-sized frames so we can compute stream chunk index to load
	if (Wave->IsSeekableStreaming())
	{
		return NAME_ADPCM;
	}
	else if (Wave->IsStreaming())
	{
#if !USE_VORBIS_FOR_STREAMING
		return NAME_OPUS;
#endif
	}

	return NAME_OGG;
}


void FAllDesktopTargetPlatform::GetAllWaveFormats(TArray<FName>& OutFormats) const
{
	static FName NAME_ADPCM(TEXT("ADPCM"));
	static FName NAME_OGG(TEXT("OGG"));
	static FName NAME_OPUS(TEXT("OPUS"));

	OutFormats.Add(NAME_ADPCM);
	OutFormats.Add(NAME_OGG);
	OutFormats.Add(NAME_OPUS);
}

#endif // WITH_ENGINE
