// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMeshFactory.h"

#include "NaniteDisplacedMesh.h"
#include "NaniteDisplacedMeshEditorModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DerivedDataBuildVersion.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Misc/StringBuilder.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "NaniteDisplacedMeshEditor"

DEFINE_LOG_CATEGORY_STATIC(LogNaniteDisplacedMesh, Log, All);

#define NANITE_DISPLACED_MESH_ID_VERSION 1

UNaniteDisplacedMeshFactory::UNaniteDisplacedMeshFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UNaniteDisplacedMesh::StaticClass();
}

UNaniteDisplacedMesh* UNaniteDisplacedMeshFactory::StaticFactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return static_cast<UNaniteDisplacedMesh*>(NewObject<UNaniteDisplacedMesh>(InParent, Class, Name, Flags | RF_Transactional | RF_Public | RF_Standalone));
}

UObject* UNaniteDisplacedMeshFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UNaniteDisplacedMesh* NewNaniteDisplacedMesh = StaticFactoryCreateNew(Class, InParent, Name, Flags, Context, Warn);
	NewNaniteDisplacedMesh->bIsEditable = !bCreateReadOnlyAsset;
	NewNaniteDisplacedMesh->MarkPackageDirty();
	return NewNaniteDisplacedMesh;
}

UNaniteDisplacedMesh* LinkDisplacedMeshAsset(UNaniteDisplacedMesh* ExistingDisplacedMesh, const FNaniteDisplacedMeshParams& InParameters, const FString& DisplacedMeshFolder, bool bCreateTransientAsset)
{
	checkf(GEditor, TEXT("There is no need to run that code if we don't have the editor"));

	// We always need a valid base mesh for displacement, and non-zero magnitude on at least one displacement map
	bool bApplyDisplacement = false;
	for( auto& DisplacementMap : InParameters.DisplacementMaps )
	{
		bApplyDisplacement = bApplyDisplacement || (DisplacementMap.Magnitude > 0.0f && IsValid(DisplacementMap.Texture));
	}

	if (!IsValid(InParameters.BaseMesh) || !bApplyDisplacement || InParameters.RelativeError <= 0.0f)
	{
		return nullptr;
	}

	if (IsValid(ExistingDisplacedMesh) && (bCreateTransientAsset || (!ExistingDisplacedMesh->HasAnyFlags(RF_Transient) && ExistingDisplacedMesh->HasAnyFlags(RF_Public))))
	{
		// Make sure the referenced displaced mesh asset matches the provided combination
		// Note: This is a faster test than generating Ids for LHS and RHS and comparing (this check will occur frequently)
		if (ExistingDisplacedMesh->Parameters == InParameters)
		{
			return ExistingDisplacedMesh;
		}
	}

	// Either the displaced mesh asset is stale (wrong permutation), or it is null.
	// In either case, find or create the correct displaced mesh asset permutation.
	TStringBuilder<512> StringBuilder;

	StringBuilder.Append(TEXT("NaniteDisplacedMesh_"));
	StringBuilder.Append(GetAggregatedIdString(InParameters));
	FString DisplacedMeshName = StringBuilder.ToString();
	StringBuilder.Reset();

	// Generate unique asset path
	FString DisplacedAssetPath = FPaths::Combine(DisplacedMeshFolder, DisplacedMeshName);

	// The mesh needed might already exist. Using load object because it's faster then using the asset registry which might still be loading
	if (UNaniteDisplacedMesh* LoadedDisplacedMesh = LoadObject<UNaniteDisplacedMesh>(nullptr, *DisplacedAssetPath, nullptr, LOAD_Quiet))
	{
		// Finish loading the object if needed
		if (LoadedDisplacedMesh->HasAnyFlags(RF_NeedLoad))
		{
			if (FLinkerLoad* TextureLinker = LoadedDisplacedMesh->GetLinker())
			{
				TextureLinker->Preload(LoadedDisplacedMesh);
			}
		}

		LoadedDisplacedMesh->ConditionalPostLoad();


		// The asset path may match, but someone could have (incorrectly) directly modified the parameters
		// on the displaced mesh asset.
		if (LoadedDisplacedMesh->Parameters == InParameters)
		{
			return LoadedDisplacedMesh;
		}
		else
		{
			FString LoadedDisplacedMeshId = GetAggregatedIdString(LoadedDisplacedMesh->Parameters);

			UE_LOG(
				LogNaniteDisplacedMesh,
				Error,
				TEXT("The NaniteDisplacementMesh parameters doesn't match the guid from its name (Current parameters: %s). Updating parameters of (%s). Consider saving the displaced mesh again to remove this error."),
				*LoadedDisplacedMeshId,
				*(LoadedDisplacedMesh->GetPathName())
			);

			// If this check assert we will need to update how we generate the id because we have a hash collision.
			check(LoadedDisplacedMeshId != GetAggregatedIdString(InParameters));

			LoadedDisplacedMesh->PreEditChange(nullptr);
			LoadedDisplacedMesh->Parameters = InParameters;
			LoadedDisplacedMesh->bIsEditable = false;
			LoadedDisplacedMesh->PostEditChange();


			return LoadedDisplacedMesh;
		}
	}

	if (bCreateTransientAsset)
	{
		UPackage* NaniteDisplacedMeshTransientPackage = FNaniteDisplacedMeshEditorModule::GetModule().GetNaniteDisplacementMeshTransientPackage();

		// First check if we already have a valid temp asset
		{
			UObject* PotentialTempAsset = FindObject<UObject>(NaniteDisplacedMeshTransientPackage, *DisplacedMeshName);

			if (IsValid(PotentialTempAsset))
			{
				if (UNaniteDisplacedMesh* TempNaniteDisplacedMesh = Cast<UNaniteDisplacedMesh>(PotentialTempAsset))
				{
					return TempNaniteDisplacedMesh;
				}
			}

			// Remove the invalid asset of the way (We don't want to deal with recycled objects)
			if (PotentialTempAsset)
			{
				PotentialTempAsset->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
			}
		}

		// Create a temp asset
		UNaniteDisplacedMesh* TempNaniteDisplacedMesh = UNaniteDisplacedMeshFactory::StaticFactoryCreateNew(
			UNaniteDisplacedMesh::StaticClass(),
			NaniteDisplacedMeshTransientPackage,
			*DisplacedMeshName,
			RF_Transactional | RF_Transient,
			nullptr,
			nullptr
			);

		// We want the garbage collector to be able to clean the temp assets when they are no longer referred
		TempNaniteDisplacedMesh->ClearFlags(RF_Standalone);
		TempNaniteDisplacedMesh->bIsEditable = false;
		TempNaniteDisplacedMesh->Parameters = InParameters;
		TempNaniteDisplacedMesh->PostEditChange();
		return TempNaniteDisplacedMesh;
	}
	else
	{
		// We need to create a new asset
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

		TStrongObjectPtr<UNaniteDisplacedMeshFactory> DisplacedMeshFactory(NewObject<UNaniteDisplacedMeshFactory>());
		DisplacedMeshFactory->bCreateReadOnlyAsset = true;
		if (UObject* Asset = AssetTools.CreateAsset(DisplacedMeshName, DisplacedMeshFolder, UNaniteDisplacedMesh::StaticClass(), DisplacedMeshFactory.Get()))
		{
			UNaniteDisplacedMesh* NewDisplacedMesh = CastChecked<UNaniteDisplacedMesh>(Asset);
			NewDisplacedMesh->Parameters = InParameters;

			if (UEditorLoadingAndSavingUtils::SavePackages({ NewDisplacedMesh->GetPackage() }, /*bOnlyDirty=*/ false))
			{
				NewDisplacedMesh->PostEditChange();
				return NewDisplacedMesh;
			}
		}
		else
		{
			UE_LOG(
				LogNaniteDisplacedMesh,
				Error,
				TEXT("Failed to create asset for %s in folder %s. Consult log for more details"),
				*DisplacedMeshName,
				*DisplacedMeshFolder
			);
		}
	}

	return nullptr;
}

