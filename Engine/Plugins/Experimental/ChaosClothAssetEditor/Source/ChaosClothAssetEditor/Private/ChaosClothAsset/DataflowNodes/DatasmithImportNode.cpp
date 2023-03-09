// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/DataflowNodes/DatasmithImportNode.h"
#include "ChaosClothAsset/DataflowNodes/DataflowNodes.h"
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
	, DestPackageName("/Game/ClothAsset")
{
	RegisterInputConnection(&DatasmithFile);
	RegisterInputConnection(&DestPackageName);
	RegisterOutputConnection(&Collection);
}

bool FChaosClothAssetDatasmithImportNode::EvaluateImpl(Dataflow::FContext& Context, FManagedArrayCollection& OutCollection) const
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
		if (ensure(DatasmithClothAsset))
		{
			DatasmithClothAsset->GetClothCollection()->CopyTo(&OutCollection);
			return true;
		}
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
