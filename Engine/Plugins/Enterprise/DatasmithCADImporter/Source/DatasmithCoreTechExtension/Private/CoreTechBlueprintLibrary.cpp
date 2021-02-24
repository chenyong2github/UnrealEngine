// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechBlueprintLibrary.h"

#include "CoreTechRetessellateAction.h"
#include "DatasmithAdditionalData.h"
#include "DatasmithStaticMeshImporter.h" // Call to BuildStaticMesh
#include "DatasmithUtils.h"

#include "AssetData.h"
#include "Engine/StaticMesh.h"
#include "IStaticMeshEditor.h"
#include "StaticMeshAttributes.h"
#include "Toolkits/ToolkitManager.h"


#define LOCTEXT_NAMESPACE "CoreTechRetessellateAction"


bool UCoreTechBlueprintLibrary::RetessellateStaticMesh(UStaticMesh* StaticMesh, const FDatasmithRetessellationOptions& TessellationSettings, FText& FailureReason)
{
	return RetessellateStaticMeshWithNotification(StaticMesh, TessellationSettings, true, FailureReason);
}

bool UCoreTechBlueprintLibrary::RetessellateStaticMeshWithNotification(UStaticMesh* StaticMesh, const FDatasmithRetessellationOptions& TessellationSettings, bool bApplyChanges, FText& FailureReason)
{
	bool bTessellationOutcome = false;

	int32 LODIndex = 0;

	FAssetData AssetData( StaticMesh );
	if (UCoreTechParametricSurfaceData* CoreTechData = Datasmith::GetAdditionalData<UCoreTechParametricSurfaceData>(AssetData))
	{
		// Make sure MeshDescription exists
		FMeshDescription* DestinationMeshDescription = StaticMesh->GetMeshDescription( LODIndex );
		if (DestinationMeshDescription == nullptr)
		{
			DestinationMeshDescription = StaticMesh->CreateMeshDescription(0);
		}

		if (DestinationMeshDescription)
		{
			if (bApplyChanges)
			{
				StaticMesh->Modify();
				StaticMesh->PreEditChange( nullptr );
			}

			const int32 OldNumberOfUVChannels = DestinationMeshDescription->VertexInstanceAttributes().GetAttributeIndexCount<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
			if (FCoreTechRetessellate_Impl::ApplyOnOneAsset( *StaticMesh, *CoreTechData, TessellationSettings ))
			{
				const int32 NumberOfUVChannels = DestinationMeshDescription->VertexInstanceAttributes().GetAttributeIndexCount<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
				if (NumberOfUVChannels < OldNumberOfUVChannels)
				{
					FailureReason = FText::Format(NSLOCTEXT("BlueprintRetessellation", "UVChannelsDestroyed", "Tessellation operation on Static Mesh {0} is destroying all UV channels above channel #{1}"), FText::FromString(StaticMesh->GetName()), NumberOfUVChannels - 1);
				}

				// Post static mesh has changed
				if (bApplyChanges)
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
				FailureReason = NSLOCTEXT("BlueprintRetessellation", "LoadFailed", "Cannot generate mesh from parametric surface data");
			}
		}
		else
		{
			FailureReason = NSLOCTEXT("BlueprintRetessellation", "MeshDescriptionMissing", "Cannot create mesh description");
		}
	}
	else
	{
		FailureReason = NSLOCTEXT("BlueprintRetessellation", "MissingData", "No tessellation data attached to the static mesh");
	}

	return bTessellationOutcome;
}

#undef LOCTEXT_NAMESPACE