FGuid GetAggregatedId(const FNaniteDisplacedMeshParams& DisplacedMeshParams)
{
	UE::DerivedData::FBuildVersionBuilder IdBuilder;

	IdBuilder << NANITE_DISPLACED_MESH_ID_VERSION;

	IdBuilder << DisplacedMeshParams.RelativeError;

	if (IsValid(DisplacedMeshParams.BaseMesh))
	{
		IdBuilder << DisplacedMeshParams.BaseMesh->GetPackage()->GetPersistentGuid();
	}

	for( auto& DisplacementMap : DisplacedMeshParams.DisplacementMaps )
	{
		if (IsValid(DisplacementMap.Texture))
		{
			IdBuilder << DisplacementMap.Texture->GetPackage()->GetPersistentGuid();
		}

		IdBuilder << DisplacementMap.Magnitude;
		IdBuilder << DisplacementMap.Center;
	}

	return IdBuilder.Build();
}

FGuid GetAggregatedId(const UNaniteDisplacedMesh& DisplacedMesh)
{
	return GetAggregatedId(DisplacedMesh.Parameters);
}

FString GetAggregatedIdString(const FNaniteDisplacedMeshParams& DisplacedMeshParams)
{
	return GetAggregatedId(DisplacedMeshParams).ToString();
}

FString GetAggregatedIdString(const UNaniteDisplacedMesh& DisplacedMesh)
{
	return GetAggregatedId(DisplacedMesh).ToString();
}

#undef LOCTEXT_NAMESPACE