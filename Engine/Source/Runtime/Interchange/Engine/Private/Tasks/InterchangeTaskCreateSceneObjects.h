// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Nodes/InterchangeBaseNode.h"

class UInterchangeBaseNode;
class UInterchangeFactoryBase;

namespace UE
{
	namespace Interchange
	{
		class FImportAsyncHelper;

		class FTaskCreateSceneObjects
		{
		private:
			FString PackageBasePath;
			int32 SourceIndex;
			TWeakPtr<FImportAsyncHelper, ESPMode::ThreadSafe> WeakAsyncHelper;
			TArray<UInterchangeBaseNode*> Nodes;
			UInterchangeFactoryBase* Factory;
			bool bCreateSceneObjectsForChildren = false;

		public:
			explicit FTaskCreateSceneObjects(const FString& InPackageBasePath, const int32 InSourceIndex, TWeakPtr<FImportAsyncHelper, ESPMode::ThreadSafe> InAsyncHelper, TArrayView<UInterchangeBaseNode*> InNodes,
				UInterchangeFactoryBase* InFactory, bool bInCreateSceneObjectsForChildren);

			ENamedThreads::Type GetDesiredThread();

			static ESubsequentsMode::Type GetSubsequentsMode()
			{
				return ESubsequentsMode::TrackSubsequents;
			}

			TStatId GetStatId() const
			{
				RETURN_QUICK_DECLARE_CYCLE_STAT(FTaskCreateAsset, STATGROUP_TaskGraphTasks);
			}

			void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);
		};


	} //ns Interchange
}//ns UE
