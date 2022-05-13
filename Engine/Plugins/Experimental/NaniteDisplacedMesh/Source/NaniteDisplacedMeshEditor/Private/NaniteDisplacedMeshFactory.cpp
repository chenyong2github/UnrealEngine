// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMeshFactory.h"
#include "NaniteDisplacedMesh.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "DerivedDataBuildVersion.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/StringBuilder.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "FileHelpers.h"

#define LOCTEXT_NAMESPACE "NaniteDisplacedMeshEditor"

DEFINE_LOG_CATEGORY_STATIC(LogNaniteDisplacedMesh, Log, All);

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

UNaniteDisplacedMesh* LinkDisplacedMeshAsset(UNaniteDisplacedMesh* ExistingDisplacedMesh, const FNaniteDisplacedMeshParams& InParameters, const FString& DisplacedMeshFolder)
{
	// We always need a valid base mesh for displacement, and non-zero magnitude on at least one displacement map
	const bool bApplyDisplacement =
		(InParameters.Magnitude1 > 0.0f && IsValid(InParameters.DisplacementMap1)) ||
		(InParameters.Magnitude2 > 0.0f && IsValid(InParameters.DisplacementMap2)) ||
		(InParameters.Magnitude3 > 0.0f && IsValid(InParameters.DisplacementMap3)) ||
		(InParameters.Magnitude4 > 0.0f && IsValid(InParameters.DisplacementMap4));

	if (!IsValid(InParameters.BaseMesh) || !bApplyDisplacement)
	{
		return nullptr;
	}

	if (IsValid(ExistingDisplacedMesh))
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

	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
	if (EditorAssetSubsystem->DoesAssetExist(DisplacedAssetPath))
	{
		// The Nanite displaced mesh permutation needed already exists.
		UObject* LoadedObject = EditorAssetSubsystem->LoadAsset(DisplacedAssetPath);
		if (UNaniteDisplacedMesh* LoadedDisplacedMesh = Cast<UNaniteDisplacedMesh>(LoadedObject))
		{
			// The asset path may match, but someone could have (incorrectly) directly modified the parameters
			// on the displaced mesh asset.
			if (LoadedDisplacedMesh->Parameters == InParameters)
			//if (GetAggregatedId(*LoadedDisplacedMesh) == GetAggregatedId(InParameters))
			{
				return LoadedDisplacedMesh;
			}
		}

		// Existing asset was wrong type, or the IDs don't match; sanitize
		bool bDeleteOK = EditorAssetSubsystem->DeleteAsset(DisplacedAssetPath);
		ensure(bDeleteOK);
	}

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

	return nullptr;
}

FGuid GetAggregatedId(const FNaniteDisplacedMeshParams& DisplacedMeshParams)
{
	UE::DerivedData::FBuildVersionBuilder IdBuilder;

	IdBuilder << DisplacedMeshParams.TessellationLevel;

	IdBuilder << DisplacedMeshParams.Magnitude1;
	IdBuilder << DisplacedMeshParams.Magnitude2;
	IdBuilder << DisplacedMeshParams.Magnitude3;
	IdBuilder << DisplacedMeshParams.Magnitude4;

	IdBuilder << DisplacedMeshParams.Bias1;
	IdBuilder << DisplacedMeshParams.Bias2;
	IdBuilder << DisplacedMeshParams.Bias3;
	IdBuilder << DisplacedMeshParams.Bias4;

	if (IsValid(DisplacedMeshParams.BaseMesh))
	{
		IdBuilder << DisplacedMeshParams.BaseMesh->GetPackage()->GetPersistentGuid();
	}

	if (IsValid(DisplacedMeshParams.DisplacementMap1))
	{
		IdBuilder << DisplacedMeshParams.DisplacementMap1->GetPackage()->GetPersistentGuid();
	}

	if (IsValid(DisplacedMeshParams.DisplacementMap2))
	{
		IdBuilder << DisplacedMeshParams.DisplacementMap2->GetPackage()->GetPersistentGuid();
	}

	if (IsValid(DisplacedMeshParams.DisplacementMap3))
	{
		IdBuilder << DisplacedMeshParams.DisplacementMap3->GetPackage()->GetPersistentGuid();
	}

	if (IsValid(DisplacedMeshParams.DisplacementMap4))
	{
		IdBuilder << DisplacedMeshParams.DisplacementMap4->GetPackage()->GetPersistentGuid();
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