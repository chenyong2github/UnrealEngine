// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskCreateSceneObjects.h"

#include "CoreMinimal.h"

#include "Async/TaskGraphInterfaces.h"
#include "Engine/World.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "PackageUtils/PackageUtils.h"
#include "Stats/Stats.h"
#include "Templates/SharedPointer.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtrTemplates.h"

UE::Interchange::FTaskCreateSceneObjects::FTaskCreateSceneObjects(const FString& InPackageBasePath, const int32 InSourceIndex, TWeakPtr<FImportAsyncHelper, ESPMode::ThreadSafe> InAsyncHelper,
	TArrayView<UInterchangeBaseNode*> InNodes, UInterchangeFactoryBase* InFactory, bool bInCreateSceneObjectsForChildren)
	: PackageBasePath(InPackageBasePath)
	, SourceIndex(InSourceIndex)
	, WeakAsyncHelper(InAsyncHelper)
	, Nodes(InNodes)
	, Factory(InFactory)
	, bCreateSceneObjectsForChildren(bInCreateSceneObjectsForChildren)
{
	check(Factory);
}

ENamedThreads::Type UE::Interchange::FTaskCreateSceneObjects::GetDesiredThread()
{
	return Factory->CanExecuteOnAnyThread() ? ENamedThreads::AnyBackgroundThreadNormalTask : ENamedThreads::GameThread;
}

void UE::Interchange::FTaskCreateSceneObjects::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(SpawnActor)
#endif

	TOptional<FGCScopeGuard> GCScopeGuard;
	if (!IsInGameThread())
	{
		GCScopeGuard.Emplace();
	}

	TSharedPtr<UE::Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(WeakAsyncHelper.IsValid());

	//Verify if the task was canceled
	if (AsyncHelper->bCancel)
	{
		return;
	}

	for (UInterchangeBaseNode* Node : Nodes)
	{
		FString NodeDisplayName = Node->GetDisplayLabel();
		SanitizeObjectName(NodeDisplayName);

		UInterchangeFactoryBase::FCreateSceneObjectsParams CreateSceneObjectsParams;
		CreateSceneObjectsParams.ObjectName = NodeDisplayName;
		CreateSceneObjectsParams.ObjectNode = Node;
		CreateSceneObjectsParams.Level = GWorld->GetCurrentLevel();
		CreateSceneObjectsParams.bCreateSceneObjectsForChildren = bCreateSceneObjectsForChildren;

		if (AsyncHelper->BaseNodeContainers.IsValidIndex(SourceIndex))
		{
			CreateSceneObjectsParams.NodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
		}

		TMap<FString, UObject*> SceneObjects = Factory->CreateSceneObjects(CreateSceneObjectsParams);

		for (const TPair<FString, UObject*>& SceneObject : SceneObjects)
		{
			if (UObject* NodeObject = SceneObject.Value)
			{
				FScopeLock Lock(&AsyncHelper->ImportedSceneObjectsPerSourceIndexLock);
				TArray<UE::Interchange::FImportAsyncHelper::FImportedObjectInfo>& ImportedInfos = AsyncHelper->ImportedSceneObjectsPerSourceIndex.FindOrAdd(SourceIndex);
				UE::Interchange::FImportAsyncHelper::FImportedObjectInfo* ImportedInfoPtr = ImportedInfos.FindByPredicate([NodeObject](const UE::Interchange::FImportAsyncHelper::FImportedObjectInfo& CurInfo)
					{
						return CurInfo.ImportedObject == NodeObject;
					});

				if (!ImportedInfoPtr)
				{
					UE::Interchange::FImportAsyncHelper::FImportedObjectInfo& ObjectInfo = ImportedInfos.AddDefaulted_GetRef();
					ObjectInfo.ImportedObject = NodeObject;
					ObjectInfo.Factory = Factory;
					ObjectInfo.FactoryNode = Node;
				}

				if (CreateSceneObjectsParams.NodeContainer)
				{
					if (const UInterchangeBaseNode* ActorNode = CreateSceneObjectsParams.NodeContainer->GetNode(SceneObject.Key))
					{
						ActorNode->ReferenceObject = FSoftObjectPath(SceneObject.Value);
					}
				}
			}
		}
	}
}