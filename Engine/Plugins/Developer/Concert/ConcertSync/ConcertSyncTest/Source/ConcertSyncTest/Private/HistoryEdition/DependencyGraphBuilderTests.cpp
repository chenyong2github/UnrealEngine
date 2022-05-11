// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertLogGlobal.h"

#include "ConcertSyncSessionDatabase.h"
#include "HistoryEdition/DebugDependencyGraph.h"
#include "HistoryEdition/DependencyGraphBuilder.h"
#include "RenameEditAndDeleteMapsFlow.h"
#include "ScopedSessionDatabase.h"

#include "Misc/AutomationTest.h"

namespace UE::ConcertSyncTests::RenameEditAndDeleteMapsFlowTest
{
	/** Validates that the graph reflects the expected dependencies. */
	bool ValidateExpectedDependencies(FAutomationTestBase& Test, const TTestActivityArray<int64>& ActivityMappings, const ConcertSyncCore::FActivityDependencyGraph& Graph);
	/** Validates that each activity has a node in the dependency graph. */
	TTestActivityArray<ConcertSyncCore::FActivityNodeID> ValidateEachActivityHasNode(FAutomationTestBase& Test, const TTestActivityArray<int64>& ActivityMappings, const ConcertSyncCore::FActivityDependencyGraph& Graph);
	
