// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessageData.h"
#include "ScopedSessionDatabase.h"
#include "HistoryEdition/ActivityGraphIDs.h"
#include "HistoryEdition/ActivityNode.h"
#include "Misc/AutomationTest.h"

namespace UE::ConcertSyncTests
{
	TArray<ConcertSyncCore::FActivityNodeID> ValidateEachActivityHasNode(
		FAutomationTestBase& Test,
		const TArray<int64>& ActivityMappings,
		const ConcertSyncCore::FActivityDependencyGraph& Graph,
		uint32 ActivityCount,
		TFunctionRef<FString(uint32 ActivityType)> LexToString)
	{
		using namespace ConcertSyncCore;
		
		TArray<FActivityNodeID> ActivityNodes;
		ActivityNodes.SetNumZeroed(ActivityCount);
		for (uint32 ActivityType = 0; ActivityType < ActivityCount; ++ActivityType)
		{
			const int64 ActivityId = ActivityMappings[ActivityType];
			const TOptional<FActivityNodeID> NodeID = Graph.FindNodeByActivity(ActivityId);
			if (!NodeID.IsSet())
			{
				Test.AddError(FString::Printf(TEXT("No node generated for activity type %s"), *LexToString(ActivityType)));
				continue;
			}
			
			if (!NodeID.IsSet())
			{
				Test.AddError(FString::Printf(TEXT("Graph has invalid state. Node ID %lld is invalid for activity type %s"), NodeID->ID, *LexToString(ActivityType)));
				continue;
			}
			
			ActivityNodes[ActivityType] = *NodeID;
		}

		return ActivityNodes;
	}

	/**
	 * Helps fill the database
	 */
	class FTestActivityBuilder
	{
		TArray<FActivityID> Activities;
		FScopedSessionDatabase& SessionDatabase;
	public:

		FTestActivityBuilder(FScopedSessionDatabase& SessionDatabase, uint32 ActivityCount)
			: SessionDatabase(SessionDatabase)
		{
			Activities.SetNumUninitialized(ActivityCount);
		}

		TArray<FActivityID> GetActivities() const { return Activities; }

		bool NewMap(FName MapName, uint32 ActivityIndex)
		{
			FConcertSyncActivity NewPackage;
			NewPackage.EndpointId = SessionDatabase.GetEndpoint();
			FConcertPackageInfo PackageInfo;
			FConcertPackageDataStream PackageDataStream;
			PackageInfo.PackageName = MapName;
			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Added;
			SessionDatabase.GetTransactionMaxEventId(PackageInfo.TransactionEventIdAtSave);
			int64 Dummy;
			return SessionDatabase.AddPackageActivity(NewPackage, PackageInfo, PackageDataStream, Activities[ActivityIndex], Dummy);
		}

		bool SaveMap(FName MapName, uint32 ActivityIndex)
		{
			FConcertSyncActivity SaveFooPackage;
			SaveFooPackage.EndpointId = SessionDatabase.GetEndpoint();
			FConcertPackageInfo PackageInfo;
			FConcertPackageDataStream PackageDataStream;
			PackageInfo.PackageName = MapName;
			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Saved;
			SessionDatabase.GetTransactionMaxEventId(PackageInfo.TransactionEventIdAtSave);
			int64 Dummy;
			return SessionDatabase.AddPackageActivity(SaveFooPackage, PackageInfo, PackageDataStream, Activities[ActivityIndex], Dummy);
		}

		bool DeleteMap(FName MapName, uint32 ActivityIndex)
		{
			FConcertSyncActivity DeleteFooPackage;
			DeleteFooPackage.EndpointId = SessionDatabase.GetEndpoint();
			FConcertPackageInfo PackageInfo;
			FConcertPackageDataStream PackageDataStream;
			PackageInfo.PackageName = MapName;
			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Deleted;
			SessionDatabase.GetTransactionMaxEventId(PackageInfo.TransactionEventIdAtSave);
			int64 Dummy;
			return SessionDatabase.AddPackageActivity(DeleteFooPackage, PackageInfo, PackageDataStream, Activities[ActivityIndex], Dummy);
		}

		bool RenameMap(FName OldMapName, FName NewMapName, uint32 SaveActivityIndex, uint32 RenameActivityIndex)
		{
			int64 Dummy;
			
			FConcertSyncActivity SaveFoo2Package;
			SaveFoo2Package.EndpointId = SessionDatabase.GetEndpoint();
			FConcertPackageInfo PackageInfo;
			FConcertPackageDataStream PackageDataStream;
			PackageInfo.PackageName = NewMapName;
			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Saved;
			SessionDatabase.GetTransactionMaxEventId(PackageInfo.TransactionEventIdAtSave);
			bool bSuccess = SessionDatabase.AddPackageActivity(SaveFoo2Package, PackageInfo, PackageDataStream, Activities[SaveActivityIndex], Dummy);
			
			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Renamed;
			PackageInfo.PackageName = OldMapName;
			PackageInfo.NewPackageName = NewMapName;
			bSuccess &= SessionDatabase.AddPackageActivity(SaveFoo2Package, PackageInfo, PackageDataStream, Activities[RenameActivityIndex], Dummy);

			return bSuccess;
		}

		bool CreateActor(FName MapName, uint32 ActivityIndex, FName ActorName = EName::Actor)
		{
			FConcertExportedObject Actor;
			Actor.ObjectId.ObjectName = ActorName;
			Actor.ObjectId.ObjectPackageName = MapName;
			Actor.ObjectId.ObjectOuterPathName = *FString::Printf(TEXT("%s:PersistentLevel"), *MapName.ToString());
			Actor.ObjectId.ObjectClassPathName = TEXT("/Script/Engine.StaticMeshActor");
			
			FConcertSyncTransactionActivity CreateActorActivity;
			CreateActorActivity.EndpointId = SessionDatabase.GetEndpoint();
			CreateActorActivity.EventData.Transaction.TransactionId = FGuid::NewGuid();
			CreateActorActivity.EventData.Transaction.OperationId = FGuid::NewGuid();
			Actor.ObjectData.bAllowCreate = true;
			CreateActorActivity.EventData.Transaction.ExportedObjects = { Actor };
			CreateActorActivity.EventData.Transaction.ModifiedPackages = { MapName };
			SessionDatabase.GetTransactionMaxEventId(CreateActorActivity.EventId);
			int64 Dummy;
			return SessionDatabase.AddTransactionActivity(CreateActorActivity, Activities[ActivityIndex], Dummy);
		}
	};
}
