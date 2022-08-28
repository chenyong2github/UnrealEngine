// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFExporterUtility.h"
#include "Materials/MaterialInstance.h"
#include "AssetRegistry/IAssetRegistry.h"

const UStaticMesh* FGLTFExporterUtility::GetPreviewMesh(const UMaterialInterface* Material)
{
#if WITH_EDITORONLY_DATA
	// The following implementation is based of FMaterialInstanceEditor::RefreshPreviewAsset
	const UStaticMesh* PreviewMesh = Cast<UStaticMesh>(Material->PreviewMesh.TryLoad());
	if (PreviewMesh == nullptr)
	{
		// Attempt to use the parent material's preview mesh if the instance's preview mesh is invalid
		const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Material);
		if (MaterialInstance != nullptr)
		{
			const UMaterialInterface* ParentMaterial = MaterialInstance->Parent;
			if (ParentMaterial != nullptr)
			{
				PreviewMesh = Cast<UStaticMesh>(ParentMaterial->PreviewMesh.TryLoad());
			}
		}
	}

	if (PreviewMesh != nullptr)
	{
		return PreviewMesh;
	}
#endif

	return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EditorMeshes/EditorSphere.EditorSphere"));
}

const USkeletalMesh* FGLTFExporterUtility::GetPreviewMesh(const UAnimSequence* AnimSequence)
{
	const USkeletalMesh* PreviewMesh = AnimSequence->GetPreviewMesh();
	if (PreviewMesh == nullptr)
	{
		const USkeleton* Skeleton = AnimSequence->GetSkeleton();
		if (Skeleton != nullptr)
		{
			PreviewMesh = Skeleton->GetPreviewMesh();
			if (PreviewMesh == nullptr)
			{
				PreviewMesh = FindCompatibleMesh(Skeleton);
			}
		}
	}

	return PreviewMesh;
}

const USkeletalMesh* FGLTFExporterUtility::FindCompatibleMesh(const USkeleton *Skeleton)
{
	FARFilter Filter;
	Filter.ClassNames.Add(USkeletalMesh::StaticClass()->GetFName());
	Filter.TagsAndValues.Add(USkeletalMesh::GetSkeletonMemberName(), FAssetData(Skeleton).GetExportTextName());

	TArray<FAssetData> FilteredAssets;
	IAssetRegistry::GetChecked().GetAssets(Filter, FilteredAssets);

	for (const FAssetData& Asset : FilteredAssets)
	{
		const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset.GetAsset());
		if (SkeletalMesh != nullptr)
		{
			return SkeletalMesh;
		}
	}

	return nullptr;
}

TArray<UWorld*> FGLTFExporterUtility::GetAssociatedWorlds(const UObject* Object)
{
	TArray<UWorld*> Worlds;
	TArray<FAssetIdentifier> Dependencies;

	const FName OuterPathName = *Object->GetOutermost()->GetPathName();
	IAssetRegistry::GetChecked().GetDependencies(OuterPathName, Dependencies);

	for (FAssetIdentifier& Dependency : Dependencies)
	{
		FString PackageName = Dependency.PackageName.ToString();
		UWorld* World = LoadObject<UWorld>(nullptr, *PackageName, nullptr, LOAD_NoWarn);
		if (World != nullptr)
		{
			Worlds.AddUnique(World);
		}
	}

	return Worlds;
}
