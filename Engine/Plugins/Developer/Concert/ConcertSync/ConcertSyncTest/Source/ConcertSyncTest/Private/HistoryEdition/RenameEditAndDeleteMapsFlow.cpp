// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenameEditAndDeleteMapsFlow.h"

#include "ConcertLogGlobal.h"
#include "ConcertSyncSessionDatabase.h"
#include "ScopedSessionDatabase.h"

namespace UE::ConcertSyncTests::RenameEditAndDeleteMapsFlowTest
{
	template<typename T>
	T MakeActivity(const FGuid& EndpointID)
	{
		T Activity;
		Activity.EndpointId = EndpointID;
		return Activity;
	}

	TSet<ETestActivity> AllActivities()
	{
		return {
			_1_NewPackageFoo,
			_1_SavePackageFoo,
			_2_AddActor,
			_3_RenameActor,
			_4_EditActor,
			_5_SavePackageBar,
			_5_RenameFooToBar,
			_6_EditActor,
			_7_DeleteBar,
			_8_NewPackageFoo,
			_8_SavePackageFoo
		};
	}

	FString LexToString(ETestActivity Activity)
	{
		switch (Activity)
		{
		case _1_NewPackageFoo: return TEXT("\"1 New package Foo\"");
		case _1_SavePackageFoo: return TEXT("\"1 Saved package Foo\"");
		case _2_AddActor: return TEXT("\"2 Create actor\"");
		case _3_RenameActor: return TEXT("\"3 Edit actor\"");
		case _4_EditActor: return TEXT("\"4 Edit actor\"");
		case _5_SavePackageBar: return TEXT("\"5 Save package\"");
		case _5_RenameFooToBar: return TEXT("\"5 Rename Foo to Bar\"");
		case _6_EditActor: return TEXT("\"6 Edit actor\"");
		case _7_DeleteBar: return TEXT("\"7 Delete package Bar\"");
		case _8_NewPackageFoo: return TEXT("\"8 Create package Bar\"");
		case _8_SavePackageFoo: return TEXT("\"8 Save package Bar\"");
				
		case ActivityCount:
		default:
			checkNoEntry();
			return FString();
		}
	}

	struct FCreatedStaticMeshActor
	{
		FConcertExportedObject Actor;
		FConcertExportedObject StaticMeshComponent;
	};
	
	FCreatedStaticMeshActor CreateEditedActor(FName OuterLevelPath);

