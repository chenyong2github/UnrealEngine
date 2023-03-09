// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothAssetFactory.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "Animation/Skeleton.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothAssetFactory)

UChaosClothAssetFactory::UChaosClothAssetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEditorImport = true;
	bEditAfterNew = true;
	SupportedClass = UChaosClothAsset::StaticClass();
}

UObject* UChaosClothAssetFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	using namespace UE::Chaos::ClothAsset;

	UChaosClothAsset* const ClothAsset = NewObject<UChaosClothAsset>(Parent, Class, Name, Flags | RF_Transactional | RF_Public | RF_Standalone);
	ClothAsset->MarkPackageDirty();

	// Add an empty default LOD, to avoid LOD mismatch with render data
	TSharedPtr<FManagedArrayCollection> ClothCollection = ClothAsset->GetClothCollection();

	FCollectionClothFacade ClothFacade(ClothCollection);
	ClothFacade.DefineSchema();
	ClothFacade.AddLod();

	// Set the default skeleton on this new LOD and rebuild the static data models (which is done by default with this override)
	ClothAsset->SetSkeleton(nullptr);

	return ClothAsset;
}
