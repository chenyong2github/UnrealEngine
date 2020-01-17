// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechRetessellateAction.h"

#ifdef CAD_LIBRARY
#include "CoreTechHelper.h"
#include "CoreTechMeshLoader.h"
#include "CoreTechTypes.h"
#endif

#include "DatasmithAdditionalData.h"
#include "DatasmithStaticMeshImporter.h" // Call to BuildStaticMesh
#include "DatasmithUtils.h"
#include "DatasmithTranslator.h"
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


const FText FCoreTechRetessellate_Impl::Label = LOCTEXT("RetessellateActionLabel", "Retessellate");
const FText FCoreTechRetessellate_Impl::Tooltip = LOCTEXT("RetessellateActionTooltip", "Tessellate the original NURBS surfaces to re-generate the mesh geometry");


bool FCoreTechRetessellate_Impl::CanApplyOnAssets(const TArray<FAssetData>& SelectedAssets)
{
#ifdef CAD_LIBRARY
	return Algo::AnyOf(SelectedAssets, [](const FAssetData& Asset){ return Datasmith::GetAdditionalData<UCoreTechParametricSurfaceData>(Asset) != nullptr; });
#else
	return false;
#endif
}

void FCoreTechRetessellate_Impl::ApplyOnAssets(const TArray<FAssetData>& SelectedAssets)
{
#ifdef CAD_LIBRARY
	TStrongObjectPtr<UCoreTechRetessellateActionOptions> RetessellateOptions = Datasmith::MakeOptions<UCoreTechRetessellateActionOptions>();

	bool bSameOptionsForAll = false;
	int32 NumAssetsToProcess = SelectedAssets.Num();
	bool bAskForSameOption = NumAssetsToProcess > 1;

	TArray<UStaticMesh*> TessellatedMeshes;
	TessellatedMeshes.Reserve( SelectedAssets.Num() );

	TUniquePtr<FScopedSlowTask> Progress;
	int32 AssetIndex = -1;
	for (const FAssetData& Asset : SelectedAssets)
	{
		AssetIndex++;
		if (UCoreTechParametricSurfaceData* CoreTechData = Datasmith::GetAdditionalData<UCoreTechParametricSurfaceData>(Asset))
		{
			if (CoreTechData->RawData.Num())
			{
				if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset.GetAsset()))
				{
					if (!bSameOptionsForAll)
					{
						Datasmith::FDisplayParameters Parameters;
						Parameters.bAskForSameOption = bAskForSameOption;
						Parameters.WindowTitle = LOCTEXT("OptionWindow_WindowTitle", "Datasmith Retessellation Options");
						Parameters.FileLabel = FText::Format(LOCTEXT("OptionWindow_AssetLabel", "Tessellate StaticMesh: {0}"), FText::FromString(StaticMesh->GetName()));
						Parameters.FileTooltip = FText::FromString(StaticMesh->GetPathName());
// 						Parameters.PackageLabel = FText::FromName(StaticMesh->GetOutermost()->FileName);
						Parameters.ProceedButtonLabel = LOCTEXT("OptionWindow_ProceedButtonLabel", "Tessellate");
						Parameters.ProceedButtonTooltip = LOCTEXT("OptionWindow_ProceedButtonTooltip", "Retessellate this mesh based on included nurbs data");
						Parameters.CancelButtonLabel = LOCTEXT("OptionWindow_CancelButtonLabel", "Cancel");
						Parameters.CancelButtonTooltip = LOCTEXT("OptionWindow_CancelButtonTooltip", "Cancel the retessellation operation");

						bAskForSameOption = false; // ask only the fist time
						RetessellateOptions->Options = CoreTechData->LastTessellationOptions;
						Datasmith::FDisplayResult Result = Datasmith::DisplayOptions(RetessellateOptions, Parameters);
						if (!Result.bValidated)
						{
							return;
						}
						bSameOptionsForAll |= Result.bUseSameOption;
					}
					CoreTechData->LastTessellationOptions = RetessellateOptions->Options;

					int32 RemainingAssetsToProcess = NumAssetsToProcess - AssetIndex;
					if (bSameOptionsForAll && !Progress.IsValid() && RemainingAssetsToProcess > 1)
					{
						Progress = MakeUnique<FScopedSlowTask>(RemainingAssetsToProcess);
						Progress->MakeDialog(true);
					}
					if (Progress)
					{
						if (Progress->ShouldCancel())
						{
							return;
						}

						FText Text = FText::Format(LOCTEXT("RetessellateAssetMessage", "Tessellate StaticMesh ({0}/{1}): {2}"),
							AssetIndex,
							NumAssetsToProcess,
							FText::FromString(StaticMesh->GetName())
						);
						Progress->EnterProgressFrame(1, Text);
					}

					if( StaticMesh->GetMeshDescription(0) == nullptr)
					{
						StaticMesh->CreateMeshDescription( 0 );
					}

					if(StaticMesh->GetMeshDescription(0) != nullptr)
					{
						StaticMesh->Modify();
						StaticMesh->PreEditChange( nullptr );

						if( ApplyOnOneAsset(*StaticMesh, *CoreTechData, RetessellateOptions->Options) )
						{
							TessellatedMeshes.Add(StaticMesh);
						}
					}
				}
			}
		}
	}

	// Make sure lightmap settings are valid
	if(TessellatedMeshes.Num() > 1)
	{
		ParallelFor(TessellatedMeshes.Num(), [&](int32 Index)
		{
			FDatasmithStaticMeshImporter::PreBuildStaticMesh(TessellatedMeshes[Index]); 
		});
	}
	else if(TessellatedMeshes.Num() > 0)
	{
		FDatasmithStaticMeshImporter::PreBuildStaticMesh( TessellatedMeshes[0] );
	}

	FDatasmithStaticMeshImporter::BuildStaticMeshes( TessellatedMeshes );

	for(UStaticMesh* StaticMesh : TessellatedMeshes)
	{
		StaticMesh->PostEditChange();
		StaticMesh->MarkPackageDirty();

		// Refresh associated editor
		TSharedPtr<IToolkit> EditingToolkit = FToolkitManager::Get().FindEditorForAsset(StaticMesh);
		if (IStaticMeshEditor* StaticMeshEditorInUse = StaticCastSharedPtr<IStaticMeshEditor>(EditingToolkit).Get())
		{
			StaticMeshEditorInUse->RefreshTool();
		}
	}

