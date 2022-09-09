// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDExporterModule.h"

#include "LevelExporterUSDOptionsCustomization.h"
#include "USDAssetOptions.h"
#include "USDMemory.h"

#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/ObjectSaveContext.h"

namespace UE::UsdExporterModule::Private
{
	TMap<FString, int32> PackagePathNameToDirtyCounter;
}

class FUsdExporterModule : public IUsdExporterModule
{
public:
	virtual void StartupModule() override
	{
		LLM_SCOPE_BYTAG(Usd);

		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >( TEXT( "PropertyEditor" ) );

		// We intentionally use the same customization for both of these
		PropertyModule.RegisterCustomClassLayout( TEXT( "LevelExporterUSDOptions" ), FOnGetDetailCustomizationInstance::CreateStatic( &FLevelExporterUSDOptionsCustomization::MakeInstance ) );
		PropertyModule.RegisterCustomClassLayout( TEXT( "LevelSequenceExporterUSDOptions" ), FOnGetDetailCustomizationInstance::CreateStatic( &FLevelExporterUSDOptionsCustomization::MakeInstance ) );

		// Modify the static mesh LOD range to have the proper define value as the maximum.
		// We have to do this the hard way here because we can't use the define within the meta tag itself
		if ( UScriptStruct* ScriptStruct = FUsdMeshAssetOptions::StaticStruct() )
		{
			if ( FProperty* LowestMeshLODProperty = ScriptStruct->FindPropertyByName( GET_MEMBER_NAME_CHECKED( FUsdMeshAssetOptions, LowestMeshLOD ) ) )
			{
				LowestMeshLODProperty->SetMetaData( TEXT( "ClampMax" ), LexToString( MAX_MESH_LOD_COUNT - 1 ) );
			}

			if ( FProperty* HighestMeshLODProperty = ScriptStruct->FindPropertyByName( GET_MEMBER_NAME_CHECKED( FUsdMeshAssetOptions, HighestMeshLOD ) ) )
			{
				HighestMeshLODProperty->SetMetaData( TEXT( "ClampMax" ), LexToString( MAX_MESH_LOD_COUNT - 1 ) );
			}
		}

		PackageMarkedDirtyEventHandle = UPackage::PackageMarkedDirtyEvent.AddLambda([](const UPackage* Package, bool bWasDirty)
		{
			if ( Package )
			{
				UE::UsdExporterModule::Private::PackagePathNameToDirtyCounter.FindOrAdd( Package->GetPathName() )++;
			}
		});

		PackageSavedWithContextEventHandle = UPackage::PackageSavedWithContextEvent.AddLambda(
			[]( const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext ObjectSaveContext )
			{
				if ( Package )
				{
					UE::UsdExporterModule::Private::PackagePathNameToDirtyCounter.Remove( Package->GetPathName() );
				}
			}
		);
	}

	virtual void ShutdownModule() override
	{
		UPackage::PackageMarkedDirtyEvent.Remove( PackageSavedWithContextEventHandle );
		UPackage::PackageMarkedDirtyEvent.Remove( PackageMarkedDirtyEventHandle );

		if ( FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr< FPropertyEditorModule >( TEXT( "PropertyEditor" ) ) )
		{
			PropertyModule->UnregisterCustomClassLayout( TEXT( "LevelExporterUSDOptions" ) );
			PropertyModule->UnregisterCustomClassLayout( TEXT( "LevelSequenceExporterUSDOptions" ) );
		}
	}

private:
	FDelegateHandle PackageMarkedDirtyEventHandle;
	FDelegateHandle PackageSavedWithContextEventHandle;
};

FString IUsdExporterModule::GeneratePackageVersionGuidString( const UPackage* Package )
{
	if ( !Package )
	{
		return {};
	}

	// Hash package's persistent Guid
	const FGuid& Guid = Package->GetPersistentGuid();
	FSHA1 SHA1;
	SHA1.Update( reinterpret_cast< const uint8* >( &Guid ), sizeof( Guid ) );

	// Hash last modified date
	FString PackageFullName = Package->GetPathName();
	FString FileName;
	if ( FPackageName::TryConvertLongPackageNameToFilename( PackageFullName, FileName ) )
	{
		FFileStatData StatData = IFileManager::Get().GetStatData( *FileName );
		if ( StatData.bIsValid )
		{
			FString ModifiedTimeString = StatData.ModificationTime.ToString();
			SHA1.UpdateWithString( *ModifiedTimeString, ModifiedTimeString.Len() );
		}
	}

	// If this asset is currently dirty, also hash how many times it was dirtied in this session
	// If its ever saved, we'll reset this counter but update the last saved date
	if ( int32* DirtyCounter = UE::UsdExporterModule::Private::PackagePathNameToDirtyCounter.Find( PackageFullName ) )
	{
		SHA1.Update( reinterpret_cast< const uint8* >( DirtyCounter ), sizeof( *DirtyCounter ) );
	}

	SHA1.Final();
	FSHAHash Hash;
	SHA1.GetHash( &Hash.Hash[ 0 ] );
	return Hash.ToString();
}

IMPLEMENT_MODULE_USD( FUsdExporterModule, USDExporter );

