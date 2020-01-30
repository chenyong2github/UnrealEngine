// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechBlueprintLibrary.h"

#ifdef CAD_LIBRARY
#include "CoreTechHelper.h"
#include "CoreTechMeshLoader.h"
#endif
#include "CoreTechRetessellateAction.h"
#include "DatasmithAdditionalData.h"
#include "DatasmithStaticMeshImporter.h" // Call to BuildStaticMesh
#include "DatasmithTranslator.h"
#include "DatasmithUtils.h"
#include "UI/DatasmithDisplayHelper.h"

#include "Algo/AnyOf.h"
#include "AssetData.h"
#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
#include "IStaticMeshEditor.h"
#include "StaticMeshAttributes.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "Toolkits/IToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Algo/Transform.h"


#define LOCTEXT_NAMESPACE "CoreTechRetessellateAction"

bool UCoreTechBlueprintLibrary::RetessellateStaticMesh( UStaticMesh* StaticMesh, const FDatasmithTessellationOptions& TessellationSettings, bool bPostChanges, FText& OutReason )
{
	bool bTessellationOutcome = false;

#ifdef CAD_LIBRARY
	int32 LODIndex = 0;

	FAssetData AssetData( StaticMesh );
	if( UCoreTechParametricSurfaceData* CoreTechData = Datasmith::GetAdditionalData<UCoreTechParametricSurfaceData>(AssetData) )
	{
		// Make sure MeshDescription exists
		FMeshDescription* DestinationMeshDescription = StaticMesh->GetMeshDescription( LODIndex );
		if (DestinationMeshDescription == nullptr)
		{
			DestinationMeshDescription = StaticMesh->CreateMeshDescription(0);
		}

		if( DestinationMeshDescription )
		{
			if(bPostChanges)
			{
				StaticMesh->Modify();
				StaticMesh->PreEditChange( nullptr );
			}

			if( FCoreTechRetessellate_Impl::ApplyOnOneAsset( *StaticMesh, *CoreTechData, TessellationSettings ) )
			{
				// Post static mesh has changed
				if(bPostChanges)
				{
					FDatasmithStaticMeshImporter::PreBuildStaticMesh(StaticMesh); // handle uvs stuff
					FDatasmithStaticMeshImporter::BuildStaticMesh(StaticMesh);

					StaticMesh->PostEditChange();
					StaticMesh->MarkPackageDirty();

					// Refresh associated editor
					TSharedPtr<IToolkit> EditingToolkit = FToolkitManager::Get().FindEditorForAsset(StaticMesh);
					if (IStaticMeshEditor* StaticMeshEditorInUse = StaticCastSharedPtr<IStaticMeshEditor>(EditingToolkit).Get())
					{
						StaticMeshEditorInUse->RefreshTool();
					}
				}
				// No posting required, just make sure the new tessellation is committed
				else
				{
					UStaticMesh::FCommitMeshDescriptionParams Params;
					Params.bMarkPackageDirty = false;
					Params.bUseHashAsGuid = true;
					StaticMesh->CommitMeshDescription( LODIndex, Params );
				}

				// Save last tessellation settings
				CoreTechData->LastTessellationOptions = TessellationSettings;

				bTessellationOutcome = true;
			}
			else
			{
				OutReason = NSLOCTEXT("BlueprintRetessellation", "LoadFailed", "Cannot generate mesh from parametric surface data");
			}
		}
		else
		{
			OutReason = NSLOCTEXT("BlueprintRetessellation", "MeshDescriptionMissing", "Cannot create mesh description");
		}
	}
	else
	{
		OutReason = NSLOCTEXT("BlueprintRetessellation", "MissingData", "No tessellation data attached to the static mesh");
	}
#endif

	return bTessellationOutcome;
}

#undef LOCTEXT_NAMESPACE