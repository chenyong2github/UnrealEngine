// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	XXXTargetPlatform.cpp: Implements the FXXXTargetPlatform class.
=============================================================================*/

#include "XXXTargetPlatform.h"
#include "Misc/ScopeLock.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"
#if WITH_ENGINE
	#include "TextureResource.h"
#endif

#pragma warning (disable:4400)
#pragma warning (disable:4564)

DEFINE_LOG_CATEGORY_STATIC(LogXXXTargetPlatform, Log, All);


/* Static initialization
 *****************************************************************************/

FCriticalSection FXXXTargetPlatform::DevicesCriticalSection;

/* FXXXTargetPlatform structors
 *****************************************************************************/

FXXXTargetPlatform::FXXXTargetPlatform()
{
	// load the final XXX engine settings for this game
	FConfigCacheIni::LoadLocalIniFile(XXXEngineSettings, TEXT("Engine"), true, *PlatformName());

#if WITH_ENGINE
	// load up texture settings from the config file
	XXXLODSettings = nullptr; // These are registered by the device profile system.
	StaticMeshLODSettings.Initialize(XXXEngineSettings);
#endif
}

FXXXTargetPlatform::~FXXXTargetPlatform( )
{
}

/* ITargetPlatform interface
 *****************************************************************************/

bool FXXXTargetPlatform::AddDevice( const FString& DeviceName, bool bDefault )
{
	FScopeLock Lock( &DevicesCriticalSection );

	FXXXTargetDevicePtr& Device = Devices.FindOrAdd( DeviceName );

	if( !Device.IsValid() )
	{
		Device = MakeShareable( new FXXXTargetDevice( *this, DeviceName ) );
		DeviceDiscoveredEvent.Broadcast( Device.ToSharedRef() );
	}

	return true;
}


void FXXXTargetPlatform::GetAllDevices( TArray<ITargetDevicePtr>& OutDevices ) const
{
	FScopeLock Lock( &DevicesCriticalSection );

	OutDevices.Reset();

	for( auto Iter = Devices.CreateConstIterator(); Iter; ++Iter )
	{
		OutDevices.Add( Iter.Value() );
	}
}



bool FXXXTargetPlatform::GenerateStreamingInstallManifest(const TMultiMap<FString, int32>& ChunkMap, const TSet<int32>& ChunkIDsInUse) const
{
	return true;
}


ITargetDevicePtr FXXXTargetPlatform::GetDefaultDevice( ) const
{
	FScopeLock Lock( &DevicesCriticalSection );

	return Devices.FindRef( DefaultDeviceName );
}


ITargetDevicePtr FXXXTargetPlatform::GetDevice( const FTargetDeviceId& DeviceId )
{
	if( DeviceId.GetPlatformName() == this->PlatformName() )
	{
		FScopeLock Lock( &DevicesCriticalSection );
		for( auto MapIt = Devices.CreateIterator(); MapIt; ++MapIt )
		{
			FXXXTargetDevicePtr& Device = MapIt->Value;
			if( Device->GetName() == DeviceId.GetDeviceName() )
			{
				return Device;
			}
		}
	}

	return nullptr;
}


bool FXXXTargetPlatform::IsRunningPlatform( ) const
{
	return false; // but this will never be called because this platform doesn't run the target platform framework
}


bool FXXXTargetPlatform::SupportsFeature( ETargetPlatformFeatures Feature ) const
{
	switch (Feature)
	{
	case ETargetPlatformFeatures::SdkConnectDisconnect:
	case ETargetPlatformFeatures::Packaging:
	case ETargetPlatformFeatures::DeviceOutputLog:
		return true;
	default:
		return TTargetPlatformBase< FXXXPlatformProperties >::SupportsFeature(Feature);
	}
}


#if WITH_ENGINE

void FXXXTargetPlatform::GetAllPossibleShaderFormats( TArray<FName>& OutFormats ) const
{
//	static FName NAME_SF_XXX(TEXT("SF_XXX"));
	static FName NAME_SF_XXX(TEXT("PCD3D_SM5"));
	OutFormats.AddUnique(NAME_SF_XXX);
}


void FXXXTargetPlatform::GetAllTargetedShaderFormats( TArray<FName>& OutFormats ) const
{
	GetAllPossibleShaderFormats(OutFormats);
}


const class FStaticMeshLODSettings& FXXXTargetPlatform::GetStaticMeshLODSettings( ) const
{
	return StaticMeshLODSettings;
}


void FXXXTargetPlatform::GetTextureFormats( const UTexture* InTexture, TArray< TArray<FName> >& OutFormats ) const
{
	GetDefaultTextureFormatNamePerLayer(OutFormats.AddDefaulted_GetRef(), this, InTexture, XXXEngineSettings, true);
}

void FXXXTargetPlatform::GetAllTextureFormats(TArray<FName>& OutFormats) const
{
	GetAllDefaultTextureFormats(this, OutFormats, true);
}


const UTextureLODSettings& FXXXTargetPlatform::GetTextureLODSettings() const
{
	return *XXXLODSettings;
}


FName FXXXTargetPlatform::GetWaveFormat( const USoundWave* Wave ) const
{
	static FName NAME_AT9(TEXT("OGG"));

	return NAME_AT9;
}

void FXXXTargetPlatform::GetAllWaveFormats( TArray<FName>& OutFormats ) const
{
	static FName NAME_AT9(TEXT("OGG"));
	OutFormats.Add(NAME_AT9);
}

#endif // WITH_ENGINE


