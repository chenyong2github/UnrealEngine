// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskCreateAsset.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeManager.h"
#include "InterchangeResult.h"
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

			UObject* GetExistingObjectFromAssetImportData(TSharedPtr<UE::Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper, UInterchangeBaseNode* Node)
			{
				UInterchangeAssetImportData* OriginalAssetImportData = nullptr;
				TArray<UObject*> SubObjects;
				GetObjectsWithOuter(AsyncHelper->TaskData.ReimportObject, SubObjects);
				for (UObject* SubObject : SubObjects)
				{
					OriginalAssetImportData = Cast<UInterchangeAssetImportData>(SubObject);
					if (OriginalAssetImportData)
					{
						break;
					}
				}

				if (OriginalAssetImportData)
				{
					if (UInterchangeBaseNodeContainer* NodeContainer = OriginalAssetImportData->NodeContainer)
					{
						UClass* FactoryNodeClass = Node->GetClass();
						UInterchangeBaseNode* SelectedOriginalNode = nullptr;
						NodeContainer->BreakableIterateNodes([FactoryNodeClass, &SelectedOriginalNode](const FString&, UInterchangeBaseNode* OriginalNode)
							{
								if (OriginalNode->GetClass() == FactoryNodeClass && OriginalNode->GetParentUid() == UInterchangeBaseNode::InvalidNodeUid())
								{
									SelectedOriginalNode = OriginalNode;
									return true;
								}
								return false;
							});

						// Hack for the reimport with a new file. (to revisited for MVP as this not a future proof solution. Should there be some sort of adapter that tell us how to do the mapping? Or should the pipeline do the mapping on a reimport? After all it is the pipeline that chose the name of the asset.
						if (SelectedOriginalNode)
						{
							return AsyncHelper->TaskData.ReimportObject;
						}
					}
				}

				return nullptr;
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

	//The create package thread must always execute on the game thread
	check(IsInGameThread());

	// Create factory
	UInterchangeFactoryBase* Factory = NewObject<UInterchangeFactoryBase>(GetTransientPackage(), FactoryClass);
	Factory->SetResultsContainer(AsyncHelper->AssetImportResult->GetResults());

	{
		FScopeLock Lock(&AsyncHelper->CreatedFactoriesLock);
		AsyncHelper->CreatedFactories.Add(Node->GetUniqueID(), Factory);
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

		if (ExistingObject != AsyncHelper->TaskData.ReimportObject)
		{
			// Try to see if we can do a mapping from the source data (we should revisit this for the MVP)
			ExistingObject = UE::Interchange::Private::GetExistingObjectFromAssetImportData(AsyncHelper, Node);
		}
		

		if (ExistingObject)
		{
			Pkg = ExistingObject->GetPackage();
			PackageName = Pkg->GetPathName();
		}
		else
		{
			UInterchangeResultError_Generic* Message = Factory->AddMessage<UInterchangeResultError_Generic>();
			Message->SourceAssetName = AsyncHelper->SourceDatas[SourceIndex]->GetFilename();
			Message->DestinationAssetName = AssetName;
			Message->AssetType = Node->GetObjectClass();
			Message->Text = NSLOCTEXT("Interchange", "CannotFindPackageDuringReimport", "Cannot find an existing package.");

			//Skip this asset
			return;
		}
	}
	else
	{
		Private::InternalGetPackageName(*AsyncHelper, SourceIndex, PackageBasePath, Node, PackageName, AssetName);
		// We can not create assets that share the name of a map file in the same location
		if (UE::Interchange::FPackageUtils::IsMapPackageAsset(PackageName))
		{
			UInterchangeResultError_Generic* Message = Factory->AddMessage<UInterchangeResultError_Generic>();
			Message->SourceAssetName = AsyncHelper->SourceDatas[SourceIndex]->GetFilename();
			Message->DestinationAssetName = AssetName;
			Message->AssetType = Node->GetObjectClass();
			Message->Text = NSLOCTEXT("Interchange", "MapExistsWithSameName", "You cannot create an asset with this name, as there is already a map file with the same name in this folder.");

			//Skip this asset
			return;
		}

		Pkg = CreatePackage(*PackageName);
		if (Pkg == nullptr)
		{
			UInterchangeResultError_Generic* Message = Factory->AddMessage<UInterchangeResultError_Generic>();
			Message->SourceAssetName = AsyncHelper->SourceDatas[SourceIndex]->GetFilename();
			Message->DestinationAssetName = AssetName;
			Message->AssetType = Node->GetObjectClass();
			Message->Text = FText::Format(NSLOCTEXT("Interchange", "CouldntCreatePackage", "It was not possible to create a package named '{0}'; the asset will not be imported."), FText::FromString(PackageName));

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
			TArray<UE::Interchange::FImportAsyncHelper::FImportedObjectInfo>& ImportedInfos = AsyncHelper->ImportedAssetsPerSourceIndex.FindOrAdd(SourceIndex);
			UE::Interchange::FImportAsyncHelper::FImportedObjectInfo& AssetInfo = ImportedInfos.AddDefaulted_GetRef();
			AssetInfo.ImportedObject = NodeAsset;
			AssetInfo.Factory = Factory;
			AssetInfo.FactoryNode = Node;
			AssetInfo.bIsReimport = bool(AsyncHelper->TaskData.ReimportObject);
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
	check(AsyncHelper.IsValid());

	//Verify if the task was cancel
	if (AsyncHelper->bCancel)
	{
		return;
	}

	UInterchangeFactoryBase* Factory = nullptr;
	{
		FScopeLock Lock(&AsyncHelper->CreatedFactoriesLock);
		Factory = AsyncHelper->CreatedFactories.FindChecked(Node->GetUniqueID());
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

		if(ExistingObject != AsyncHelper->TaskData.ReimportObject)
		{
			// Try to see if we can do a mapping from the source data (we should revisit this for the MVP)
			ExistingObject = UE::Interchange::Private::GetExistingObjectFromAssetImportData(AsyncHelper, Node);
		}

		bSkipAsset = !ExistingObject || ExistingObject != AsyncHelper->TaskData.ReimportObject;
		if (!bSkipAsset)
		{
			Pkg = AsyncHelper->TaskData.ReimportObject->GetPackage();
			PackageName = Pkg->GetPathName();
			AssetName = ExistingObject->GetName();
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
			UInterchangeResultError_Generic* Message = Factory->AddMessage<UInterchangeResultError_Generic>();
			Message->SourceAssetName = AsyncHelper->SourceDatas[SourceIndex]->GetFilename();
			Message->DestinationAssetName = AssetName;
			Message->AssetType = Node->GetObjectClass();
			Message->Text = NSLOCTEXT("Interchange", "BadPackage", "It was not possible to create the asset as its package was not created correctly.");

			return;
		}

		if (!AsyncHelper->SourceDatas.IsValidIndex(SourceIndex) || !AsyncHelper->Translators.IsValidIndex(SourceIndex))
		{
			UInterchangeResultError_Generic* Message = Factory->AddMessage<UInterchangeResultError_Generic>();
			Message->DestinationAssetName = AssetName;
			Message->AssetType = Node->GetObjectClass();
			Message->Text = NSLOCTEXT("Interchange", "SourceDataOrTranslatorInvalid", "It was not possible to create the asset as its translator was not created correctly.");

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
			TArray<UE::Interchange::FImportAsyncHelper::FImportedObjectInfo>& ImportedInfos = AsyncHelper->ImportedAssetsPerSourceIndex.FindOrAdd(SourceIndex);
			UE::Interchange::FImportAsyncHelper::FImportedObjectInfo* AssetInfoPtr = ImportedInfos.FindByPredicate([NodeAsset](const UE::Interchange::FImportAsyncHelper::FImportedObjectInfo& CurInfo)
			{
				return CurInfo.ImportedObject == NodeAsset;
			});

			if (!AssetInfoPtr)
			{
				UE::Interchange::FImportAsyncHelper::FImportedObjectInfo& AssetInfo = ImportedInfos.AddDefaulted_GetRef();
				AssetInfo.ImportedObject = NodeAsset;
				AssetInfo.Factory = Factory;
				AssetInfo.FactoryNode = Node;
				AssetInfo.bIsReimport = bool(AsyncHelper->TaskData.ReimportObject);
			}

			// Fill in destination asset and type in any results which have been added previously by a translator or pipeline, now that we have a corresponding factory.
			UInterchangeResultsContainer* Results = AsyncHelper->AssetImportResult->GetResults();
			for (UInterchangeResult* Result : Results->GetResults())
			{
				if (!Result->InterchangeKey.IsEmpty() && (Result->DestinationAssetName.IsEmpty() || Result->AssetType == nullptr))
				{
					TArray<FString> TargetAssets;
					Node->GetTargetNodeUids(TargetAssets);
					if (TargetAssets.Contains(Result->InterchangeKey))
					{
						Result->DestinationAssetName = NodeAsset->GetPathName();
						Result->AssetType = NodeAsset->GetClass();
					}
				}
			}
		}

		Node->ReferenceObject = FSoftObjectPath(NodeAsset);
	}
}