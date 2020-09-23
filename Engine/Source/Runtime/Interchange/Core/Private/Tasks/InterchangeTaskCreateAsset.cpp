// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskCreateAsset.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeLogPrivate.h"
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
				FString NodeDisplayName = Node->GetDisplayLabel().ToString();
				const FString BaseFileName = FPaths::GetBaseFilename(SourceData->GetFilename());

				//Set the asset name and the package name
				if (NodeDisplayName.Equals(BaseFileName) || BaseFileName.IsEmpty())
				{
					OutAssetName = NodeDisplayName;
				}
				else
				{
					OutAssetName = BaseFileName.IsEmpty() ? NodeDisplayName : BaseFileName + TEXT("_") + NodeDisplayName;
				}
				OutPackageName = FPaths::Combine(*PackageBasePath, *OutAssetName);

				//Sanitize only the package name
				UE::Interchange::SanitizeInvalidChar(OutPackageName);
			}
		}//ns Private
	}//ns Interchange
}//ns UE

void UE::Interchange::FTaskCreatePackage::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	TSharedPtr<UE::Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	//The create package thread must always execute on the game thread
	check(IsInGameThread());

	UPackage* Pkg = nullptr;
	FString PackageName;
	FString AssetName;
	//If we do a reimport no need to create a package
	if (AsyncHelper->TaskData.ReimportObject)
	{
		Pkg = AsyncHelper->TaskData.ReimportObject->GetPackage();
		PackageName = Pkg->GetPathName();
	}
	else
	{
		Private::InternalGetPackageName(*AsyncHelper, SourceIndex, PackageBasePath, Node, PackageName, AssetName);
		// We can not create assets that share the name of a map file in the same location
		if (UE::Interchange::FPackageUtils::IsMapPackageAsset(PackageName))
		{
			const FText Message = FText::Format(NSLOCTEXT("Interchange", "AssetNameInUseByMap", "You can not create an asset named '{0}' because there is already a map file with this name in this folder."), FText::FromString(AssetName));
			UE_LOG(LogInterchangeCore, Warning, TEXT("%s"), *Message.ToString());
			//Skip this asset
			return;
		}

		Pkg = CreatePackage(nullptr, *PackageName);
		if (Pkg == nullptr)
		{
			const FText Message = FText::Format(NSLOCTEXT("Interchange", "CannotCreatePackageErrorMsg", "Cannot create package named '{0}', will not import asset {1}."), FText::FromString(PackageName), FText::FromString(AssetName));
			UE_LOG(LogInterchangeCore, Warning, TEXT("%s"), *Message.ToString());
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
		Factory->CreateEmptyAsset(CreateAssetParams);
	}
	// Make sure the destination package is loaded
	Pkg->FullyLoad();

	FScopeLock Lock(&AsyncHelper->CreatedPackagesLock);
	AsyncHelper->CreatedPackages.Add(PackageName, Pkg);
}

void UE::Interchange::FTaskCreateAsset::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	TSharedPtr<UE::Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(WeakAsyncHelper.IsValid());

	FString PackageName;
	FString AssetName;
	Private::InternalGetPackageName(*AsyncHelper, SourceIndex, PackageBasePath, Node, PackageName, AssetName);
	if (AsyncHelper->TaskData.ReimportObject)
	{
		UPackage* Pkg = AsyncHelper->TaskData.ReimportObject->GetPackage();
		PackageName = Pkg->GetPathName();
	}

	UPackage* Pkg = nullptr;
	{
		FScopeLock Lock(&AsyncHelper->CreatedPackagesLock);
		UPackage** PkgPtr = AsyncHelper->CreatedPackages.Find(PackageName);

		if (!PkgPtr || !(*PkgPtr))
		{
			const FText Message = FText::Format(NSLOCTEXT("Interchange", "CannotCreateAssetNoPackageErrorMsg", "Cannot create asset named '{1}', package '{0}'was not created properly."), FText::FromString(PackageName), FText::FromString(AssetName));
			UE_LOG(LogInterchangeCore, Warning, TEXT("%s"), *Message.ToString());
			return;
		}

		if (!AsyncHelper->SourceDatas.IsValidIndex(SourceIndex) || !AsyncHelper->Translators.IsValidIndex(SourceIndex))
		{
			const FText Message = FText::Format(NSLOCTEXT("Interchange", "CannotCreateAssetMissingDataErrorMsg", "Cannot create asset named '{0}', Source data or translator is invalid."), FText::FromString(AssetName));
			UE_LOG(LogInterchangeCore, Warning, TEXT("%s"), *Message.ToString());
			return;
		}

		Pkg = *PkgPtr;
	}

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

	UObject* NodeAsset = Factory->CreateAsset(CreateAssetParams);
	if (NodeAsset)
	{
		FScopeLock Lock(&AsyncHelper->ImportedAssetsPerSourceIndexLock);
		TArray<UE::Interchange::FImportAsyncHelper::FImportedAssetInfo>& ImportedInfos = AsyncHelper->ImportedAssetsPerSourceIndex.FindOrAdd(SourceIndex);
		UE::Interchange::FImportAsyncHelper::FImportedAssetInfo& AssetInfo = ImportedInfos.AddZeroed_GetRef();
		AssetInfo.ImportAsset = NodeAsset;
		Node->ReferenceObject = FSoftObjectPath(NodeAsset);
		AssetInfo.Factory = Factory;
	}
}