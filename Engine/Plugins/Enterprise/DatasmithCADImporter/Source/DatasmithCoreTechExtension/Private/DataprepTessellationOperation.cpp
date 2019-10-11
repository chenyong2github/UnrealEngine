// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepTessellationOperation.h"

#ifdef CAD_LIBRARY
#include "CoreTechHelper.h"
#include "CoreTechMeshLoader.h"
#include "CoreTechParametricSurfaceExtension.h"
#endif
#include "DatasmithAdditionalData.h"
#include "DatasmithAssetImportData.h"
#include "DatasmithUtils.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "GenericPlatform/GenericPlatformTime.h"
#include "Logging/LogMacros.h"
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "DataprepTessellationOperation"

DEFINE_LOG_CATEGORY_STATIC(LogCADLibrary, Log, All)

void UDataprepTessellationOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
#ifdef CAD_LIBRARY
	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();

	int32 StaticMeshCount = 0;

	for( UObject* Object : InContext.Objects )
	{
		if( UStaticMesh* StaticMesh = Cast< UStaticMesh >( Object ))
		{
			if( !StaticMesh->IsMeshDescriptionValid( 0 ) )
			{
				FText WarningMsg = FText::Format( LOCTEXT( "DataprepTessellationOperation_EmptyMesh", "No triangles in static mesh {0}" ), FText::FromString( StaticMesh->GetName() ) );
				LogWarning( WarningMsg );
				// #ueent_todo: Remove this log when Dataprep logging is operational
				UE_LOG( LogCADLibrary, Log, TEXT("Retessellation failed : %s "), *WarningMsg.ToString() );
				continue;
			}

			// Borrowed from UCoreTechRetessellateAction class
			// #ueent_todo: Create CAD blueprint library to expose this feature
			FAssetData AssetData( StaticMesh );
			if( UCoreTechParametricSurfaceData* CoreTechData = Datasmith::GetAdditionalData<UCoreTechParametricSurfaceData>(AssetData) )
			{
				FString ResourceFile = CoreTechData->SourceFile.IsEmpty() ? FPaths::ProjectIntermediateDir() / "temp.ct" : CoreTechData->SourceFile;
				FFileHelper::SaveArrayToFile(CoreTechData->RawData, *ResourceFile);

				CADLibrary::CoreTechMeshLoader Loader;

				CADLibrary::FImportParameters Parameters;
				Parameters.MetricUnit = CoreTechData->SceneParameters.MetricUnit;
				Parameters.ChordTolerance = TessellationSettings.ChordTolerance;
				Parameters.MaxEdgeLength = TessellationSettings.MaxEdgeLength;
				Parameters.MaxNormalAngle = TessellationSettings.NormalTolerance;
				Parameters.StitchingTechnique = CADLibrary::EStitchingTechnique(TessellationSettings.StitchingTechnique);

				CADLibrary::FMeshParameters MeshParameters;
				MeshParameters.ModelCoordSys = FDatasmithUtils::EModelCoordSystem(CoreTechData->SceneParameters.ModelCoordSys);

				// Previous MeshDescription is get to be able to create a new one with the same order of PolygonGroup (the matching of color and partition is currently based on their order)
				FMeshDescription* DestinationMeshDescription = StaticMesh->GetMeshDescription(0);
				if (DestinationMeshDescription == nullptr)
				{
					DestinationMeshDescription = StaticMesh->CreateMeshDescription(0);
				}
				FStaticMeshAttributes DestinationMeshAttributes(*DestinationMeshDescription);

				// Create MeshDescription from tessellated mesh
				FMeshDescription MeshDescription;
				FStaticMeshAttributes MeshDescriptionAttributes(MeshDescription);
				MeshDescriptionAttributes.Register();

				TPolygonGroupAttributesRef<FName> PolygonGroupDestinationMeshSlotNames = DestinationMeshAttributes.GetPolygonGroupMaterialSlotNames();
				TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = MeshDescriptionAttributes.GetPolygonGroupMaterialSlotNames();
				for (FPolygonGroupID PolygonGroupID : DestinationMeshDescription->PolygonGroups().GetElementIDs())
				{
					FName ImportedSlotName = PolygonGroupDestinationMeshSlotNames[PolygonGroupID];
					FPolygonGroupID PolyGroupID = MeshDescription.CreatePolygonGroup();
					PolygonGroupImportedMaterialSlotNames[PolyGroupID] = ImportedSlotName;
				}

				if (!Loader.LoadFile(ResourceFile, MeshDescription, Parameters, MeshParameters))
				{
					UE_LOG(LogCADLibrary, Log, TEXT("Retessellation of %s failed : Cannot generate mesh from parametric surface data"), *StaticMesh->GetName());
					continue;
				}

				// Move result of tessellation into static mesh LOD 0
				*DestinationMeshDescription = MoveTemp(MeshDescription);

				// @TODO: check: no CommitStaticMeshDescription?

				// Save last tessellation settings
				CoreTechData->LastTessellationOptions = TessellationSettings;
			}
			else
			{
				UE_LOG( LogCADLibrary, Log, TEXT("Retessellation of %s failed : No tessellation data attached to the static mesh"), *StaticMesh->GetName() );
				continue;
			}

			++StaticMeshCount;
		}
	}

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	UE_LOG( LogCADLibrary, Log, TEXT("Tessellation of %d static mesh(es) in [%d min %.3f s]"), StaticMeshCount, ElapsedMin, ElapsedSeconds );
#else
	UE_LOG( LogCADLibrary, Warning, TEXT("Tessellation not performed") );
#endif
}

#undef LOCTEXT_NAMESPACE
