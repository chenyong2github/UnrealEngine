// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/DatasmithImportNode.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "DatasmithImportContext.h"
#include "DatasmithImportFactory.h"
#include "ExternalSource.h"
#include "ExternalSourceModule.h"
#include "SourceUri.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DatasmithImportNode)

#define LOCTEXT_NAMESPACE "ChaosClothAssetDatasmithImportNode"

FChaosClothAssetDatasmithImportNode::FChaosClothAssetDatasmithImportNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&DatasmithFile);
	RegisterOutputConnection(&Collection);
}

bool FChaosClothAssetDatasmithImportNode::EvaluateImpl(Dataflow::FContext& Context, FManagedArrayCollection& OutCollection) const
{
	using namespace UE::DatasmithImporter;

	const FFilePath& InFilePath = GetValue<FFilePath>(Context, &DatasmithFile);

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

	const FName PackageName = MakeUniqueObjectName(nullptr, UPackage::StaticClass(), FName(FPaths::Combine(GetTransientPackage()->GetPathName(), ExternalSource->GetSourceName())));
	const TStrongObjectPtr<UPackage> DestinationPackage(CreatePackage(*PackageName.ToString()));
	if (!ensure(DestinationPackage))
	{
		// Failed to create the package to hold this asset for some reason
		return false;
	}

	// Don't create the Actors in the level, just read the Assets
	DatasmithImportContext.Options->BaseOptions.SceneHandling = EDatasmithImportScene::AssetsOnly;

	constexpr EObjectFlags NewObjectFlags = RF_Public | RF_Transactional | RF_Transient | RF_Standalone;
	const TSharedPtr<FJsonObject> ImportSettingsJson;
	constexpr bool bIsSilent = true;

	if (!DatasmithImportContext.Init(DestinationPackage->GetPathName(), NewObjectFlags, GWarn, ImportSettingsJson, bIsSilent))
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
		UObject* const ClothObject = DatasmithImportContext.ImportedClothes.CreateIterator()->Value;
		UChaosClothAsset* const DatasmithClothAsset = Cast<UChaosClothAsset>(ClothObject);
		if (ensure(DatasmithClothAsset))
		{
			DatasmithClothAsset->GetClothCollection()->CopyTo(&OutCollection);
			return true;
		}
		DatasmithClothAsset->ClearFlags(RF_Standalone);
	}

	return false;
}

void FChaosClothAssetDatasmithImportNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	FManagedArrayCollection OutCollection;
	const bool bSuccess = EvaluateImpl(Context, OutCollection);

	if (bSuccess)
	{
		SetValue<FManagedArrayCollection>(Context, OutCollection, &Collection);
	}
	else
	{
		using namespace UE::Chaos::ClothAsset;

		// Init with an empty cloth collection
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>();
		FCollectionClothFacade CollectionClothFacade(ClothCollection);
		CollectionClothFacade.DefineSchema();
		CollectionClothFacade.AddLod();

		SetValue<FManagedArrayCollection>(Context, *ClothCollection, &Collection);
	}
}

#undef LOCTEXT_NAMESPACE
