// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SkinWeightsUtilities.h"
#include "LODUtilities.h"
#include "Modules/ModuleManager.h"
#include "Components/SkinnedMeshComponent.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Engine/SkeletalMesh.h"
#include "EditorFramework/AssetImportData.h"
#include "MeshUtilities.h"

#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxTextureImportData.h"
#include "Factories/FbxImportUI.h"
#include "AssetRegistryModule.h"
#include "ObjectTools.h"
#include "AssetImportTask.h"
#include "FbxImporter.h"
#include "ScopedTransaction.h"

#include "ComponentReregisterContext.h"
#include "Animation/SkinWeightProfile.h"

#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "Framework/Application/SlateApplication.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkinWeightsUtilities, Log, All);

bool FSkinWeightsUtilities::ImportAlternateSkinWeight(USkeletalMesh* SkeletalMesh, FString Path, int32 TargetLODIndex, const FName& ProfileName)
{
	check(SkeletalMesh);
	check(SkeletalMesh->GetLODInfo(TargetLODIndex));
	FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(TargetLODIndex);
	
	if (LODInfo && LODInfo->bHasBeenSimplified && LODInfo->ReductionSettings.BaseLOD != TargetLODIndex)
	{
		//We cannot remove alternate skin weights profile for a generated LOD
		UE_LOG(LogSkinWeightsUtilities, Error, TEXT("Cannot import Skin Weight Profile for a generated LOD."));
		return false;
	}

	FString AbsoluteFilePath = UAssetImportData::ResolveImportFilename(Path, SkeletalMesh->GetOutermost());
	if (!FPaths::FileExists(AbsoluteFilePath))
	{
		UE_LOG(LogSkinWeightsUtilities, Error, TEXT("Path containing Skin Weight Profile data does not exist (%s)."), *Path);
		return false;
	}
	FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(SkeletalMesh);
	FScopedSkeletalMeshPostEditChange ScopePostEditChange(SkeletalMesh);

	UnFbx::FBXImportOptions ImportOptions;
	//Import the alternate fbx into a temporary skeletal mesh using the same import options
	UFbxFactory* FbxFactory = NewObject<UFbxFactory>(UFbxFactory::StaticClass());
	FbxFactory->AddToRoot();

	FbxFactory->ImportUI = NewObject<UFbxImportUI>(FbxFactory);
	UFbxSkeletalMeshImportData* OriginalSkeletalMeshImportData = UFbxSkeletalMeshImportData::GetImportDataForSkeletalMesh(SkeletalMesh, nullptr);
	if (OriginalSkeletalMeshImportData != nullptr)
	{
		//Copy the skeletal mesh import data options
		FbxFactory->ImportUI->SkeletalMeshImportData = DuplicateObject<UFbxSkeletalMeshImportData>(OriginalSkeletalMeshImportData, FbxFactory);
	}
	//Skip the auto detect type on import, the test set a specific value
	FbxFactory->SetDetectImportTypeOnImport(false);
	FbxFactory->ImportUI->bImportAsSkeletal = true;
	FbxFactory->ImportUI->MeshTypeToImport = FBXIT_SkeletalMesh;
	FbxFactory->ImportUI->bIsReimport = false;
	FbxFactory->ImportUI->ReimportMesh = nullptr;
	FbxFactory->ImportUI->bAllowContentTypeImport = true;
	FbxFactory->ImportUI->bImportAnimations = false;
	FbxFactory->ImportUI->bAutomatedImportShouldDetectType = false;
	FbxFactory->ImportUI->bCreatePhysicsAsset = false;
	FbxFactory->ImportUI->bImportMaterials = false;
	FbxFactory->ImportUI->bImportTextures = false;
	FbxFactory->ImportUI->bImportMesh = true;
	FbxFactory->ImportUI->bImportRigidMesh = false;
	FbxFactory->ImportUI->bIsObjImport = false;
	FbxFactory->ImportUI->bOverrideFullName = true;
	FbxFactory->ImportUI->Skeleton = nullptr;
	
	//Force some skeletal mesh import options
	if (FbxFactory->ImportUI->SkeletalMeshImportData)
	{
		FbxFactory->ImportUI->SkeletalMeshImportData->bImportMeshLODs = false;
		FbxFactory->ImportUI->SkeletalMeshImportData->bImportMorphTargets = false;
		FbxFactory->ImportUI->SkeletalMeshImportData->bUpdateSkeletonReferencePose = false;
		FbxFactory->ImportUI->SkeletalMeshImportData->ImportContentType = EFBXImportContentType::FBXICT_All; //We need geo and skinning, so we can match the weights
	}
	//Force some material options
	if (FbxFactory->ImportUI->TextureImportData)
	{
		FbxFactory->ImportUI->TextureImportData->MaterialSearchLocation = EMaterialSearchLocation::Local;
		FbxFactory->ImportUI->TextureImportData->BaseMaterialName.Reset();
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	FString ImportAssetPath = TEXT("/Engine/TempEditor/SkeletalMeshTool");
	//Empty the temporary path
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	
	auto DeletePathAssets = [&AssetRegistryModule, &ImportAssetPath]()
	{
		TArray<FAssetData> AssetsToDelete;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*ImportAssetPath), AssetsToDelete, true);
		ObjectTools::DeleteAssets(AssetsToDelete, false);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	};

	DeletePathAssets();

	ApplyImportUIToImportOptions(FbxFactory->ImportUI, ImportOptions);

	TArray<FString> ImportFilePaths;
	ImportFilePaths.Add(AbsoluteFilePath);

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->AddToRoot();
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->DestinationPath = ImportAssetPath;
	Task->bSave = false;
	Task->DestinationName = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Task->Options = FbxFactory->ImportUI->SkeletalMeshImportData;
	Task->Filename = AbsoluteFilePath;
	Task->Factory = FbxFactory;
	FbxFactory->SetAssetImportTask(Task);
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	UObject* ImportedObject = nullptr;
	
	for (FString AssetPath : Task->ImportedObjectPaths)
	{
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FName(*AssetPath));
		ImportedObject = AssetData.GetAsset();
		if (ImportedObject != nullptr)
		{
			break;
		}
	}
	
	//Factory and task can now be garbage collected
	Task->RemoveFromRoot();
	FbxFactory->RemoveFromRoot();

	USkeletalMesh* TmpSkeletalMesh = Cast<USkeletalMesh>(ImportedObject);
	if (TmpSkeletalMesh == nullptr || TmpSkeletalMesh->Skeleton == nullptr)
	{
		UE_LOG(LogSkinWeightsUtilities, Error, TEXT("Failed to import Skin Weight Profile from provided FBX file (%s)."), *Path);
		DeletePathAssets();
		return false;
	}

	//The LOD index of the source is always 0, 
	const int32 SrcLodIndex = 0;
	bool bResult = false;

	if (SkeletalMesh && TmpSkeletalMesh)
	{
		if (FSkeletalMeshModel* TargetModel = SkeletalMesh->GetImportedModel())
		{
			if (TargetModel->LODModels.IsValidIndex(TargetLODIndex))
			{
				//Prepare the profile data
				FSkeletalMeshLODModel& TargetLODModel = TargetModel->LODModels[TargetLODIndex];
				
				// Prepare the profile data
				FSkinWeightProfileInfo* Profile = SkeletalMesh->GetSkinWeightProfiles().FindByPredicate([ProfileName](FSkinWeightProfileInfo Profile) { return Profile.Name == ProfileName; });

				const bool bIsReimport = Profile != nullptr;
				FText TransactionName = bIsReimport ? NSLOCTEXT("UnrealEd", "UpdateAlternateSkinningWeight", "Update Alternate Skinning Weight")
					: NSLOCTEXT("UnrealEd", "ImportAlternateSkinningWeight", "Import Alternate Skinning Weight");
				FScopedTransaction ScopedTransaction(TransactionName);
				SkeletalMesh->Modify();

				if (bIsReimport)
				{
					// Update source file path
					FString& StoredPath = Profile->PerLODSourceFiles.FindOrAdd(TargetLODIndex);
					StoredPath = UAssetImportData::SanitizeImportFilename(AbsoluteFilePath, SkeletalMesh->GetOutermost());
					Profile->PerLODSourceFiles.KeySort([](int32 A, int32 B) { return A < B; });
				}
				
				// Clear profile data before import
				FImportedSkinWeightProfileData& ProfileData = TargetLODModel.SkinWeightProfiles.FindOrAdd(ProfileName);
				ProfileData.SkinWeights.Empty();
				ProfileData.SourceModelInfluences.Empty();

				FImportedSkinWeightProfileData PreviousProfileData = ProfileData;
				
				FOverlappingThresholds OverlappingThresholds = ImportOptions.OverlappingThresholds;
				bool ShouldImportNormals = ImportOptions.ShouldImportNormals();
				bool ShouldImportTangents = ImportOptions.ShouldImportTangents();
				bool bUseMikkTSpace = ImportOptions.NormalGenerationMethod == EFBXNormalGenerationMethod::MikkTSpace;

				TArray<FRawSkinWeight>& SkinWeights = ProfileData.SkinWeights;
				bResult = FLODUtilities::UpdateAlternateSkinWeights(SkeletalMesh, ProfileName, TmpSkeletalMesh, TargetLODIndex, SrcLodIndex, OverlappingThresholds, ShouldImportNormals, ShouldImportTangents, bUseMikkTSpace, ImportOptions.bComputeWeightedNormals);
				
				if (!bResult)
				{
					// Remove invalid profile data due to failed import
					if (!bIsReimport)
					{
						TargetLODModel.SkinWeightProfiles.Remove(ProfileName);
					}
					else
					{
						// Otherwise restore previous data
						ProfileData = PreviousProfileData;
					}
				}

				// Only add if it is an initial import and it was successful 
				if (!bIsReimport && bResult)
				{
					FSkinWeightProfileInfo SkeletalMeshProfile;
					SkeletalMeshProfile.DefaultProfile = (SkeletalMesh->GetNumSkinWeightProfiles() == 0);
					SkeletalMeshProfile.DefaultProfileFromLODIndex = TargetLODIndex;
					SkeletalMeshProfile.Name = ProfileName;
					SkeletalMeshProfile.PerLODSourceFiles.Add(TargetLODIndex, UAssetImportData::SanitizeImportFilename(AbsoluteFilePath, SkeletalMesh->GetOutermost()));
					SkeletalMesh->AddSkinWeightProfile(SkeletalMeshProfile);

					Profile = &SkeletalMeshProfile;
				}
			}
		}
	}
	
	//Make sure all created objects are gone
	DeletePathAssets();

	return bResult;
}

