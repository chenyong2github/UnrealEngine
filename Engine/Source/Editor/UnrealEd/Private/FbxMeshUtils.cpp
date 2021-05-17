// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxMeshUtils.h"
#include "EngineDefines.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/FbxAssetImportData.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxImportUI.h"
#include "Engine/StaticMesh.h"
#include "EditorDirectories.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "ComponentReregisterContext.h"
#include "Logging/TokenizedMessage.h"
#include "FbxImporter.h"
#include "StaticMeshResources.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshModel.h"
#include "EditorFramework/AssetImportData.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "ImportUtils/StaticMeshImportUtils.h"

#if WITH_APEX_CLOTHING
	#include "ApexClothingUtils.h"
#endif // #if WITH_APEX_CLOTHING


#include "Misc/FbxErrors.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "ClothingAsset.h"
#include "SkinWeightsUtilities.h"
#include "LODUtilities.h"

DEFINE_LOG_CATEGORY_STATIC(LogExportMeshUtils, Log, All);

#define LOCTEXT_NAMESPACE "FbxMeshUtil"


namespace FbxMeshUtils
{
	/** Helper function used for retrieving data required for importing static mesh LODs */
	void PopulateFBXStaticMeshLODList(UnFbx::FFbxImporter* FFbxImporter, FbxNode* Node, TArray< TArray<FbxNode*>* >& LODNodeList, int32& MaxLODCount, bool bUseLODs)
	{
		// Check for LOD nodes, if one is found, add it to the list
		if (bUseLODs && Node->GetNodeAttribute() && Node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eLODGroup)
		{
			for (int32 ChildIdx = 0; ChildIdx < Node->GetChildCount(); ++ChildIdx)
			{
				if ((LODNodeList.Num() - 1) < ChildIdx)
				{
					TArray<FbxNode*>* NodeList = new TArray<FbxNode*>;
					LODNodeList.Add(NodeList);
				}
				FFbxImporter->FindAllLODGroupNode(*(LODNodeList[ChildIdx]), Node, ChildIdx);
			}

			if (MaxLODCount < (Node->GetChildCount() - 1))
			{
				MaxLODCount = Node->GetChildCount() - 1;
			}
		}
		else
		{
			// If we're just looking for meshes instead of LOD nodes, add those to the list
			if (!bUseLODs && Node->GetMesh())
			{
				if (LODNodeList.Num() == 0)
				{
					TArray<FbxNode*>* NodeList = new TArray<FbxNode*>;
					LODNodeList.Add(NodeList);
				}

				LODNodeList[0]->Add(Node);
			}

			// Recursively examine child nodes
			for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
			{
				PopulateFBXStaticMeshLODList(FFbxImporter, Node->GetChild(ChildIndex), LODNodeList, MaxLODCount, bUseLODs);
			}
		}
	}