	/**
	 * Builds the dependency graph from a typical sequence of events.
	 *
	 * Sequence of user actions:
	 *	1 Create map Foo
	 *	2 Add actor A
	 *	3 Rename actor A
	 *	4 Edit actor A
	 *	5 Rename map to Bar
	 *	6 Edit actor A
	 *	7 Delete map Bar
	 *	8 Create map Bar
	 *
	 *	The dependency graph should look like this:
	 *	2 -> 1 (PackageCreation)
	 *	3 -> 2 (EditPossiblyDependsOnPackage)
	 *	4 -> 3 (EditPossiblyDependsOnPackage)
	 *	5 -> 1 (PackageCreation)
	 *	6 -> 5 (PackageRename)
	 *	7 -> 5 (PackageRename)
	 *	8 -> 5 (PackageRemoval)
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRenameEditAndDeleteMapsFlowTest, "Concert.History.BuildGraph.RenameEditAndDeleteMapsFlow", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FRenameEditAndDeleteMapsFlowTest::RunTest(const FString& Parameters)
	{
		FScopedSessionDatabase SessionDatabase(*this);
		const TTestActivityArray<int64> Activities = CreateActivityHistory(SessionDatabase, SessionDatabase.GetEndpoint());
		
		const ConcertSyncCore::FActivityDependencyGraph DependencyGraph = ConcertSyncCore::BuildDependencyGraphFrom(SessionDatabase);
		UE_LOG(LogConcert, Log, TEXT("%s tested graph in Graphviz format:\n\n%s"), *GetTestFullName(), *ConcertSyncCore::Graphviz::ExportToGraphviz(DependencyGraph, SessionDatabase));
		return ValidateExpectedDependencies(*this, Activities, DependencyGraph);
	}
	
	bool ValidateExpectedDependencies(FAutomationTestBase& Test, const TTestActivityArray<int64>& ActivityMappings, const ConcertSyncCore::FActivityDependencyGraph& Graph)
	{
		using namespace ConcertSyncCore;
		
		const TTestActivityArray<FActivityNodeID> ActivityNodes
			= ValidateEachActivityHasNode(Test, ActivityMappings, Graph);

		// 1 Create map Foo
		{
			Test.TestFalse(TEXT("1 Creating new package 'Foo' has no dependencies."), Graph.GetNodeById(ActivityNodes[_1_NewPackageFoo]).HasAnyDependency());
			Test.TestTrue(TEXT("1 Creating new package 'Foo' has correct node flags"), Graph.GetNodeById(ActivityNodes[_1_NewPackageFoo]).GetNodeFlags() == EActivityNodeFlags::None);
			
			Test.TestTrue(TEXT("1 Saving 'Foo' has dependency to creating package 'Foo'."), Graph.GetNodeById(ActivityNodes[_1_SavePackageFoo]).DependsOnActivity(ActivityMappings[_1_NewPackageFoo], Graph, EActivityDependencyReason::PackageCreation, EDependencyStrength::HardDependency));
			Test.TestTrue(TEXT("1 Saving 'Foo' has correct node flags"), Graph.GetNodeById(ActivityNodes[_1_SavePackageFoo]).GetNodeFlags() == EActivityNodeFlags::None);
			Test.TestEqual(TEXT("1 Saving 'Foo' has exactly 1 dependency"), Graph.GetNodeById(ActivityNodes[_1_SavePackageFoo]).GetDependencies().Num(), 1);
		}

		// 2 Add actor A
		{
			Test.TestTrue(TEXT("2 Adding actor to 'Foo' depends on creating package 'Foo'."), Graph.GetNodeById(ActivityNodes[_2_AddActor]).DependsOnActivity(ActivityMappings[_1_NewPackageFoo], Graph, EActivityDependencyReason::PackageCreation, EDependencyStrength::HardDependency));
			Test.TestTrue(TEXT("2 Adding actor to 'Foo' has correct node flags"), Graph.GetNodeById(ActivityNodes[_2_AddActor]).GetNodeFlags() == EActivityNodeFlags::None);
			Test.TestEqual(TEXT("2 Saving 'Foo' has exactly 1 dependency"), Graph.GetNodeById(ActivityNodes[_2_AddActor]).GetDependencies().Num(), 1);
		}

		// 3 Rename actor A
		{
			// It must be a EDependencyStrength::HardDependency because you cannot edit the actor without having created it 
			Test.TestTrue(TEXT("3 Renaming actor depends on having created the actor."), Graph.GetNodeById(ActivityNodes[_3_RenameActor]).DependsOnActivity(ActivityMappings[_2_AddActor], Graph, EActivityDependencyReason::SubobjectCreation, EDependencyStrength::HardDependency));
			Test.TestTrue(TEXT("3 Renaming actor to 'Foo' has correct node flags"), Graph.GetNodeById(ActivityNodes[_3_RenameActor]).GetNodeFlags() == EActivityNodeFlags::None);
			Test.TestEqual(TEXT("3 Renaming actor has exactly 1 dependency"), Graph.GetNodeById(ActivityNodes[_3_RenameActor]).GetDependencies().Num(), 1);
		}

		// 4 Edit actor A
		{
			// The previous edit might have affected us (e.g. this activity may have executed the construction script)
			// Note: This should not have a hard dependency on having renamed the actor because a rename is just a property change of ActorLabel.
			Test.TestTrue(TEXT("4 Editing actor may depend on having edited the actor previously."), Graph.GetNodeById(ActivityNodes[_4_EditActor]).DependsOnActivity(ActivityMappings[_3_RenameActor], Graph, EActivityDependencyReason::EditAfterPreviousPackageEdit, EDependencyStrength::PossibleDependency));
			// This activity must have a EDependencyStrength::HardDependency to _2_AddActor because the edit cannot happen without having created the actor
			Test.TestTrue(TEXT("4 Editing actor depends on having created the actor."), Graph.GetNodeById(ActivityNodes[_4_EditActor]).DependsOnActivity(ActivityMappings[_2_AddActor], Graph, EActivityDependencyReason::SubobjectCreation, EDependencyStrength::HardDependency));
			Test.TestTrue(TEXT("4 Editing actor has correct node flags"), Graph.GetNodeById(ActivityNodes[_4_EditActor]).GetNodeFlags() == EActivityNodeFlags::None);
			Test.TestEqual(TEXT("4 Editing actor has exactly 1 dependency"), Graph.GetNodeById(ActivityNodes[_4_EditActor]).GetDependencies().Num(), 2);
		}

		// 5 Rename map to Bar
		{
			Test.TestFalse(TEXT("5 Saving new package 'Bar' has no dependencies."), Graph.GetNodeById(ActivityNodes[_5_SavePackageBar]).HasAnyDependency());
			Test.TestTrue(TEXT("5 Saving new package 'Bar' has correct node flags"), Graph.GetNodeById(ActivityNodes[_5_SavePackageBar]).GetNodeFlags() == EActivityNodeFlags::None);
			
			Test.TestTrue(TEXT("5 Renaming 'Foo' to 'Bar' has dependency to creating package 'Foo'."), Graph.GetNodeById(ActivityNodes[_5_RenameFooToBar]).DependsOnActivity(ActivityMappings[_1_NewPackageFoo], Graph, EActivityDependencyReason::PackageCreation, EDependencyStrength::HardDependency));
			Test.TestTrue(TEXT("5 Renaming 'Foo' to 'Bar' has dependency to creating package 'Bar'."), Graph.GetNodeById(ActivityNodes[_5_RenameFooToBar]).DependsOnActivity(ActivityMappings[_5_SavePackageBar], Graph, EActivityDependencyReason::PackageCreation, EDependencyStrength::PossibleDependency));
			Test.TestTrue(TEXT("5 Renaming 'Foo' to 'Bar' has correct node flags"), Graph.GetNodeById(ActivityNodes[_5_RenameFooToBar]).GetNodeFlags() == EActivityNodeFlags::RenameActivity);
			Test.TestEqual(TEXT("5 Saving 'Foo' has exactly 2 dependencies"), Graph.GetNodeById(ActivityNodes[_5_RenameFooToBar]).GetDependencies().Num(), 2);
		}

		// 6 Edit actor A
		{
			Test.TestTrue(TEXT("6 Editing actor in 'Bar' depends on having renamed 'Foo' to 'Bar'."), Graph.GetNodeById(ActivityNodes[_6_EditActor]).DependsOnActivity(ActivityMappings[_5_RenameFooToBar], Graph, EActivityDependencyReason::PackageRename, EDependencyStrength::HardDependency));
			Test.TestTrue(TEXT("6 Editing actor has correct node flags"), Graph.GetNodeById(ActivityNodes[_6_EditActor]).GetNodeFlags() == EActivityNodeFlags::None);
			Test.TestEqual(TEXT("6 Editing actor has exactly 1 dependency"), Graph.GetNodeById(ActivityNodes[_6_EditActor]).GetDependencies().Num(), 1);
		}

		// 7 Delete map Bar
		{
			Test.TestTrue(TEXT("7 Deleting 'Bar' depends on having renamed 'Foo' to 'Bar' previously."), Graph.GetNodeById(ActivityNodes[_7_DeleteBar]).DependsOnActivity(ActivityMappings[_5_RenameFooToBar], Graph, EActivityDependencyReason::PackageRename, EDependencyStrength::HardDependency));
			Test.TestTrue(TEXT("7 Deleting 'Bar' has correct node flags"), Graph.GetNodeById(ActivityNodes[_7_DeleteBar]).GetNodeFlags() == EActivityNodeFlags::None);
			Test.TestEqual(TEXT("7 Deleting 'Bar' after rename has exactly 1 dependency"), Graph.GetNodeById(ActivityNodes[_7_DeleteBar]).GetDependencies().Num(), 1);
		}

		// 8 Create map Bar
		{
			Test.TestTrue(TEXT("8 Re-creating 'Bar' depends on having deleted 'Bar' previously."), Graph.GetNodeById(ActivityNodes[_8_NewPackageFoo]).DependsOnActivity(ActivityMappings[_7_DeleteBar], Graph, EActivityDependencyReason::PackageRemoval, EDependencyStrength::HardDependency));
			Test.TestTrue(TEXT("8 Re-creating actor has correct node flags"), Graph.GetNodeById(ActivityNodes[_8_NewPackageFoo]).GetNodeFlags() == EActivityNodeFlags::None);
			Test.TestEqual(TEXT("8 Re-creating 'Bar' 1 dependency"), Graph.GetNodeById(ActivityNodes[_8_NewPackageFoo]).GetDependencies().Num(), 1);
			
			Test.TestTrue(TEXT("8 Saving 'Bar' depends on re-created 'Bar'."), Graph.GetNodeById(ActivityNodes[_8_SavePackageFoo]).DependsOnActivity(ActivityMappings[_8_NewPackageFoo], Graph, EActivityDependencyReason::PackageCreation, EDependencyStrength::HardDependency));
			Test.TestTrue(TEXT("8 Saving 'Bar' has correct node flags"), Graph.GetNodeById(ActivityNodes[_8_SavePackageFoo]).GetNodeFlags() == EActivityNodeFlags::None);
			Test.TestEqual(TEXT("8 Saving 'Bar' after re-creation has exactly 1 dependency"), Graph.GetNodeById(ActivityNodes[_8_SavePackageFoo]).GetDependencies().Num(), 1);
		}
		
		return true;
	}

	TTestActivityArray<ConcertSyncCore::FActivityNodeID> ValidateEachActivityHasNode(FAutomationTestBase& Test, const TTestActivityArray<int64>& ActivityMappings, const ConcertSyncCore::FActivityDependencyGraph& Graph)
	{
		using namespace ConcertSyncCore;
		
		TTestActivityArray<FActivityNodeID> ActivityNodes;
		ActivityNodes.SetNumZeroed(ActivityCount);
		for (int32 ActivityType = 0; ActivityType < ActivityCount; ++ActivityType)
		{
			const int64 ActivityId = ActivityMappings[ActivityType];
			const TOptional<FActivityNodeID> NodeID = Graph.FindNodeByActivity(ActivityId);
			if (!NodeID.IsSet())
			{
				Test.AddError(FString::Printf(TEXT("No node generated for activity %s"), *LexToString(static_cast<ETestActivity>(ActivityType))));
				continue;
			}
			
			const FActivityNode& Node = Graph.GetNodeById(*NodeID);
			if (!NodeID.IsSet())
			{
				Test.AddError(FString::Printf(TEXT("Graph has invalid state. Node ID %lld is invalid for activity %s"), NodeID->ID, *LexToString(static_cast<ETestActivity>(ActivityType))));
				continue;
			}
			
			ActivityNodes[ActivityType] = *NodeID;
		}

		return ActivityNodes;
	}
}

namespace UE::ConcertSyncTests::DeletingAndRecreatingActorIsHardDependency
{
	enum ETestActivity
	{
		CreateActor,
		DeleteActor,
		RecreateActor,

		ActivityCount
	};
	
	TArray<FActivityID> FillDatabase(FScopedSessionDatabase& Database);
	FConcertExportedObject CreateActorMetaData(FName OuterLevelPath);
	
	/**
	 * 1. Create actor A
	 * 2. Delete Actor A
	 * 3. Re-create actor A.
	 *
	 * 3 > 2 is a hard dependency (removing 2 would result in attempting to create the actor twice, which is invalid).
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeletingAndRecreatingActorIsHardDependency, "Concert.History.BuildGraph.DeletingAndRecreatingActorIsHardDependency", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FDeletingAndRecreatingActorIsHardDependency::RunTest(const FString& Parameters)
	{
		using namespace ConcertSyncCore;
		
		FScopedSessionDatabase SessionDatabase(*this);
		const TArray<FActivityID> TestActivities = FillDatabase(SessionDatabase);

		const FActivityDependencyGraph DependencyGraph = BuildDependencyGraphFrom(SessionDatabase);
		UE_LOG(LogConcert, Log, TEXT("%s tested graph in Graphviz format:\n\n%s"), *GetTestFullName(), *ConcertSyncCore::Graphviz::ExportToGraphviz(DependencyGraph, SessionDatabase));

		const TOptional<FActivityNodeID> CreateActorNodeID = DependencyGraph.FindNodeByActivity(TestActivities[CreateActor]);
		const TOptional<FActivityNodeID> DeleteActorNodeID = DependencyGraph.FindNodeByActivity(TestActivities[DeleteActor]);
		const TOptional<FActivityNodeID> RecreateActorNodeID = DependencyGraph.FindNodeByActivity(TestActivities[RecreateActor]);
		if (!CreateActorNodeID || !DeleteActorNodeID || !RecreateActorNodeID)
		{
			AddError(TEXT("Activities not registered"));
			return false;
		}

		const FActivityNode& CreatedActorNode = DependencyGraph.GetNodeById(*CreateActorNodeID);
		const FActivityNode& DeleteActorNode = DependencyGraph.GetNodeById(*DeleteActorNodeID);
		const FActivityNode& RecreateActorNode = DependencyGraph.GetNodeById(*RecreateActorNodeID);

		TestEqual(TEXT("CreatedActorNode->GetDependencies().Num() == 0"), CreatedActorNode.GetDependencies().Num(), 0);
		TestEqual(TEXT("DeleteActorNode->GetDependencies().Num() == 1"), DeleteActorNode.GetDependencies().Num(), 1);
		TestEqual(TEXT("RecreateActorNode->GetDependencies().Num() == 1"), RecreateActorNode.GetDependencies().Num(), 1);

		TestTrue(TEXT("DeleteActorNode depends on CreatedActorNode"), DeleteActorNode.DependsOnActivity(TestActivities[CreateActor], DependencyGraph, EActivityDependencyReason::SubobjectCreation, EDependencyStrength::HardDependency));
		TestTrue(TEXT("RecreateActorNode depends on DeleteActorNode"), RecreateActorNode.DependsOnActivity(TestActivities[DeleteActor], DependencyGraph, EActivityDependencyReason::SubobjectRemoval, EDependencyStrength::HardDependency));

		return true;
	}
	
	TArray<FActivityID> FillDatabase(FScopedSessionDatabase& SessionDatabase)
	{
		TArray<FActivityID> ActivityIDs;
		ActivityIDs.SetNumUninitialized(ActivityCount);

		const FName FooLevel = TEXT("/Game/Foo");
		int64 Dummy;
		
		// 1 Create actor
		{
			FConcertSyncTransactionActivity CreateActorActivity;
			CreateActorActivity.EndpointId = SessionDatabase.GetEndpoint();
			CreateActorActivity.EventData.Transaction.TransactionId = FGuid::NewGuid();
			CreateActorActivity.EventData.Transaction.OperationId = FGuid::NewGuid();
			FConcertExportedObject Actor = CreateActorMetaData(FooLevel);
			Actor.ObjectData.bAllowCreate = true;
			CreateActorActivity.EventData.Transaction.ExportedObjects = { Actor };
			CreateActorActivity.EventData.Transaction.ModifiedPackages = { FooLevel };
			SessionDatabase.GetTransactionMaxEventId(CreateActorActivity.EventId);
			SessionDatabase.AddTransactionActivity(CreateActorActivity, ActivityIDs[CreateActor], Dummy);
		}
		
		// 2 Delete actor
		{
			FConcertSyncTransactionActivity RemoveActorActivity;
			RemoveActorActivity.EndpointId = SessionDatabase.GetEndpoint();
			RemoveActorActivity.EventData.Transaction.TransactionId = FGuid::NewGuid();
			RemoveActorActivity.EventData.Transaction.OperationId = FGuid::NewGuid();
			FConcertExportedObject Actor = CreateActorMetaData(FooLevel);
			Actor.ObjectData.bIsPendingKill = true;
			RemoveActorActivity.EventData.Transaction.ExportedObjects = { Actor };
			RemoveActorActivity.EventData.Transaction.ModifiedPackages = { FooLevel };
			SessionDatabase.GetTransactionMaxEventId(RemoveActorActivity.EventId);
			SessionDatabase.AddTransactionActivity(RemoveActorActivity, ActivityIDs[DeleteActor], Dummy);
		}
		
		// 3 Re-create actor
		{
			FConcertSyncTransactionActivity CreateActorActivity;
			CreateActorActivity.EndpointId = SessionDatabase.GetEndpoint();
			CreateActorActivity.EventData.Transaction.TransactionId = FGuid::NewGuid();
			CreateActorActivity.EventData.Transaction.OperationId = FGuid::NewGuid();
			FConcertExportedObject Actor = CreateActorMetaData(FooLevel);
			Actor.ObjectData.bAllowCreate = true;
			CreateActorActivity.EventData.Transaction.ExportedObjects = { Actor };
			CreateActorActivity.EventData.Transaction.ModifiedPackages = { FooLevel };
			SessionDatabase.GetTransactionMaxEventId(CreateActorActivity.EventId);
			SessionDatabase.AddTransactionActivity(CreateActorActivity, ActivityIDs[RecreateActor], Dummy);
		}
		
		return ActivityIDs;
	}
	
	FConcertExportedObject CreateActorMetaData(FName OuterLevelPath)
	{
		FConcertExportedObject Result;
		Result.ObjectId.ObjectName = TEXT("SomeTestActorName42");
		Result.ObjectId.ObjectPackageName = OuterLevelPath;
		Result.ObjectId.ObjectOuterPathName = *FString::Printf(TEXT("%s:PersistentLevel"), *OuterLevelPath.ToString());
		Result.ObjectId.ObjectClassPathName = TEXT("/Script/Engine.StaticMeshActor");
		return Result;
	}
}

namespace UE::ConcertSyncTests::IndirectPackageDependencyTest
{
	/**
	 * This tests that potential indirect dependencies are handled.
	 *
	 * Sequence of user actions:
	 *  1 Create data asset A
	 *  2 Make actor reference A
	 *  3 Edit data asset
	 *  4 Edit actor
	 *
	 * The dependency graph should look like this:
	 *  2 -> 1 (PackageCreation)
	 *  3 -> 1 (PackageCreation)
	 *  4 -> 1 (EditPossiblyDependsOnPackage)
	 *  4 -> 2 (EditPossiblyDependsOnPackage)
	 *
	 * This is relevant because the actor's construction script may depend query data from the data asset.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIndirectPackageDependencyTest, "Concert.History.BuildGraph.IndirectPackageDependencyTest", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FIndirectPackageDependencyTest::RunTest(const FString& Parameters)
	{
		// TODO:
		return true;
	}
}