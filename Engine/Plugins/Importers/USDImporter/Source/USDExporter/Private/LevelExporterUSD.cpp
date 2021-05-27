// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelExporterUSD.h"

#include "LevelExporterUSDOptions.h"
#include "UnrealUSDWrapper.h"
#include "USDErrorUtils.h"
#include "USDLayerUtils.h"
#include "USDOptionsWindow.h"
#include "USDSkeletalDataConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "AssetExportTask.h"
#include "EditorLevelUtils.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "IPythonScriptPlugin.h"

namespace UE
{
	namespace LevelExporterUSD
	{
		namespace Private
		{
			/**
			 * Fully streams in all levels whose names are not in LevelsToIgnore, returning a list of all levels that were streamed in.
			 */
			TArray<ULevel*> StreamInRequiredLevels( UWorld* World, const TSet<FString>& LevelsToIgnore )
			{
				TArray<ULevel*> Result;
				if ( !World )
				{
					return Result;
				}

				// Make sure all streamed levels are loaded so we can query their visibility
				const bool bForce = true;
				World->LoadSecondaryLevels( bForce );

				if ( ULevel* PersistentLevel = World->PersistentLevel )
				{
					const FString LevelName = TEXT( "Persistent Level" );
					if ( !PersistentLevel->bIsVisible && !LevelsToIgnore.Contains( LevelName ) )
					{
						Result.Add( PersistentLevel );
					}
				}

				for ( ULevelStreaming* StreamingLevel : World->GetStreamingLevels() )
				{
					if ( StreamingLevel )
					{
						if ( ULevel* Level = StreamingLevel->GetLoadedLevel() )
						{
							const FString LevelName = Level->GetTypedOuter<UWorld>()->GetName();
							if ( !Level->bIsVisible && !LevelsToIgnore.Contains( LevelName ) )
							{
								Result.Add( Level );
							}
						}
					}
				}

				TArray<bool> ShouldBeVisible;
				ShouldBeVisible.SetNumUninitialized(Result.Num());
				for ( bool& bVal : ShouldBeVisible )
				{
					bVal = true;
				}

				const bool bForceLayersVisible = true;
				EditorLevelUtils::SetLevelsVisibility( Result, ShouldBeVisible, bForceLayersVisible, ELevelVisibilityDirtyMode::DontModify );

				return Result;
			}

			/** Streams out LevelsToStreamOut from World */
			void StreamOutLevels( const TArray<ULevel*>& LevelsToStreamOut )
			{
				TArray<bool> ShouldBeVisible;
				ShouldBeVisible.SetNumZeroed( LevelsToStreamOut.Num() );

				const bool bForceLayersVisible = false;
				EditorLevelUtils::SetLevelsVisibility( LevelsToStreamOut, ShouldBeVisible, bForceLayersVisible, ELevelVisibilityDirtyMode::DontModify );
			}
		}
	}
}

ULevelExporterUSD::ULevelExporterUSD()
{
#if USE_USD_SDK
	for ( const FString& Extension : UnrealUSDWrapper::GetAllSupportedFileFormats() )
	{
		// USDZ is not supported for writing for now
		if ( Extension.Equals( TEXT( "usdz" ) ) )
		{
			continue;
		}

		FormatExtension.Add(Extension);
		FormatDescription.Add(TEXT("USD file"));
	}
	SupportedClass = UWorld::StaticClass();
	bText = false;
#endif // #if USE_USD_SDK
}

bool ULevelExporterUSD::ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags )
{
#if USE_USD_SDK
	UWorld* World = Cast< UWorld >( Object );
	if ( !World )
	{
		return false;
	}

	ULevelExporterUSDOptions* Options = nullptr;
	if ( ExportTask )
	{
		Options = Cast<ULevelExporterUSDOptions>( ExportTask->Options );
	}

	if ( !Options && ( !ExportTask || !ExportTask->bAutomated ) )
	{
		Options = GetMutableDefault<ULevelExporterUSDOptions>();
		if ( Options )
		{
			// There is a dedicated "Export selected" option that sets this, so let's sync to it
			if ( ExportTask )
			{
				Options->bSelectionOnly = ExportTask->bSelected;
			}

			const bool bIsImport = false;
			const bool bContinue = SUsdOptionsWindow::ShowOptions( *Options, bIsImport );
			if ( !bContinue )
			{
				return false;
			}
		}
	}

	if ( !Options )
	{
		return false;
	}

	// The more robust thing is to force-stream-in all levels that are currently invisible/unloaded but we check to export.
	// Not only to force actors to spawn, but also to make sure that when we want to bake a landscape it is visible
	TArray<ULevel*> StreamedInLevels = UE::LevelExporterUSD::Private::StreamInRequiredLevels( World, Options->LevelsToIgnore );

	// Note how we don't explicitly pass the Options down to Python here: We stash our desired export options on the CDO, and
	// those are read from Python by executing export_with_cdo_options().
	Options->CurrentTask = ExportTask;
	if ( IPythonScriptPlugin::Get()->IsPythonAvailable() )
	{
		IPythonScriptPlugin::Get()->ExecPythonCommand( TEXT( "import usd_unreal.level_exporter; usd_unreal.level_exporter.export_with_cdo_options()" ) );
	}
	Options->CurrentTask = nullptr;

	// Return the newly streamed in levels to their old visibilities
	UE::LevelExporterUSD::Private::StreamOutLevels( StreamedInLevels );

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}