	bool ImportStaticMeshLOD( UStaticMesh* BaseStaticMesh, const FString& Filename, int32 LODLevel)
	{
		bool bSuccess = false;

		UE_LOG(LogExportMeshUtils, Log, TEXT("Fbx LOD loading"));
		// logger for all error/warnings
		// this one prints all messages that are stored in FFbxImporter
		// this function seems to get called outside of FBX factory
		UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
		UnFbx::FFbxLoggerSetter Logger(FFbxImporter);

		UnFbx::FBXImportOptions* ImportOptions = FFbxImporter->GetImportOptions();
		
		bool IsReimport = BaseStaticMesh->GetRenderData()->LODResources.Num() > LODLevel;
		UFbxStaticMeshImportData* ImportData = Cast<UFbxStaticMeshImportData>(BaseStaticMesh->AssetImportData);
		if (ImportData != nullptr)
		{
			
			UFbxImportUI* ReimportUI = NewObject<UFbxImportUI>();
			ReimportUI->MeshTypeToImport = FBXIT_StaticMesh;
			UnFbx::FBXImportOptions::ResetOptions(ImportOptions);
			// Import data already exists, apply it to the fbx import options
			ReimportUI->StaticMeshImportData = ImportData;
			ApplyImportUIToImportOptions(ReimportUI, *ImportOptions);
			ImportOptions->bIsImportCancelable = false;
			ImportOptions->bImportMaterials = false;
			ImportOptions->bImportTextures = false;
			//Make sure the LODGroup do not change when re-importing a mesh
			ImportOptions->StaticMeshLODGroup = BaseStaticMesh->LODGroup;
		}
		ImportOptions->bAutoComputeLodDistances = true; //Setting auto compute distance to true will avoid changing the staticmesh flag
		if ( !FFbxImporter->ImportFromFile( *Filename, FPaths::GetExtension( Filename ), true ) )
		{
			// Log the error message and fail the import.
			// @todo verify if the message works
			FFbxImporter->FlushToTokenizedErrorMessage(EMessageSeverity::Error);
		}
		else
		{
			FFbxImporter->FlushToTokenizedErrorMessage(EMessageSeverity::Warning);
			if (ImportData)
			{
				FFbxImporter->ApplyTransformSettingsToFbxNode(FFbxImporter->Scene->GetRootNode(), ImportData);
			}

			bool bUseLODs = true;
			int32 MaxLODLevel = 0;
			TArray< TArray<FbxNode*>* > LODNodeList;

			// Create a list of LOD nodes
			PopulateFBXStaticMeshLODList(FFbxImporter, FFbxImporter->Scene->GetRootNode(), LODNodeList, MaxLODLevel, bUseLODs);

			// No LODs, so just grab all of the meshes in the file
			if (MaxLODLevel == 0)
			{
				bUseLODs = false;
				MaxLODLevel = BaseStaticMesh->GetNumLODs();

				// Create a list of meshes
				PopulateFBXStaticMeshLODList(FFbxImporter, FFbxImporter->Scene->GetRootNode(), LODNodeList, MaxLODLevel, bUseLODs);

				// Nothing found, error out
				if (LODNodeList.Num() == 0)
				{
					FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText(LOCTEXT("Prompt_NoMeshFound", "No meshes were found in file."))), FFbxErrors::Generic_Mesh_MeshNotFound);

					FFbxImporter->ReleaseScene();
					return bSuccess;
				}
			}

			TSharedPtr<FExistingStaticMeshData> ExistMeshDataPtr;
			if (IsReimport)
			{
				ExistMeshDataPtr = StaticMeshImportUtils::SaveExistingStaticMeshData(BaseStaticMesh, FFbxImporter->ImportOptions, LODLevel);
			}

