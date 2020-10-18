// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomBindingFactory.h"
#include "GroomBindingAsset.h"

UGroomBindingFactory::UGroomBindingFactory()
{
	SupportedClass = UGroomBindingAsset::StaticClass();

	bCreateNew = true;
	bEditAfterNew = true;
	bText = false;
	bEditorImport = true;
}

UObject* UGroomBindingFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UGroomBindingAsset* GroomBinding = NewObject<UGroomBindingAsset>(InParent, InName, Flags | RF_Transactional);
	GroomBinding->Groom = nullptr;
	GroomBinding->TargetSkeletalMesh = nullptr;
	GroomBinding->SourceSkeletalMesh = nullptr;
	GroomBinding->NumInterpolationPoints = 100;

	return GroomBinding;
}

UGroomBindingAsset* CreateGroomBindinAsset(const FString& InDesiredPackagePath, class UGroomAsset* GroomAsset, class USkeletalMesh* SourceSkelMesh, class USkeletalMesh* TargetSkelMesh, const int32 NumInterpolationPoints);
UGroomBindingAsset* UGroomBindingFactory::CreateNewGroomBindingAsset(
	const FString& InDesiredPackagePath,
	bool bInBuildAsset,
	UGroomAsset* InGroomAsset,
	USkeletalMesh* InSkeletalMesh,
	int32 InNumInterpolationPoints,
	USkeletalMesh* InSourceSkeletalMeshForTransfer)
{
	if (!InGroomAsset || !InSkeletalMesh)
	{
		return nullptr;
	}

	UGroomBindingAsset* BindingAsset = CreateGroomBindinAsset(InDesiredPackagePath, InGroomAsset, InSourceSkeletalMeshForTransfer, InSkeletalMesh, InNumInterpolationPoints);
	if (bInBuildAsset)
	{
		BindingAsset->Build();
	}
	return BindingAsset;
}