#endif // CAD_LIBRARY
}

bool FCoreTechRetessellate_Impl::ApplyOnOneAsset(UStaticMesh& StaticMesh, UCoreTechParametricSurfaceData& CoreTechData, const FDatasmithTessellationOptions& RetessellateOptions)
{
	bool bSuccessfulTessellation = false;

#ifdef CAD_LIBRARY
	// make a temporary file as GPure can only deal with files.
	FString ResourceFile = CoreTechData.SourceFile.IsEmpty() ? FPaths::ProjectIntermediateDir() / "temp.ct" : CoreTechData.SourceFile;
	FFileHelper::SaveArrayToFile(CoreTechData.RawData, *ResourceFile);

	CADLibrary::CoreTechMeshLoader Loader;
	CADLibrary::CTMesh Mesh;

	CADLibrary::FImportParameters ImportParameters;
	ImportParameters.MetricUnit = CoreTechData.SceneParameters.MetricUnit;
	ImportParameters.ScaleFactor = CoreTechData.SceneParameters.ScaleFactor;
	ImportParameters.ChordTolerance = RetessellateOptions.ChordTolerance;
	ImportParameters.MaxEdgeLength = RetessellateOptions.MaxEdgeLength;
	ImportParameters.MaxNormalAngle = RetessellateOptions.NormalTolerance;
	ImportParameters.ModelCoordSys = CADLibrary::EModelCoordSystem(CoreTechData.SceneParameters.ModelCoordSys);
	ImportParameters.StitchingTechnique = CADLibrary::EStitchingTechnique(RetessellateOptions.StitchingTechnique);

	CADLibrary::FMeshParameters MeshParameters;
	MeshParameters.bNeedSwapOrientation = CoreTechData.MeshParameters.bNeedSwapOrientation;
	MeshParameters.bIsSymmetric = CoreTechData.MeshParameters.bIsSymmetric;
	MeshParameters.SymmetricNormal = CoreTechData.MeshParameters.SymmetricNormal;
	MeshParameters.SymmetricOrigin = CoreTechData.MeshParameters.SymmetricOrigin;

	// Previous MeshDescription is get to be able to create a new one with the same order of PolygonGroup (the matching of color and partition is currently based on their order)
	if(FMeshDescription* DestinationMeshDescription = StaticMesh.GetMeshDescription(0))
	{
		FStaticMeshAttributes DestinationMeshDescriptionAttributes(*DestinationMeshDescription);

		FMeshDescription MeshDescription;
		FStaticMeshAttributes MeshDescriptionAttributes(MeshDescription);
		MeshDescriptionAttributes.Register();

		TPolygonGroupAttributesRef<FName> PolygonGroupDestinationMeshSlotNames = DestinationMeshDescriptionAttributes.GetPolygonGroupMaterialSlotNames();
		TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = MeshDescriptionAttributes.GetPolygonGroupMaterialSlotNames();
		for (FPolygonGroupID PolygonGroupID : DestinationMeshDescription->PolygonGroups().GetElementIDs())
		{
			FName ImportedSlotName = PolygonGroupDestinationMeshSlotNames[PolygonGroupID];
			FPolygonGroupID PolyGroupID = MeshDescription.CreatePolygonGroup();
			PolygonGroupImportedMaterialSlotNames[PolyGroupID] = ImportedSlotName;
		}

		if ( Loader.LoadFile(ResourceFile, MeshDescription, ImportParameters, MeshParameters))
		{
			// @TODO: check: no commit?
			*DestinationMeshDescription = MoveTemp(MeshDescription);
			bSuccessfulTessellation = true;
		}
	}
#endif // CAD_LIBRARY

	return bSuccessfulTessellation;
}

