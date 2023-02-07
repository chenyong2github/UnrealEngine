// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothDataflowNodes.h"
#include "Logging/LogMacros.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "SourceUri.h"
#include "ExternalSource.h"
#include "ExternalSourceModule.h"
#include "DatasmithImportContext.h"
#include "DatasmithImportFactory.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothCollection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothDataflowNodes)

#define LOCTEXT_NAMESPACE "ClothDataflowNodes"

namespace Dataflow
{
	void RegisterClothDataflowNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FClothAssetTerminalDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FClothAssetDatasmithImportNode);
	}
}

FClothAssetTerminalDataflowNode::FClothAssetTerminalDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid) :
	FDataflowTerminalNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection, &Collection);
}


void FClothAssetTerminalDataflowNode::SetAssetValue(TObjectPtr<UObject> Asset, Dataflow::FContext& Context) const
{
	if (UChaosClothAsset* const ClothAsset = Cast<UChaosClothAsset>(Asset.Get()))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		if (InCollection.HasGroup(UE::Chaos::ClothAsset::FClothCollection::LodsGroup))  // TODO: SkeletalMeshRenderData crashes with empty collection
		{
			ClothAsset->GetClothCollection()->Reset();
			
			InCollection.CopyTo(ClothAsset->GetClothCollection().Get());

			// Set the render mesh to duplicate the sim mesh
			constexpr int32 MaterialId = 0;	
			
			ClothAsset->CopySimMeshToRenderMesh(MaterialId);  // TODO: Make this a node (needs to make cloth collection an adapter)

			// Rebuild the asset static data
			ClothAsset->Build();
		}
	}
}

void FClothAssetTerminalDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
	SetValue<FManagedArrayCollection>(Context, InCollection, &Collection);
}

FClothAssetDatasmithImportNode::FClothAssetDatasmithImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid),
	DestPackageName("/Game/ClothAsset")
{
	RegisterInputConnection(&DatasmithFile);
	RegisterInputConnection(&DestPackageName);
	RegisterOutputConnection(&Collection);
}


bool FClothAssetDatasmithImportNode::EvaluateImpl(Dataflow::FContext& Context, FManagedArrayCollection& OutCollection) const
{
	using namespace UE::DatasmithImporter;

	const FFilePath& InFilePath = GetValue<FFilePath>(Context, &DatasmithFile);
	const FString& InDestPackageName = GetValue<FString>(Context, &DestPackageName);

	const FSourceUri SourceUri = FSourceUri::FromFilePath(InFilePath.FilePath);
	const TSharedPtr<FExternalSource> ExternalSource = IExternalSourceModule::GetOrCreateExternalSource(SourceUri);
	if (!ExternalSource)
	{
		return false;
	}

	constexpr bool bLoadConfig = false; // TODO: Not sure what this does
	const FName LoggerName(TEXT("ImportDatasmithClothNode"));
	const FText LoggerLabel(NSLOCTEXT("ImportDatasmithClothNode", "LoggerLabel", "ImportDatasmithClothNode"));
	FDatasmithImportContext DatasmithImportContext(ExternalSource, bLoadConfig, LoggerName, LoggerLabel);

	const TStrongObjectPtr<const UPackage> DestinationPackage(CreatePackage(*InDestPackageName));		// TODO: would be nice to not have to create the package but it seems necessary for now
	if (!ensure(DestinationPackage))
	{
		// Failed to create the package to hold this asset for some reason
		return false;
	}
	
	// Don't create the Actors in the level, just read the Assets
	DatasmithImportContext.Options->BaseOptions.SceneHandling = EDatasmithImportScene::AssetsOnly;

	constexpr EObjectFlags NewObjectFlags = RF_Public | RF_Standalone | RF_Transactional;
	const TSharedPtr<FJsonObject> ImportSettingsJson;
	constexpr bool bIsSilent = true;
	const FString DestinationPath = DestinationPackage->GetName();
	if (!DatasmithImportContext.Init(DestinationPath, NewObjectFlags, GWarn, ImportSettingsJson, bIsSilent))
	{
		return false;
	}

	if (const TSharedPtr<IDatasmithScene> LoadedScene = ExternalSource->TryLoad())
	{
		DatasmithImportContext.InitScene(LoadedScene.ToSharedRef());
	}
	else
	{
		return false;
	}

	bool bUserCancelled = false;
	bool bImportSucceed = DatasmithImportFactoryImpl::ImportDatasmithScene(DatasmithImportContext, bUserCancelled);
	bImportSucceed &= !bUserCancelled;

	if (bImportSucceed && DatasmithImportContext.ImportedClothes.Num() > 0)
	{
		const UObject* const ClothObject = DatasmithImportContext.ImportedClothes.CreateIterator()->Value;
		const UChaosClothAsset* const DatasmithClothAsset = Cast<UChaosClothAsset>(ClothObject);
		ensure(DatasmithClothAsset);

		DatasmithClothAsset->GetClothCollection()->CopyTo(&OutCollection);

		return true;
	}

	return false;
}

void FClothAssetDatasmithImportNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	FManagedArrayCollection OutCollection;
	const bool bSuccess = EvaluateImpl(Context, OutCollection);

	if (bSuccess)
	{
		SetValue<FManagedArrayCollection>(Context, OutCollection, &Collection);
	}
	else
	{
		// Init with an empty cloth collection
		UE::Chaos::ClothAsset::FClothCollection ClothCollection;
		SetValue<FManagedArrayCollection>(Context, ClothCollection, &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