bool FSkinWeightsUtilities::ReimportAlternateSkinWeight(USkeletalMesh* SkeletalMesh, int32 TargetLODIndex)
{
	bool bResult = false;

	const TArray<FSkinWeightProfileInfo>& SkinWeightProfiles = SkeletalMesh->GetSkinWeightProfiles();
	if (SkinWeightProfiles.Num() <= 0)
	{
		return bResult;
	}
	FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(SkeletalMesh);
	FScopedSkeletalMeshPostEditChange ScopePostEditChange(SkeletalMesh);

	for (int32 ProfileIndex = 0; ProfileIndex < SkinWeightProfiles.Num(); ++ProfileIndex)
	{
		const FSkinWeightProfileInfo& ProfileInfo = SkinWeightProfiles[ProfileIndex];

		const FString* PathNamePtr = ProfileInfo.PerLODSourceFiles.Find(TargetLODIndex);
		//Skip profile that do not have data for TargetLODIndex
		if (!PathNamePtr)
		{
			continue;
		}

		const FString& PathName = *PathNamePtr;

		if (FPaths::FileExists(PathName))
		{
			bResult |= FSkinWeightsUtilities::ImportAlternateSkinWeight(SkeletalMesh, PathName, TargetLODIndex, ProfileInfo.Name);
		}
		else
		{
			const FString PickedFileName = FSkinWeightsUtilities::PickSkinWeightFBXPath(TargetLODIndex);
			if (!PickedFileName.IsEmpty() && FPaths::FileExists(PickedFileName))
			{
				bResult |= FSkinWeightsUtilities::ImportAlternateSkinWeight(SkeletalMesh, PickedFileName, TargetLODIndex, ProfileInfo.Name);
			}
		}
	}

	
	if (bResult)
	{
		FLODUtilities::RegenerateDependentLODs(SkeletalMesh, TargetLODIndex);
	}
	
	return bResult;
}