#undef LOCTEXT_NAMESPACE

TSet<UStaticMesh*> GetReferencedStaticMeshes(const TArray<AActor*>& SelectedActors)
{
	TSet<UStaticMesh*> ReferencedStaticMeshes;

#ifdef CAD_LIBRARY
	for (const AActor* Actor : SelectedActors)
	{
		if (Actor)
		{
			for (const auto& Component : Actor->GetComponents())
			{
				if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Component))
				{
					ReferencedStaticMeshes.Add(SMC->GetStaticMesh());
				}
			}
		}
	}
#endif // CAD_LIBRARY

	return ReferencedStaticMeshes;
}

bool UCoreTechRetessellateAction::CanApplyOnActors(const TArray<AActor*>& SelectedActors)
{
	const TSet<UStaticMesh*> ReferencedStaticMeshes = GetReferencedStaticMeshes(SelectedActors);
	return Algo::AnyOf(ReferencedStaticMeshes, [](const UStaticMesh* Mesh){ return Datasmith::GetAdditionalData<UCoreTechParametricSurfaceData>(FAssetData(Mesh)); });
}

void UCoreTechRetessellateAction::ApplyOnActors(const TArray<AActor*>& SelectedActors)
{
	TArray<FAssetData> AssetData;
	Algo::Transform(GetReferencedStaticMeshes(SelectedActors), AssetData, [](UStaticMesh* Mesh){ return FAssetData(Mesh);});
	return ApplyOnAssets(AssetData);
}
