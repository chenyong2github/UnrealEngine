// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskCreateAsset.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "Misc/Paths.h"
#include "PackageUtils/PackageUtils.h"
#include "Stats/Stats.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ObjectMacros.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Nodes/InterchangeBaseNode.h"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			void InternalGetPackageName(const UE::Interchange::FImportAsyncHelper& AsyncHelper, const int32 SourceIndex, const FString& PackageBasePath, const UInterchangeBaseNode* Node, FString& OutPackageName, FString& OutAssetName)
			{
				const UInterchangeSourceData* SourceData = AsyncHelper.SourceDatas[SourceIndex];
				check(SourceData);
				FString NodeDisplayName = Node->GetAssetName();

				// Set the asset name and the package name
				OutAssetName = NodeDisplayName;
				SanitizeObjectName(OutAssetName);

				FString SanitazedPackageBasePath = PackageBasePath;
				SanitizeObjectPath(SanitazedPackageBasePath);

				OutPackageName = FPaths::Combine(*SanitazedPackageBasePath, *OutAssetName);
			}
		}//ns Private
	}//ns Interchange
}//ns UE

void UE::Interchange::FTaskCreatePackage::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(CreatePackage)
#endif
	TSharedPtr<UE::Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	//Verify if the task was cancel
	if (AsyncHelper->bCancel)
	{
		return;
	}

	UPackage* Pkg = nullptr;
	FString PackageName;
	FString AssetName;
	//If we do a reimport no need to create a package
	if (AsyncHelper->TaskData.ReimportObject)
	{
		Private::InternalGetPackageName(*AsyncHelper, SourceIndex, PackageBasePath, Node, PackageName, AssetName);
		UPackage* ResultFindPackage = FindPackage(nullptr, *PackageName);
		UObject* FindOuter = (ResultFindPackage == nullptr ? ANY_PACKAGE : ResultFindPackage);
		UObject* ExistingObject = FindObject<UObject>(FindOuter, *AssetName);
		if (ExistingObject)
		{
			Pkg = ExistingObject->GetPackage();
			PackageName = Pkg->GetPathName();
		}
		else
		{
			const FText Message = FText::Format(NSLOCTEXT("Interchange", "CannotFindPackageDuringReimportErrorMsg", "Cannot find package named '{0}', for asset {1}."), FText::FromString(PackageName), FText::FromString(AssetName));
			UE_LOG(LogInterchangeEngine, Warning, TEXT("%s"), *Message.ToString());
			//Skip this asset
			return;
		}
	}
	else
	{
		//The create package thread must always execute on the game thread
		check(IsInGameThread());

		Private::InternalGetPackageName(*AsyncHelper, SourceIndex, PackageBasePath, Node, PackageName, AssetName);
		// We can not create assets that share the name of a map file in the same location
		if (UE::Interchange::FPackageUtils::IsMapPackageAsset(PackageName))
		{
			const FText Message = FText::Format(NSLOCTEXT("Interchange", "AssetNameInUseByMap", "You can not create an asset named '{0}' because there is already a map file with this name in this folder."), FText::FromString(AssetName));
			UE_LOG(LogInterchangeEngine, Warning, TEXT("%s"), *Message.ToString());
			//Skip this asset
			return;
		}

		Pkg = CreatePackage(*PackageName);
		if (Pkg == nullptr)
		{
			const FText Message = FText::Format(NSLOCTEXT("Interchange", "CannotCreatePackageErrorMsg", "Cannot create package named '{0}', will not import asset {1}."), FText::FromString(PackageName), FText::FromString(AssetName));
			UE_LOG(LogInterchangeEngine, Warning, TEXT("%s"), *Message.ToString());
			//Skip this asset
			return;
		}

		//Import Asset describe by the node
		UInterchangeFactoryBase::FCreateAssetParams CreateAssetParams;
		CreateAssetParams.AssetName = AssetName;
		CreateAssetParams.AssetNode = Node;
		CreateAssetParams.Parent = Pkg;
		CreateAssetParams.SourceData = AsyncHelper->SourceDatas[SourceIndex];
		CreateAssetParams.Translator = nullptr;
		if (AsyncHelper->BaseNodeContainers.IsValidIndex(SourceIndex))
		{
			CreateAssetParams.NodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
		}
		CreateAssetParams.ReimportObject = AsyncHelper->TaskData.ReimportObject;
		//Make sure the asset UObject is created with the correct type on the main thread
		UObject* NodeAsset = Factory->CreateEmptyAsset(CreateAssetParams);
		if (NodeAsset)
		{
			if (!NodeAsset->HasAnyInternalFlags(EInternalObjectFlags::Async))
			{
				//Since the async flag is not set we must be in the game thread
				ensure(IsInGameThread());
				NodeAsset->SetInternalFlags(EInternalObjectFlags::Async);
			}
			FScopeLock Lock(&AsyncHelper->ImportedAssetsPerSourceIndexLock);
			TArray<UE::Interchange::FImportAsyncHelper::FImportedAssetInfo>& ImportedInfos = AsyncHelper->ImportedAssetsPerSourceIndex.FindOrAdd(SourceIndex);
			UE::Interchange::FImportAsyncHelper::FImportedAssetInfo& AssetInfo = ImportedInfos.AddDefaulted_GetRef();
			AssetInfo.ImportAsset = NodeAsset;
			AssetInfo.Factory = Factory;
			AssetInfo.NodeUniqueId = Node->GetUniqueID();
			Node->ReferenceObject = FSoftObjectPath(NodeAsset);
		}
	}
	// Make sure the destination package is loaded
	Pkg->FullyLoad();
	
	{
		FScopeLock Lock(&AsyncHelper->CreatedPackagesLock);
		AsyncHelper->CreatedPackages.Add(PackageName, Pkg);
	}
}

