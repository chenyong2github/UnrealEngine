// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scene/InterchangeSceneImportAssetFactory.h"

#include "InterchangeAssetImportData.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeSceneImportAsset.h"
#include "InterchangeSceneImportAssetFactoryNode.h"
#include "InterchangeResult.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "InterchangeVariantSetNode.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "InterchangeSceneImportAssetFactory"

namespace UE::Interchange::Private::InterchangeSceneImportAssetFactory
{
	const UInterchangeSceneImportAssetFactoryNode* GetFactoryNode(const UInterchangeFactoryBase::FImportAssetObjectParams& Arguments, UClass* TargetClass)
	{
		if (!Arguments.NodeContainer || !Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(TargetClass))
		{
			return nullptr;
		}

		const UInterchangeSceneImportAssetFactoryNode* FactoryNode = Cast<UInterchangeSceneImportAssetFactoryNode>(Arguments.AssetNode);
		if (FactoryNode == nullptr)
		{
			return nullptr;
		}

		return FactoryNode;
	}

	UObject* FindOrCreateAsset(const UInterchangeFactoryBase::FImportAssetObjectParams& Arguments, UClass* TargetClass)
	{
		UObject* TargetAsset = Arguments.ReimportObject ? Arguments.ReimportObject : StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

		// Create a new asset or return existing asset, if possible
		if (!TargetAsset)
		{
			check(IsInGameThread());
			TargetAsset = NewObject<UObject>(Arguments.Parent, TargetClass, *Arguments.AssetName, RF_Public | RF_Standalone);
		}
		else if (!TargetAsset->GetClass()->IsChildOf(TargetClass))
		{
			TargetAsset = nullptr;
		}

		return TargetAsset;
	}
}

UClass* UInterchangeSceneImportAssetFactory::GetFactoryClass() const
{
	return UInterchangeSceneImportAsset::StaticClass();
}

bool UInterchangeSceneImportAssetFactory::GetSourceFilenames(const UObject* Object, TArray<FString>& OutSourceFilenames) const
{
#if WITH_EDITORONLY_DATA
	if (const UInterchangeSceneImportAsset* SceneImportAsset = Cast<UInterchangeSceneImportAsset>(Object))
	{
		return UE::Interchange::FFactoryCommon::GetSourceFilenames(SceneImportAsset->AssetImportData, OutSourceFilenames);
	}
#endif

	return false;
}

bool UInterchangeSceneImportAssetFactory::SetSourceFilename(const UObject* Object, const FString& SourceFilename, int32 SourceIndex) const
{
#if WITH_EDITORONLY_DATA
	if (const UInterchangeSceneImportAsset* SceneImportAsset = Cast<UInterchangeSceneImportAsset>(Object))
	{
		return UE::Interchange::FFactoryCommon::SetSourceFilename(SceneImportAsset->AssetImportData, SourceFilename, SourceIndex);
	}
#endif

	return false;
}

UInterchangeFactoryBase::FImportAssetResult UInterchangeSceneImportAssetFactory::BeginImportAsset_GameThread(const FImportAssetObjectParams& Arguments)
{
	using namespace UE::Interchange::Private::InterchangeSceneImportAssetFactory;

	UClass* TargetClass = GetFactoryClass();

	if (GetFactoryNode(Arguments, TargetClass) == nullptr)
	{
		return {};
	}

	UInterchangeSceneImportAsset* SceneImportAsset = Cast<UInterchangeSceneImportAsset>(FindOrCreateAsset(Arguments, TargetClass));

	if (!SceneImportAsset)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("Could not create InterchangeSceneImportAsset asset %s"), *Arguments.AssetName);
	}

	FImportAssetResult Result;
	Result.ImportedObject = SceneImportAsset;
	
	return Result;
}

UObject* UInterchangeSceneImportAssetFactory::ImportAssetObject_Async(const FImportAssetObjectParams& Arguments)
{
	using namespace UE::Interchange::Private::InterchangeSceneImportAssetFactory;

	UClass* TargetClass = GetFactoryClass();

	const UInterchangeSceneImportAssetFactoryNode* FactoryNode = GetFactoryNode(Arguments, TargetClass);
	if (!FactoryNode)
	{
		return nullptr;
	}

	UInterchangeSceneImportAsset* SceneImportAsset = Cast<UInterchangeSceneImportAsset>(FindOrCreateAsset(Arguments, TargetClass));

	if (!ensure(SceneImportAsset))
	{
		UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
		Message->SourceAssetName = Arguments.SourceData->GetFilename();
		Message->DestinationAssetName = Arguments.AssetName;
		Message->AssetType = UInterchangeSceneImportAsset::StaticClass();
		Message->Text = FText::Format(LOCTEXT("CreateAssetFailed", "Could not create nor find SceneImportAsset asset {0}."), FText::FromString(Arguments.AssetName));
		return nullptr;
	}

	/** Apply all FactoryNode custom attributes to the level sequence asset */
	FactoryNode->ApplyAllCustomAttributeToObject(SceneImportAsset);

	//Getting the file Hash will cache it into the source data
	Arguments.SourceData->GetFileContentHash();

	return SceneImportAsset;
}

void UInterchangeSceneImportAssetFactory::SetupObject_GameThread(const FSetupObjectParams& Arguments)
{
	check(IsInGameThread());
	Super::SetupObject_GameThread(Arguments);

#if WITH_EDITORONLY_DATA
	using namespace UE::Interchange;

	if (ensure(Arguments.ImportedObject && Arguments.SourceData))
	{
		// We must call the Update of the asset source file in the main thread because UAssetImportData::Update execute some delegate we do not control
		UInterchangeSceneImportAsset* SceneImportAsset = Cast<UInterchangeSceneImportAsset>(Arguments.ImportedObject);
		if (ensure(SceneImportAsset))
		{
			if (Arguments.bIsReimport)
			{
				ensure(SceneImportAsset->AssetImportData);
				SceneImportAsset->UpdateSceneObjects();
			}

			FFactoryCommon::FUpdateImportAssetDataParameters Parameters(SceneImportAsset, SceneImportAsset->AssetImportData, Arguments.SourceData, Arguments.NodeUniqueID, Arguments.NodeContainer, Arguments.OriginalPipelines);
			SceneImportAsset->AssetImportData = Cast<UInterchangeAssetImportData>(FFactoryCommon::UpdateImportAssetData(Parameters));
		}

	}
#endif
}

void UInterchangeSceneImportAssetFactory::FinalizeObject_GameThread(const FSetupObjectParams& Arguments)
{
	check(IsInGameThread());
	Super::FinalizeObject_GameThread(Arguments);

#if WITH_EDITOR
	if (UInterchangeSceneImportAsset* SceneImportAsset = Cast<UInterchangeSceneImportAsset>(Arguments.ImportedObject))
	{
		SceneImportAsset->RegisterWorldRenameCallbacks();
	}
#endif
}

#undef LOCTEXT_NAMESPACE