	TTestActivityArray<int64> CreateActivityHistory(FConcertSyncSessionDatabase& SessionDatabase, const FGuid& EndpointID)
	{
		TTestActivityArray<int64> ActivityIDs;
		TTestActivityArray<int64> PackageEventIDs;
		ActivityIDs.SetNumUninitialized(ActivityCount);
		PackageEventIDs.SetNumUninitialized(ActivityCount);
		
		bool bAllSucceeded = true;
		
		// The names of the activities make it into the generated Graphviz graph 
		const FName EditedActorName = TEXT("Actor");
		const FName FooLevel = TEXT("/Game/Foo");
		const FName BarLevel = TEXT("/Game/Bar");
		
		// 1 Create map Foo
		{
			FConcertSyncActivity NewPackage = MakeActivity<FConcertSyncActivity>(EndpointID);
			FConcertPackageInfo PackageInfo;
			FConcertPackageDataStream PackageDataStream;
			PackageInfo.PackageName = FooLevel;
			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Added;
			SessionDatabase.GetTransactionMaxEventId(PackageInfo.TransactionEventIdAtSave);
			bAllSucceeded &= SessionDatabase.AddPackageActivity(NewPackage, PackageInfo, PackageDataStream, ActivityIDs[_1_NewPackageFoo], PackageEventIDs[_1_NewPackageFoo]);

			FConcertSyncActivity SavePackage = MakeActivity<FConcertSyncActivity>(EndpointID);
			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Saved;
			SessionDatabase.GetTransactionMaxEventId(PackageInfo.TransactionEventIdAtSave);
			bAllSucceeded &= SessionDatabase.AddPackageActivity(SavePackage, PackageInfo, PackageDataStream, ActivityIDs[_1_SavePackageFoo], PackageEventIDs[_1_SavePackageFoo]);
		}

		// 2 Add actor A
		{
			FConcertSyncTransactionActivity CreateActor = MakeActivity<FConcertSyncTransactionActivity>(EndpointID);
			CreateActor.EventData.Transaction.TransactionId = FGuid::NewGuid();
			CreateActor.EventData.Transaction.OperationId = FGuid::NewGuid();
			FCreatedStaticMeshActor NewActorData = CreateEditedActor(FooLevel);
			NewActorData.Actor.ObjectData.bAllowCreate = true;
			NewActorData.StaticMeshComponent.ObjectData.bAllowCreate = true;
			CreateActor.EventData.Transaction.ExportedObjects = { NewActorData.StaticMeshComponent, NewActorData.Actor };
			CreateActor.EventData.Transaction.ModifiedPackages = { FooLevel };
			SessionDatabase.GetTransactionMaxEventId(CreateActor.EventId);
			bAllSucceeded &= SessionDatabase.AddTransactionActivity(CreateActor, ActivityIDs[_2_AddActor], PackageEventIDs[_2_AddActor]);
		}

		// 3 Rename actor A
		{
			FConcertSyncTransactionActivity EditActor = MakeActivity<FConcertSyncTransactionActivity>(EndpointID);
			EditActor.EventData.Transaction.TransactionId = FGuid::NewGuid();
			EditActor.EventData.Transaction.OperationId = FGuid::NewGuid();
			
			FCreatedStaticMeshActor NewActorData = CreateEditedActor(FooLevel);
			NewActorData.Actor.PropertyDatas = { { TEXT("ActorLabel"), {} } };
			EditActor.EventData.Transaction.ExportedObjects = { NewActorData.Actor };
			EditActor.EventData.Transaction.ModifiedPackages = { FooLevel };
			SessionDatabase.GetTransactionMaxEventId(EditActor.EventId);
			bAllSucceeded &= SessionDatabase.AddTransactionActivity(EditActor, ActivityIDs[_3_RenameActor], PackageEventIDs[_3_RenameActor]);
		}

		// 4 Edit actor A
		{
			FConcertSyncTransactionActivity EditActor = MakeActivity<FConcertSyncTransactionActivity>(EndpointID);
			EditActor.EventData.Transaction.TransactionId = FGuid::NewGuid();
			EditActor.EventData.Transaction.OperationId = FGuid::NewGuid();
			FCreatedStaticMeshActor NewActorData = CreateEditedActor(FooLevel);
			NewActorData.StaticMeshComponent.PropertyDatas = { { TEXT("RelativeLocation"), {} } };
			EditActor.EventData.Transaction.ExportedObjects = { NewActorData.StaticMeshComponent };
			EditActor.EventData.Transaction.ModifiedPackages = { FooLevel };
			SessionDatabase.GetTransactionMaxEventId(EditActor.EventId);
			bAllSucceeded &= SessionDatabase.AddTransactionActivity(EditActor, ActivityIDs[_4_EditActor], PackageEventIDs[_4_EditActor]);
		}

		// 5 Rename map to Bar
		{
			FConcertSyncActivity SaveBarPackage = MakeActivity<FConcertSyncActivity>(EndpointID);
			FConcertPackageInfo PackageInfo;
			FConcertPackageDataStream PackageDataStream;
			PackageInfo.PackageName = BarLevel;
			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Saved;
			SessionDatabase.GetTransactionMaxEventId(PackageInfo.TransactionEventIdAtSave);
			bAllSucceeded &= SessionDatabase.AddPackageActivity(SaveBarPackage, PackageInfo, PackageDataStream, ActivityIDs[_5_SavePackageBar], PackageEventIDs[_5_SavePackageBar]);

			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Renamed;
			PackageInfo.PackageName = FooLevel;
			PackageInfo.NewPackageName = BarLevel;
			bAllSucceeded &= SessionDatabase.AddPackageActivity(SaveBarPackage, PackageInfo, PackageDataStream, ActivityIDs[_5_RenameFooToBar], PackageEventIDs[_5_RenameFooToBar]);
		}

		// 6 Edit actor A
		{
			FConcertSyncTransactionActivity EditActor = MakeActivity<FConcertSyncTransactionActivity>(EndpointID);
			EditActor.EventData.Transaction.TransactionId = FGuid::NewGuid();
			EditActor.EventData.Transaction.OperationId = FGuid::NewGuid();
			FCreatedStaticMeshActor NewActorData = CreateEditedActor(BarLevel);
			NewActorData.StaticMeshComponent.PropertyDatas = { { TEXT("RelativeLocation"), {} } };
			EditActor.EventData.Transaction.ExportedObjects = { NewActorData.StaticMeshComponent };
			EditActor.EventData.Transaction.ModifiedPackages = { BarLevel };
			SessionDatabase.GetTransactionMaxEventId(EditActor.EventId);
			bAllSucceeded &= SessionDatabase.AddTransactionActivity(EditActor, ActivityIDs[_6_EditActor], PackageEventIDs[_6_EditActor]);
		}

		// 7 Delete map Bar
		{
			FConcertSyncActivity SaveBarPackage = MakeActivity<FConcertSyncActivity>(EndpointID);
			FConcertPackageInfo PackageInfo;
			FConcertPackageDataStream PackageDataStream;
			PackageInfo.PackageName = BarLevel;
			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Deleted;
			SessionDatabase.GetTransactionMaxEventId(PackageInfo.TransactionEventIdAtSave);
			bAllSucceeded &= SessionDatabase.AddPackageActivity(SaveBarPackage, PackageInfo, PackageDataStream, ActivityIDs[_7_DeleteBar], PackageEventIDs[_7_DeleteBar]);
		}
		
		// 8 Create map Bar
		{
			FConcertSyncActivity NewPackage = MakeActivity<FConcertSyncActivity>(EndpointID);
			FConcertPackageInfo PackageInfo;
			FConcertPackageDataStream PackageDataStream;
			PackageInfo.PackageName = TEXT("/Game/Bar");
			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Added;
			SessionDatabase.GetTransactionMaxEventId(PackageInfo.TransactionEventIdAtSave);
			bAllSucceeded &= SessionDatabase.AddPackageActivity(NewPackage, PackageInfo, PackageDataStream, ActivityIDs[_8_NewPackageFoo], PackageEventIDs[_8_NewPackageFoo]);

			FConcertSyncActivity SavePackage = MakeActivity<FConcertSyncActivity>(EndpointID);
			PackageInfo.PackageUpdateType = EConcertPackageUpdateType::Saved;
			SessionDatabase.GetTransactionMaxEventId(PackageInfo.TransactionEventIdAtSave);
			bAllSucceeded &= SessionDatabase.AddPackageActivity(SavePackage, PackageInfo, PackageDataStream, ActivityIDs[_8_SavePackageFoo], PackageEventIDs[_8_SavePackageFoo]);
		}

		if (!bAllSucceeded)
		{
			UE_LOG(LogConcert, Error, TEXT("Something went wrong creating the activities. Test result may be wrong."))
		}
		return ActivityIDs;
	}

	FCreatedStaticMeshActor CreateEditedActor(FName OuterLevelPath)
	{
		FCreatedStaticMeshActor Result;

		Result.Actor.ObjectId.ObjectName = TEXT("StaticMeshActor0");
		Result.Actor.ObjectId.ObjectPackageName = OuterLevelPath;
		Result.Actor.ObjectId.ObjectOuterPathName = *FString::Printf(TEXT("%s:PersistentLevel"), *OuterLevelPath.ToString());
		Result.Actor.ObjectId.ObjectClassPathName = TEXT("/Script/Engine.StaticMeshActor");
		
		Result.StaticMeshComponent.ObjectId.ObjectName = TEXT("StaticMeshComponent0");
		Result.StaticMeshComponent.ObjectId.ObjectPackageName = OuterLevelPath;
		Result.StaticMeshComponent.ObjectId.ObjectOuterPathName = *FString::Printf(TEXT("%s:PersistentLevel.%s"), *OuterLevelPath.ToString(), *Result.Actor.ObjectId.ObjectName.ToString());
		Result.StaticMeshComponent.ObjectId.ObjectClassPathName = TEXT("/Script/Engine.StaticMeshComponent");
		
		return Result;
	}
}

