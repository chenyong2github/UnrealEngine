// Copyright Epic Games, Inc. All Rights Reserved. 

#include "AssetUtils/CreateTexture2DUtil.h"

UE::AssetUtils::ECreateTexture2DResult UE::AssetUtils::SaveGeneratedTexture2DAsset(
	UTexture2D* GeneratedTexture,
	FTexture2DAssetOptions& Options,
	FTexture2DAssetResults& ResultsOut)
{
	// validate input texture
	if (ensure(GeneratedTexture) == false)
	{
		return ECreateTexture2DResult::InvalidInputTexture;
	}
	if (ensure(GeneratedTexture->GetOuter() == GetTransientPackage()) == false)
	{
		return ECreateTexture2DResult::InvalidInputTexture;
	}
	if (ensure(GeneratedTexture->Source.IsValid()) == false)
	{
		return ECreateTexture2DResult::InvalidInputTexture;
	}

	FString NewObjectName = FPackageName::GetLongPackageAssetName(Options.NewAssetPath);

	UPackage* UsePackage;
	if (Options.UsePackage != nullptr)
	{
		UsePackage = Options.UsePackage;
	}
	else
	{
		UsePackage = CreatePackage(*Options.NewAssetPath);
	}
	if (ensure(UsePackage != nullptr) == false)
	{
		return ECreateTexture2DResult::InvalidPackage;
	}

	// move texture from Transient package to real package
	bool bRenameOK = GeneratedTexture->Rename(*NewObjectName, UsePackage, REN_None);
	if (ensure(bRenameOK) == false)
	{
		return ECreateTexture2DResult::NameError;
	}

	// remove transient flag, add public/standalone/transactional
	GeneratedTexture->ClearFlags(RF_Transient);
	GeneratedTexture->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	[[maybe_unused]] bool MarkedDirty = GeneratedTexture->MarkPackageDirty();

	// do we need to Modify() it? we are not doing any undo/redo
	GeneratedTexture->Modify();

	// force texture to rebuild (can we defer this?)
	GeneratedTexture->UpdateResource();

	if (Options.bDeferPostEditChange == false)
	{
		GeneratedTexture->PostEditChange();		// this may be necessary if any Materials are using this texture
	}

	ResultsOut.Texture = GeneratedTexture;
	return ECreateTexture2DResult::Ok;
}