bool FSkinWeightsUtilities::RemoveSkinnedWeightProfileData(USkeletalMesh* SkeletalMesh, const FName& ProfileName, int32 LODIndex)
{
	check(SkeletalMesh);
	check(SkeletalMesh->GetImportedModel());
	check(SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LODIndex));
	FSkeletalMeshLODModel& LODModelDest = SkeletalMesh->GetImportedModel()->LODModels[LODIndex];
	LODModelDest.SkinWeightProfiles.Remove(ProfileName);

	FSkeletalMeshImportData ImportDataDest;
	LODModelDest.RawSkeletalMeshBulkData.LoadRawMesh(ImportDataDest);

	//Rechunk the skeletal mesh since we remove it, we rebuild the skeletal mesh to achieve rechunking
	UFbxSkeletalMeshImportData* OriginalSkeletalMeshImportData = UFbxSkeletalMeshImportData::GetImportDataForSkeletalMesh(SkeletalMesh, nullptr);

	TArray<FVector> LODPointsDest;
	TArray<SkeletalMeshImportData::FMeshWedge> LODWedgesDest;
	TArray<SkeletalMeshImportData::FMeshFace> LODFacesDest;
	TArray<SkeletalMeshImportData::FVertInfluence> LODInfluencesDest;
	TArray<int32> LODPointToRawMapDest;
	ImportDataDest.CopyLODImportData(LODPointsDest, LODWedgesDest, LODFacesDest, LODInfluencesDest, LODPointToRawMapDest);

	const bool bShouldImportNormals = OriginalSkeletalMeshImportData->NormalImportMethod == FBXNIM_ImportNormals || OriginalSkeletalMeshImportData->NormalImportMethod == FBXNIM_ImportNormalsAndTangents;
	const bool bShouldImportTangents = OriginalSkeletalMeshImportData->NormalImportMethod == FBXNIM_ImportNormalsAndTangents;
	//Set the options with the current asset build options
	IMeshUtilities::MeshBuildOptions BuildOptions;
	BuildOptions.OverlappingThresholds.ThresholdPosition = OriginalSkeletalMeshImportData->ThresholdPosition;
	BuildOptions.OverlappingThresholds.ThresholdTangentNormal = OriginalSkeletalMeshImportData->ThresholdTangentNormal;
	BuildOptions.OverlappingThresholds.ThresholdUV = OriginalSkeletalMeshImportData->ThresholdUV;
	BuildOptions.bComputeNormals = !bShouldImportNormals || !ImportDataDest.bHasNormals;
	BuildOptions.bComputeTangents = !bShouldImportTangents || !ImportDataDest.bHasTangents;
	BuildOptions.bUseMikkTSpace = (OriginalSkeletalMeshImportData->NormalGenerationMethod == EFBXNormalGenerationMethod::MikkTSpace) && (!bShouldImportNormals || !bShouldImportTangents);
	BuildOptions.bComputeWeightedNormals = OriginalSkeletalMeshImportData->bComputeWeightedNormals;
	BuildOptions.bRemoveDegenerateTriangles = false;

	//Build the skeletal mesh asset
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
	TArray<FText> WarningMessages;
	TArray<FName> WarningNames;
	//Build the destination mesh with the Alternate influences, so the chunking is done properly.
	const bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(LODModelDest, SkeletalMesh->RefSkeleton, LODInfluencesDest, LODWedgesDest, LODFacesDest, LODPointsDest, LODPointToRawMapDest, BuildOptions, &WarningMessages, &WarningNames);
	FLODUtilities::RegenerateAllImportSkinWeightProfileData(LODModelDest);

	return bBuildSuccess;
}

FString FSkinWeightsUtilities::PickSkinWeightFBXPath(int32 LODIndex)
{
	FString PickedFileName("");

	FString ExtensionStr;
	ExtensionStr += TEXT("FBX files|*.fbx|");

	// First, display the file open dialog for selecting the file.
	TArray<FString> OpenFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bOpen = false;
	if (DesktopPlatform)
	{
		const FString DialogTitle = TEXT("Pick FBX file containing Skin Weight data for LOD ") + FString::FormatAsNumber(LODIndex);
		bOpen = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			DialogTitle,
			*FEditorDirectories::Get().GetLastDirectory(ELastDirectory::FBX),
			TEXT(""),
			*ExtensionStr,
			EFileDialogFlags::None,
			OpenFilenames
		);
	}

	if (bOpen)
	{
		if (OpenFilenames.Num() == 1)
		{
			PickedFileName = OpenFilenames[0];
			// Set last directory path for FBX files
			FEditorDirectories::Get().SetLastDirectory(ELastDirectory::FBX, FPaths::GetPath(PickedFileName));
		}
		else
		{
			// Error
		}
	}

	return PickedFileName;
}