			// Display the LOD selection dialog
			if (LODLevel > BaseStaticMesh->GetNumLODs())
			{
				// Make sure they don't manage to select a bad LOD index
				FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Warning, FText::Format(LOCTEXT("Prompt_InvalidLODIndex", "Invalid mesh LOD index {0}, as no prior LOD index exists!"), FText::AsNumber(LODLevel))), FFbxErrors::Generic_Mesh_LOD_InvalidIndex);
			}
			else
			{
				UStaticMesh* TempStaticMesh = NULL;
				if (!LODNodeList.IsValidIndex(bUseLODs ? LODLevel : 0))
				{
					if (bUseLODs)
					{
						//Use the first LOD when user try to add or re-import a LOD from a file(different from the LOD 0 file) containing multiple LODs
						bUseLODs = false;
					}
				}
				
				if (LODNodeList.IsValidIndex(bUseLODs ? LODLevel : 0))
				{
					TempStaticMesh = (UStaticMesh*)FFbxImporter->ImportStaticMeshAsSingle(BaseStaticMesh->GetOutermost(), *(LODNodeList[bUseLODs ? LODLevel : 0]), NAME_None, RF_NoFlags, ImportData, BaseStaticMesh, LODLevel, ExistMeshDataPtr.Get());
				}
				
				// Add imported mesh to existing model
				if( TempStaticMesh )
				{
					//Build the staticmesh
					FFbxImporter->PostImportStaticMesh(TempStaticMesh, *(LODNodeList[bUseLODs ? LODLevel : 0]), LODLevel);
					TArray<int32> ReimportLodList;
					ReimportLodList.Add(LODLevel);
					StaticMeshImportUtils::UpdateSomeLodsImportMeshData(BaseStaticMesh, &ReimportLodList);
					if(IsReimport)
					{
						StaticMeshImportUtils::RestoreExistingMeshData(ExistMeshDataPtr, BaseStaticMesh, LODLevel, false, ImportOptions->bResetToFbxOnMaterialConflict);
					}

					// Update mesh component
					BaseStaticMesh->PostEditChange();
					BaseStaticMesh->MarkPackageDirty();

					// Import worked
					FNotificationInfo NotificationInfo(FText::GetEmpty());
					NotificationInfo.Text = FText::Format(LOCTEXT("LODImportSuccessful", "Mesh for LOD {0} imported successfully!"), FText::AsNumber(LODLevel));
					NotificationInfo.ExpireDuration = 5.0f;
					FSlateNotificationManager::Get().AddNotification(NotificationInfo);
					if (BaseStaticMesh->IsSourceModelValid(LODLevel))
					{
						FStaticMeshSourceModel& SourceModel = BaseStaticMesh->GetSourceModel(LODLevel);
						SourceModel.SourceImportFilename = UAssetImportData::SanitizeImportFilename(Filename, nullptr);
						SourceModel.bImportWithBaseMesh = false;
					}
					bSuccess = true;
				}
				else
				{
					// Import failed
					FNotificationInfo NotificationInfo(FText::GetEmpty());
					NotificationInfo.Text = FText::Format(LOCTEXT("LODImportFail", "Failed to import mesh for LOD {0}!"), FText::AsNumber( LODLevel ));
					NotificationInfo.ExpireDuration = 5.0f;
					FSlateNotificationManager::Get().AddNotification(NotificationInfo);

					bSuccess = false;
				}
			}

			// Cleanup
			for (int32 i = 0; i < LODNodeList.Num(); ++i)
			{
				delete LODNodeList[i];
			}
		}
		FFbxImporter->ReleaseScene();

		return bSuccess;
	}

	bool ImportSkeletalMeshLOD( class USkeletalMesh* SelectedSkelMesh, const FString& Filename, int32 LODLevel)
	{
		UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
		//Make sure skeletal mesh is valid
		if (!SelectedSkelMesh)
		{
			FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, LOCTEXT("FBXImport_NoSelectedSkeletalMesh", "Cannot import a LOD if there is not a valid selected skeletal mesh.")), FFbxErrors::Generic_MeshNotFound);
			return false;
		}

		bool bSuccess = false;

		// Check the file extension for FBX. Anything that isn't .FBX is rejected
		const FString FileExtension = FPaths::GetExtension(Filename);
		const bool bIsFBX = FCString::Stricmp(*FileExtension, TEXT("FBX")) == 0;
		bool bSceneIsCleanUp = false;
		TArray< TArray<FbxNode*>* > MeshArray;
		auto CleanUpScene = [&bSceneIsCleanUp, &MeshArray, &FFbxImporter]()
		{
			if (bSceneIsCleanUp)
			{
				return;
			}
			bSceneIsCleanUp = true;
			// Cleanup
			for (int32 i = 0; i < MeshArray.Num(); i++)
			{
				delete MeshArray[i];
			}
			FFbxImporter->ReleaseScene();
			FFbxImporter = nullptr;
		};

		//Skip none fbx file
		if (!bIsFBX)
		{
			return false;
		}

		FScopedSkeletalMeshPostEditChange ScopePostEditChange(SelectedSkelMesh);
		UnFbx::FFbxScopedOperation FbxScopedOperation(FFbxImporter);

		//If the imported LOD already exist, we will need to reimport all the skin weight profiles
		bool bMustReimportAlternateSkinWeightProfile = false;

		// Get a list of all the clothing assets affecting this LOD so we can re-apply later
		TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingBindings;
		TArray<UClothingAssetBase*> ClothingAssetsInUse;
		TArray<int32> ClothingAssetSectionIndices;
		TArray<int32> ClothingAssetInternalLodIndices;

		FSkeletalMeshModel* ImportedResource = SelectedSkelMesh->GetImportedModel();
		if(ImportedResource && ImportedResource->LODModels.IsValidIndex(LODLevel))
		{
			bMustReimportAlternateSkinWeightProfile = true;
			FLODUtilities::UnbindClothingAndBackup(SelectedSkelMesh, ClothingBindings, LODLevel);
		}

		//Lambda to call to re-apply the clothing
		auto ReapplyClothing = [&SelectedSkelMesh, &ClothingBindings, &ImportedResource, &LODLevel]()
		{
			if (ImportedResource && ImportedResource->LODModels.IsValidIndex(LODLevel))
			{
				// Re-apply our clothing assets
				FLODUtilities::RestoreClothingFromBackup(SelectedSkelMesh, ClothingBindings, LODLevel);
			}
		};

		// don't import material and animation
		UnFbx::FBXImportOptions* ImportOptions = FFbxImporter->GetImportOptions();
			
		//Set the skeletal mesh import data from the base mesh, this make sure the import rotation transform is use when importing a LOD
		UFbxSkeletalMeshImportData* TempAssetImportData = NULL;

		UFbxAssetImportData *FbxAssetImportData = Cast<UFbxAssetImportData>(SelectedSkelMesh->GetAssetImportData());
		if (FbxAssetImportData != nullptr)
		{
			UFbxSkeletalMeshImportData* ImportData = Cast<UFbxSkeletalMeshImportData>(FbxAssetImportData);
			if (ImportData)
			{
				TempAssetImportData = ImportData;
				UnFbx::FBXImportOptions::ResetOptions(ImportOptions);
				// Prepare the import options
				UFbxImportUI* ReimportUI = NewObject<UFbxImportUI>();
				ReimportUI->MeshTypeToImport = FBXIT_SkeletalMesh;
				ReimportUI->Skeleton = SelectedSkelMesh->GetSkeleton();
				ReimportUI->PhysicsAsset = SelectedSkelMesh->GetPhysicsAsset();
				// Import data already exists, apply it to the fbx import options
				ReimportUI->SkeletalMeshImportData = ImportData;
				//Some options not supported with skeletal mesh
				ReimportUI->SkeletalMeshImportData->bBakePivotInVertex = false;
				ReimportUI->SkeletalMeshImportData->bTransformVertexToAbsolute = true;
				ApplyImportUIToImportOptions(ReimportUI, *ImportOptions);
			}
			ImportOptions->bImportMaterials = false;
			ImportOptions->bImportTextures = false;
		}
		ImportOptions->bImportAnimations = false;
		//Adjust the option in case we import only the skinning or the geometry
		if (ImportOptions->bImportAsSkeletalSkinning)
		{
			ImportOptions->bImportMaterials = false;
			ImportOptions->bImportTextures = false;
			ImportOptions->bImportLOD = false;
			ImportOptions->bImportSkeletalMeshLODs = false;
			ImportOptions->bImportAnimations = false;
			ImportOptions->bImportMorph = false;
		}
		else if (ImportOptions->bImportAsSkeletalGeometry)
		{
			ImportOptions->bImportAnimations = false;
			ImportOptions->bUpdateSkeletonReferencePose = false;
		}

		if ( !FFbxImporter->ImportFromFile( *Filename, FPaths::GetExtension( Filename ), true ) )
		{
			ReapplyClothing();
			// Log the error message and fail the import.
			FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, LOCTEXT("FBXImport_ParseFailed", "FBX file parsing failed.")), FFbxErrors::Generic_FBXFileParseFailed);
		}
		else
		{
			bool bUseLODs = true;
			int32 MaxLODLevel = 0;
			TArray<FbxNode*>* MeshObject = NULL;

			//Set the build options if the BuildDat is not available so it is the same option we use to import the LOD
			if (ImportedResource && ImportedResource->LODModels.IsValidIndex(LODLevel) && !SelectedSkelMesh->IsLODImportedDataBuildAvailable(LODLevel))
			{
				FSkeletalMeshLODInfo* LODInfo = SelectedSkelMesh->GetLODInfo(LODLevel);
				if (LODInfo)
				{
					LODInfo->BuildSettings.bBuildAdjacencyBuffer = true;
					LODInfo->BuildSettings.bRecomputeNormals = !ImportOptions->ShouldImportNormals();
					LODInfo->BuildSettings.bRecomputeTangents = !ImportOptions->ShouldImportTangents();
					LODInfo->BuildSettings.bUseMikkTSpace = (ImportOptions->NormalGenerationMethod == EFBXNormalGenerationMethod::MikkTSpace) && (!ImportOptions->ShouldImportNormals() || !ImportOptions->ShouldImportTangents());
					LODInfo->BuildSettings.bComputeWeightedNormals = ImportOptions->bComputeWeightedNormals;
					LODInfo->BuildSettings.bRemoveDegenerates = ImportOptions->bRemoveDegenerates;
					LODInfo->BuildSettings.ThresholdPosition = ImportOptions->OverlappingThresholds.ThresholdPosition;
					LODInfo->BuildSettings.ThresholdTangentNormal = ImportOptions->OverlappingThresholds.ThresholdTangentNormal;
					LODInfo->BuildSettings.ThresholdUV = ImportOptions->OverlappingThresholds.ThresholdUV;
					LODInfo->BuildSettings.MorphThresholdPosition = ImportOptions->OverlappingThresholds.MorphThresholdPosition;
				}
			}

			// Populate the mesh array
			FFbxImporter->FillFbxSkelMeshArrayInScene(FFbxImporter->Scene->GetRootNode(), MeshArray, false, ImportOptions->bImportAsSkeletalGeometry || ImportOptions->bImportAsSkeletalSkinning, ImportOptions->bImportScene);

			// Nothing found, error out
			if (MeshArray.Num() == 0)
			{
				ReapplyClothing();
				FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, LOCTEXT("FBXImport_NoMesh", "No meshes were found in file.")), FFbxErrors::Generic_MeshNotFound);
				CleanUpScene();
				return false;
			}

			MeshObject = MeshArray[0];

			// check if there is LODGroup for this skeletal mesh
			for (int32 j = 0; j < MeshObject->Num(); j++)
			{
				FbxNode* Node = (*MeshObject)[j];
				if (Node->GetNodeAttribute() && Node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eLODGroup)
				{
					// get max LODgroup level
					if (MaxLODLevel < (Node->GetChildCount() - 1))
					{
						MaxLODLevel = Node->GetChildCount() - 1;
					}
				}
			}

			// No LODs found, switch to supporting a mesh array containing meshes instead of LODs
			if (MaxLODLevel == 0)
			{
				bUseLODs = false;
				MaxLODLevel = SelectedSkelMesh->GetLODNum();
			}

			int32 SelectedLOD = LODLevel;
			if (SelectedLOD > SelectedSkelMesh->GetLODNum())
			{
				ReapplyClothing();
				// Make sure they don't manage to select a bad LOD index
				FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Warning, FText::Format(LOCTEXT("FBXImport_InvalidLODIdx", "Invalid mesh LOD index {0}, no prior LOD index exists"), FText::AsNumber(SelectedLOD))), FFbxErrors::Generic_Mesh_LOD_InvalidIndex);
			}
			else
			{
				TArray<FbxNode*> SkelMeshNodeArray;

				if (bUseLODs || ImportOptions->bImportMorph)
				{
					for (int32 j = 0; j < MeshObject->Num(); j++)
					{
						FbxNode* Node = (*MeshObject)[j];
						if (Node->GetNodeAttribute() && Node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eLODGroup)
						{
							TArray<FbxNode*> NodeInLod;
							if (Node->GetChildCount() > SelectedLOD)
							{
								FFbxImporter->FindAllLODGroupNode(NodeInLod, Node, SelectedLOD);
							}
							else // in less some LODGroups have less level, use the last level
							{
								FFbxImporter->FindAllLODGroupNode(NodeInLod, Node, Node->GetChildCount() - 1);
							}

							for (FbxNode *MeshNode : NodeInLod)
							{
								SkelMeshNodeArray.Add(MeshNode);
							}
						}
						else
						{
							SkelMeshNodeArray.Add(Node);
						}
					}
				}

				// Import mesh
				USkeletalMesh* TempSkelMesh = NULL;
				TArray<FName> OrderedMaterialNames;
				{
					int32 NoneNameCount = 0;
					for (const FSkeletalMaterial &Material : SelectedSkelMesh->GetMaterials())
					{
						if (Material.ImportedMaterialSlotName == NAME_None)
							NoneNameCount++;

						OrderedMaterialNames.Add(Material.ImportedMaterialSlotName);
					}
					if (NoneNameCount >= OrderedMaterialNames.Num())
					{
						OrderedMaterialNames.Empty();
					}
				}
					
				TSharedPtr<FExistingSkelMeshData> SkelMeshDataPtr;
				if (SelectedSkelMesh->GetLODNum() > SelectedLOD)
				{
					SelectedSkelMesh->PreEditChange(NULL);
					SkelMeshDataPtr = SkeletalMeshImportUtils::SaveExistingSkelMeshData(SelectedSkelMesh, true, SelectedLOD);
				}

				//Original fbx data storage
				TArray<FName> ImportMaterialOriginalNameData;
				TArray<FImportMeshLodSectionsData> ImportMeshLodData;
				ImportMeshLodData.AddZeroed();
				FSkeletalMeshImportData OutData;

				UnFbx::FFbxImporter::FImportSkeletalMeshArgs ImportSkeletalMeshArgs;
				ImportSkeletalMeshArgs.InParent = SelectedSkelMesh->GetOutermost();
				ImportSkeletalMeshArgs.NodeArray = bUseLODs ? SkelMeshNodeArray : *MeshObject;
				ImportSkeletalMeshArgs.Name = NAME_None;
				ImportSkeletalMeshArgs.Flags = RF_Transient;
				ImportSkeletalMeshArgs.TemplateImportData = TempAssetImportData;
				ImportSkeletalMeshArgs.LodIndex = SelectedLOD;
				ImportSkeletalMeshArgs.OrderedMaterialNames = OrderedMaterialNames.Num() > 0 ? &OrderedMaterialNames : nullptr;
				ImportSkeletalMeshArgs.ImportMaterialOriginalNameData = &ImportMaterialOriginalNameData;
				ImportSkeletalMeshArgs.ImportMeshSectionsData = &ImportMeshLodData[0];
				ImportSkeletalMeshArgs.OutData = &OutData;

				TempSkelMesh = (USkeletalMesh*)FFbxImporter->ImportSkeletalMesh( ImportSkeletalMeshArgs );
				// Add the new imported LOD to the existing model (check skeleton compatibility)
				if( TempSkelMesh  && FFbxImporter->ImportSkeletalMeshLOD(TempSkelMesh, SelectedSkelMesh, SelectedLOD, TempAssetImportData))
				{
					//Update the import data for this lod
					UnFbx::FFbxImporter::UpdateSkeletalMeshImportData(SelectedSkelMesh, nullptr, SelectedLOD, &ImportMaterialOriginalNameData, &ImportMeshLodData);

					if (SkelMeshDataPtr)
					{
						SkeletalMeshImportUtils::RestoreExistingSkelMeshData(SkelMeshDataPtr, SelectedSkelMesh, SelectedLOD, false, ImportOptions->bImportAsSkeletalSkinning, ImportOptions->bResetToFbxOnMaterialConflict);
					}

					if (ImportOptions->bImportMorph)
					{
						FFbxImporter->ImportFbxMorphTarget(SkelMeshNodeArray, SelectedSkelMesh, SelectedLOD, OutData);
					}

					bSuccess = true;

					// Set LOD source filename
					SelectedSkelMesh->GetLODInfo(SelectedLOD)->SourceImportFilename = UAssetImportData::SanitizeImportFilename(Filename, nullptr);
					SelectedSkelMesh->GetLODInfo(SelectedLOD)->bImportWithBaseMesh = false;

					ReapplyClothing();

					//Must be the last step because it cleanup the fbx importer to import the alternate skinning FBX
					if (bMustReimportAlternateSkinWeightProfile)
					{
						//We cannot use anymore the FFbxImporter after the cleanup
						CleanUpScene();
						FSkinWeightsUtilities::ReimportAlternateSkinWeight(SelectedSkelMesh, SelectedLOD);
					}

					// Notification of success
					FNotificationInfo NotificationInfo(FText::GetEmpty());
					NotificationInfo.Text = FText::Format(NSLOCTEXT("UnrealEd", "LODImportSuccessful", "Mesh for LOD {0} imported successfully!"), FText::AsNumber(SelectedLOD));
					NotificationInfo.ExpireDuration = 5.0f;
					FSlateNotificationManager::Get().AddNotification(NotificationInfo);
				}
				else
				{
					ReapplyClothing();
					// Notification of failure
					FNotificationInfo NotificationInfo(FText::GetEmpty());
					NotificationInfo.Text = FText::Format(NSLOCTEXT("UnrealEd", "LODImportFail", "Failed to import mesh for LOD {0}!"), FText::AsNumber(SelectedLOD));
					NotificationInfo.ExpireDuration = 5.0f;
					FSlateNotificationManager::Get().AddNotification(NotificationInfo);
				}
			}
		}
		CleanUpScene();
		return bSuccess;
	}

	FString PromptForLODImportFile(const FText& PromptTitle)
	{
		FString ChosenFilname("");

		FString ExtensionStr;
		ExtensionStr += TEXT("All model files|*.fbx;*.obj|");
		ExtensionStr += TEXT("FBX files|*.fbx|");
		ExtensionStr += TEXT("Object files|*.obj|");
		ExtensionStr += TEXT("All files|*.*");

		// First, display the file open dialog for selecting the file.
		TArray<FString> OpenFilenames;
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		bool bOpen = false;
		if(DesktopPlatform)
		{
			bOpen = DesktopPlatform->OpenFileDialog(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				PromptTitle.ToString(),
				*FEditorDirectories::Get().GetLastDirectory(ELastDirectory::FBX),
				TEXT(""),
				*ExtensionStr,
				EFileDialogFlags::None,
				OpenFilenames
				);
		}

		// Only continue if we pressed OK and have only one file selected.
		if(bOpen)
		{
			if(OpenFilenames.Num() == 0)
			{
				UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
				FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, LOCTEXT("NoFileSelectedForLOD", "No file was selected for the LOD.")), FFbxErrors::Generic_Mesh_LOD_NoFileSelected);
			}
			else if(OpenFilenames.Num() > 1)
			{
				UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
				FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, LOCTEXT("MultipleFilesSelectedForLOD", "You may only select one file for the LOD.")), FFbxErrors::Generic_Mesh_LOD_MultipleFilesSelected);
			}
			else
			{
				ChosenFilname = OpenFilenames[0];
				FEditorDirectories::Get().SetLastDirectory(ELastDirectory::FBX, FPaths::GetPath(ChosenFilname)); // Save path as default for next time.
			}
		}
		
		return ChosenFilname;
	}

	bool ImportMeshLODDialog(class UObject* SelectedMesh, int32 LODLevel, bool bNotifyCB /*= true*/)
	{
		if(!SelectedMesh)
		{
			return false;
		}

		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(SelectedMesh);
		UStaticMesh* StaticMesh = Cast<UStaticMesh>(SelectedMesh);

		if( !SkeletalMesh && !StaticMesh )
		{
			return false;
		}

		FString FilenameToImport("");

		if(SkeletalMesh)
		{
			if(SkeletalMesh->IsValidLODIndex(LODLevel))
			{
				FilenameToImport = SkeletalMesh->GetLODInfo(LODLevel)->SourceImportFilename.IsEmpty() ?
					SkeletalMesh->GetLODInfo(LODLevel)->SourceImportFilename :
					UAssetImportData::ResolveImportFilename(SkeletalMesh->GetLODInfo(LODLevel)->SourceImportFilename, nullptr);
			}
		}
		else if (StaticMesh)
		{
			if (StaticMesh->IsSourceModelValid(LODLevel))
			{
				const FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(LODLevel);
				FilenameToImport = SourceModel.SourceImportFilename.IsEmpty() ?
					SourceModel.SourceImportFilename :
					UAssetImportData::ResolveImportFilename(SourceModel.SourceImportFilename, nullptr);
			}
		}

		// Check the file exists first
		const bool bSourceFileExists = FPaths::FileExists(FilenameToImport);
		// We'll give the user a chance to choose a new file if a previously set file fails to import
		const bool bPromptOnFail = bSourceFileExists;
		
		if(!bSourceFileExists || FilenameToImport.IsEmpty())
		{
			FText PromptTitle;

			if(FilenameToImport.IsEmpty())
			{
				PromptTitle = FText::Format(LOCTEXT("LODImportPrompt_NoSource", "Choose a file to import for LOD {0}"), FText::AsNumber(LODLevel));
			}
			else if(!bSourceFileExists)
			{
				PromptTitle = FText::Format(LOCTEXT("LODImportPrompt_SourceNotFound", "LOD {0} Source file not found. Choose new file."), FText::AsNumber(LODLevel));
			}

			FilenameToImport = PromptForLODImportFile(PromptTitle);
		}
		
		bool bImportSuccess = false;

		if(!FilenameToImport.IsEmpty())
		{
			if(SkeletalMesh)
			{
				bImportSuccess = ImportSkeletalMeshLOD(SkeletalMesh, FilenameToImport, LODLevel);
			}
			else if(StaticMesh)
			{
				bImportSuccess = ImportStaticMeshLOD(StaticMesh, FilenameToImport, LODLevel);
			}
		}

		if(!bImportSuccess && bPromptOnFail)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("LODImport_SourceMissingDialog", "Failed to import LOD{0} as the source file failed to import, please select a new source file."), FText::AsNumber(LODLevel)));

			FText PromptTitle = FText::Format(LOCTEXT("LODImportPrompt_SourceFailed", "Failed to import source file for LOD {0}, choose a new file"), FText::AsNumber(LODLevel));
			FilenameToImport = PromptForLODImportFile(PromptTitle);

			if(FilenameToImport.Len() > 0 && FPaths::FileExists(FilenameToImport))
			{
				if(SkeletalMesh)
				{
					bImportSuccess = ImportSkeletalMeshLOD(SkeletalMesh, FilenameToImport, LODLevel);
				}
				else if(StaticMesh)
				{
					bImportSuccess = ImportStaticMeshLOD(StaticMesh, FilenameToImport, LODLevel);
				}
			}
		}

		//If the filename is empty it mean the user cancel the file selection
		if(!bImportSuccess && !FilenameToImport.IsEmpty())
		{
			// Failed to import a LOD, even after retries (if applicable)
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("LODImport_Failure", "Failed to import LOD{0}"), FText::AsNumber(LODLevel)));
		}

		if (bImportSuccess && bNotifyCB)
		{
			if (SkeletalMesh)
			{
				GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostLODImport(SkeletalMesh, LODLevel);
			}				
			else if(StaticMesh)
			{
				GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostLODImport(StaticMesh, LODLevel);
			}
		}

		return bImportSuccess;
	}

	void SetImportOption(UFbxImportUI* ImportUI)
	{
		UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
		UnFbx::FBXImportOptions* ImportOptions = FFbxImporter->GetImportOptions();
		ApplyImportUIToImportOptions(ImportUI, *ImportOptions);
	}
}  //end namespace MeshUtils

#undef LOCTEXT_NAMESPACE