void UE::Interchange::FTaskCreateAsset::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(CreateAsset)
#endif
	TSharedPtr<UE::Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(WeakAsyncHelper.IsValid());

	//Verify if the task was cancel
	if (AsyncHelper->bCancel)
	{
		return;
	}

	UPackage* Pkg = nullptr;
	FString PackageName;
	FString AssetName;
	Private::InternalGetPackageName(*AsyncHelper, SourceIndex, PackageBasePath, Node, PackageName, AssetName);
	bool bSkipAsset = false;
	UObject* ExistingObject = nullptr;
	if (AsyncHelper->TaskData.ReimportObject)
	{
		//When we re-import one particular asset, if the source file contain other assets, we want to set the node= reference UObject for those asset to the existing asset
		//The way to discover this case is to compare the re-import asset with the node asset.
		UPackage* ResultFindPackage = FindPackage(nullptr, *PackageName);
		UObject* FindOuter = (ResultFindPackage == nullptr ? ANY_PACKAGE : ResultFindPackage);
		ExistingObject = FindObject<UObject>(FindOuter, *AssetName);
		bSkipAsset = !ExistingObject || ExistingObject != AsyncHelper->TaskData.ReimportObject;
		if (!bSkipAsset)
		{
			Pkg = AsyncHelper->TaskData.ReimportObject->GetPackage();
			PackageName = Pkg->GetPathName();
		}
		else if(ExistingObject)
		{
			Pkg = ExistingObject->GetPackage();
			PackageName = Pkg->GetPathName();
		}
	}
	else
	{
		FScopeLock Lock(&AsyncHelper->CreatedPackagesLock);
		UPackage** PkgPtr = AsyncHelper->CreatedPackages.Find(PackageName);

		if (!PkgPtr || !(*PkgPtr))
		{
			const FText Message = FText::Format(NSLOCTEXT("Interchange", "CannotCreateAssetNoPackageErrorMsg", "Cannot create asset named '{1}', package '{0}'was not created properly."), FText::FromString(PackageName), FText::FromString(AssetName));
			UE_LOG(LogInterchangeEngine, Warning, TEXT("%s"), *Message.ToString());
			return;
		}

		if (!AsyncHelper->SourceDatas.IsValidIndex(SourceIndex) || !AsyncHelper->Translators.IsValidIndex(SourceIndex))
		{
			const FText Message = FText::Format(NSLOCTEXT("Interchange", "CannotCreateAssetMissingDataErrorMsg", "Cannot create asset named '{0}', Source data or translator is invalid."), FText::FromString(AssetName));
			UE_LOG(LogInterchangeEngine, Warning, TEXT("%s"), *Message.ToString());
			return;
		}

		Pkg = *PkgPtr;
	}

	UObject* NodeAsset = nullptr;
	if (bSkipAsset)
	{
		NodeAsset = ExistingObject;
	}
	else
	{
		UInterchangeTranslatorBase* Translator = AsyncHelper->Translators[SourceIndex];
		//Import Asset describe by the node
		UInterchangeFactoryBase::FCreateAssetParams CreateAssetParams;
		CreateAssetParams.AssetName = AssetName;
		CreateAssetParams.AssetNode = Node;
		CreateAssetParams.Parent = Pkg;
		CreateAssetParams.SourceData = AsyncHelper->SourceDatas[SourceIndex];
		CreateAssetParams.Translator = Translator;
		if (AsyncHelper->BaseNodeContainers.IsValidIndex(SourceIndex))
		{
			CreateAssetParams.NodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
		}
		CreateAssetParams.ReimportObject = AsyncHelper->TaskData.ReimportObject;

		CreateAssetParams.ReimportStrategyFlags = EReimportStrategyFlags::ApplyNoProperties;
		//CreateAssetParams.ReimportStrategyFlags = EReimportStrategyFlags::ApplyPipelineProperties;
		//CreateAssetParams.ReimportStrategyFlags = EReimportStrategyFlags::ApplyEditorChangedProperties;

		NodeAsset = Factory->CreateAsset(CreateAssetParams);
	}
	if (NodeAsset)
	{
		if (!bSkipAsset)
		{
			FScopeLock Lock(&AsyncHelper->ImportedAssetsPerSourceIndexLock);
			TArray<UE::Interchange::FImportAsyncHelper::FImportedAssetInfo>& ImportedInfos = AsyncHelper->ImportedAssetsPerSourceIndex.FindOrAdd(SourceIndex);
			UE::Interchange::FImportAsyncHelper::FImportedAssetInfo* AssetInfoPtr = ImportedInfos.FindByPredicate([NodeAsset](const UE::Interchange::FImportAsyncHelper::FImportedAssetInfo& CurInfo)
			{
				return CurInfo.ImportAsset == NodeAsset;
			});

			if (!AssetInfoPtr)
			{
				UE::Interchange::FImportAsyncHelper::FImportedAssetInfo& AssetInfo = ImportedInfos.AddDefaulted_GetRef();
				AssetInfo.ImportAsset = NodeAsset;
				AssetInfo.Factory = Factory;
				AssetInfo.NodeUniqueId = Node->GetUniqueID();
			}
		}

		Node->ReferenceObject = FSoftObjectPath(NodeAsset);
	}
}