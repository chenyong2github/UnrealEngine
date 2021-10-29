// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTest.h"
#include "CoreMinimal.h"
#include "AITestsCommon.h"
#include "Misc/App.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeBaker.h"
#include "Conditions/StateTreeCondition_Common.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "StateTree.h"
#include "StateTreeTestTypes.h"
#include "StateTreeExecutionContext.h"

#define LOCTEXT_NAMESPACE "AITestSuite_StateTreeTest"

PRAGMA_DISABLE_OPTIMIZATION

namespace UE::StateTree::Tests
{
	UStateTree& NewStateTree()
	{
		UStateTree* StateTree = NewObject<UStateTree>();
		check(StateTree);
		UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(StateTree);
		check(EditorData);
		StateTree->EditorData = EditorData;
		UStateTreeSchema* Schema = NewObject<UStateTreeTestSchema>();
		StateTree->SetSchema(Schema);
		return *StateTree;
	}
}

struct FStateTreeTest_MakeAndBakeStateTree : FAITestBase
{
	virtual bool InstantTest() override
	{
		UStateTree& StateTree = UE::StateTree::Tests::NewStateTree();
		UStateTreeEditorData& EditorData = *Cast<UStateTreeEditorData>(StateTree.EditorData);
		
		UStateTreeState& Root = EditorData.AddSubTree(FName(TEXT("Root")));
		UStateTreeState& StateA = Root.AddChildState(FName(TEXT("A")));
		UStateTreeState& StateB = Root.AddChildState(FName(TEXT("B")));

		// Root
		FTestEval_A& EvalA = Root.AddEvaluator<FTestEval_A>();
		
		// State A
		FTestTask_B& TaskB1 = StateA.AddTask<FTestTask_B>();
		EditorData.AddPropertyBinding(STATETREE_PROPPATH_CHECKED(EvalA, IntA), STATETREE_PROPPATH_CHECKED(TaskB1, IntB));

		FStateTreeCondition_CompareInt& IntCond = StateA.AddEnterCondition<FStateTreeCondition_CompareInt>(0, EGenericAICheck::Less, 2);
		EditorData.AddPropertyBinding(STATETREE_PROPPATH_CHECKED(EvalA, IntA), STATETREE_PROPPATH_CHECKED(IntCond, Left));

		StateA.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &StateB);

		// State B
		FTestTask_B& TaskB2 = StateB.AddTask<FTestTask_B>();
		EditorData.AddPropertyBinding(STATETREE_PROPPATH_CHECKED(EvalA, bBoolA), STATETREE_PROPPATH_CHECKED(TaskB2, bBoolB));

		FStateTreeTransition& Trans = StateB.AddTransition(EStateTreeTransitionEvent::OnCondition, EStateTreeTransitionType::GotoState, &Root);
		FStateTreeCondition_CompareFloat& TransFloatCond = Trans.AddCondition<FStateTreeCondition_CompareFloat>(0.0f, EGenericAICheck::Less, 13.0f);
		EditorData.AddPropertyBinding(STATETREE_PROPPATH_CHECKED(EvalA, FloatA), STATETREE_PROPPATH_CHECKED(TransFloatCond, Left));

		StateB.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::Succeeded);

		FStateTreeCompilerLog Log;
		FStateTreeBaker Baker(Log);
		bool bResult = Baker.Bake(StateTree);

		AITEST_TRUE("StateTree should get baked", bResult);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FStateTreeTest_MakeAndBakeStateTree, "System.AI.StateTree.MakeAndBakeStateTree");


struct FStateTreeTest_WanderLoop : FAITestBase
{
	virtual bool InstantTest() override
	{
		UStateTree& StateTree = UE::StateTree::Tests::NewStateTree();
		UStateTreeEditorData& EditorData = *Cast<UStateTreeEditorData>(StateTree.EditorData);
		
		UStateTreeState& Root = EditorData.AddSubTree(FName(TEXT("Root")));
		UStateTreeState& Wander = Root.AddChildState(FName(TEXT("Wander")));
		
		UStateTreeState& UseSmartObjectOnLane = Wander.AddChildState(FName(TEXT("UseSmartObjectOnLane")));
		UStateTreeState& WalkAlongLane = Wander.AddChildState(FName(TEXT("WalkAlongLane")));
		UStateTreeState& StandOnLane = Wander.AddChildState(FName(TEXT("StandOnLane")));
		
		UStateTreeState& WalkToSO = UseSmartObjectOnLane.AddChildState(FName(TEXT("WalkToSO")));
		UStateTreeState& UseSO = UseSmartObjectOnLane.AddChildState(FName(TEXT("UseSO")));

		// - Root

		//   \- Wander
		FTestEval_Wander& WanderEval = Wander.AddEvaluator<FTestEval_Wander>(FName(TEXT("WanderEval")));
		FTestEval_SmartObjectSensor& SmartObjectEval = Wander.AddEvaluator<FTestEval_SmartObjectSensor>();

		//      |- UseSmartObjectOnLane
		FTestTask_ReserveSmartObject& ReserveSOTask = UseSmartObjectOnLane.AddTask<FTestTask_ReserveSmartObject>(FName(TEXT("ReserveSOTask")));
		EditorData.AddPropertyBinding(STATETREE_PROPPATH_CHECKED(SmartObjectEval, PotentialSmartObjects), STATETREE_PROPPATH_CHECKED(ReserveSOTask, PotentialSmartObjects));
		FStateTreeCondition_CompareBool& ReserveHasSmartObjects = UseSmartObjectOnLane.AddEnterCondition<FStateTreeCondition_CompareBool>(false, true);
		EditorData.AddPropertyBinding(STATETREE_PROPPATH_CHECKED(SmartObjectEval, bHasSmartObjects), STATETREE_PROPPATH_CHECKED(ReserveHasSmartObjects, bLeft));
		UseSmartObjectOnLane.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &Wander); // This catches child states too

		//      |  |- WalkToSO
		FTestTask_MoveTo& MoveToSOTask = WalkToSO.AddTask<FTestTask_MoveTo>(FName(TEXT("MoveToSOTask")));
		MoveToSOTask.TicksToCompletion = 2;
		EditorData.AddPropertyBinding(STATETREE_PROPPATH_CHECKED(ReserveSOTask, ReservedSmartObjectLocation), STATETREE_PROPPATH_CHECKED(MoveToSOTask, MoveLocation));
		WalkToSO.AddTransition(EStateTreeTransitionEvent::OnSucceeded, EStateTreeTransitionType::NextState);

		//      |  \- UseSO
		FTestTask_UseSmartObject& UseSOTask = UseSO.AddTask<FTestTask_UseSmartObject>(FName(TEXT("UseSOTask")));
		UseSOTask.TicksToCompletion = 2;
		EditorData.AddPropertyBinding(STATETREE_PROPPATH_CHECKED(ReserveSOTask, ReservedSmartObject), STATETREE_PROPPATH_CHECKED(UseSOTask, SmartObject));
		// UseSO uses UseSmartObjectOnLane completed transition.

		//      |- Walk Along Lane
		FTestTask_MoveTo& MoveAlongLaneTask = WalkAlongLane.AddTask<FTestTask_MoveTo>(FName(TEXT("MoveAlongLaneTask")));
		MoveAlongLaneTask.TicksToCompletion = 2;
		EditorData.AddPropertyBinding(STATETREE_PROPPATH_CHECKED(WanderEval, WanderLocation), STATETREE_PROPPATH_CHECKED(MoveAlongLaneTask, MoveLocation));
		FStateTreeCondition_CompareBool& MoveHasWanderLoc = WalkAlongLane.AddEnterCondition<FStateTreeCondition_CompareBool>(false, true);
		EditorData.AddPropertyBinding(STATETREE_PROPPATH_CHECKED(WanderEval, bHasWanderLocation), STATETREE_PROPPATH_CHECKED(MoveHasWanderLoc, bLeft));
		WalkAlongLane.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &Wander);

		//      \- StandOnLane
		FTestTask_Stand& StandTask = StandOnLane.AddTask<FTestTask_Stand>(FName(TEXT("StandTask")));
		StandTask.TicksToCompletion = 2;
		StandOnLane.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &Wander);

		FStateTreeCompilerLog Log;
		FStateTreeBaker Baker(Log);
		bool bResult = Baker.Bake(StateTree);

		AITEST_TRUE("StateTree should get baked", bResult);

		EStateTreeRunStatus Status = EStateTreeRunStatus::Unset;
		FTestStateTreeExecutionContext Exec;
		const bool bInitSucceeded = Exec.Init(StateTree, StateTree, EStateTreeStorage::Internal);
		AITEST_TRUE("StateTree should init", bInitSucceeded);

		const FString TickStr(TEXT("Tick"));
		const FString EnterStateStr(TEXT("EnterState"));
		const FString ExitStateStr(TEXT("ExitState"));
		const FString ReserveFailedStr(TEXT("ReserveFailed"));
		
		Exec.Start();

		// No SOs on first tick, we should stand.
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.Name, TickStr));
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.Name, TickStr));
		Exec.LogClear();

		// Stand completed, SO sensor should have found SOs, Expect Move to SO
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree MoveToSOTask should enter state, and tick", Exec.Expect(MoveToSOTask.Name, EnterStateStr).Then(MoveToSOTask.Name, TickStr));
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree MoveToSOTask should tick", Exec.Expect(MoveToSOTask.Name, TickStr));
		Exec.LogClear();

		// Use SO
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree UseSOTask should enter state, and tick", Exec.Expect(UseSOTask.Name, EnterStateStr).Then(UseSOTask.Name, TickStr));
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree UseSOTask should tick", Exec.Expect(UseSOTask.Name, TickStr));
		Exec.LogClear();

		// SO done, should select Wander>UseSmartObjectOnLane>WalkToSO.
		// The next SO on the sensor is invalid, we try to reserve invalid SO, fail, then fallback to select Stand.
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree ReserveSOTask should fail enter state", Exec.Expect(ReserveSOTask.Name, EnterStateStr).Then(ReserveSOTask.Name, ReserveFailedStr));
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.Name, TickStr));
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.Name, TickStr));
		Exec.LogClear();

		// We should end up in stand again because of the failed cool down on SO sensor.
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.Name, TickStr));
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.Name, TickStr));
		Exec.LogClear();
		
		// After stand is done we should have new SO ready to use.
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree MoveToSOTask should enter state, and tick", Exec.Expect(MoveToSOTask.Name, EnterStateStr).Then(MoveToSOTask.Name, TickStr));
		Exec.LogClear();

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FStateTreeTest_WanderLoop, "System.AI.StateTree.WanderLoop");


struct FStateTreeTest_Sequence : FAITestBase
{
	virtual bool InstantTest() override
	{
		UStateTree& StateTree = UE::StateTree::Tests::NewStateTree();
		UStateTreeEditorData& EditorData = *Cast<UStateTreeEditorData>(StateTree.EditorData);
		
		UStateTreeState& Root = EditorData.AddSubTree(FName(TEXT("Root")));
		UStateTreeState& State1 = Root.AddChildState(FName(TEXT("State1")));
		UStateTreeState& State2 = Root.AddChildState(FName(TEXT("State2")));

		FTestTask_Stand& Task1 = State1.AddTask<FTestTask_Stand>(FName(TEXT("Task1")));
		State1.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::NextState);

		FTestTask_Stand& Task2 = State2.AddTask<FTestTask_Stand>(FName(TEXT("Task2")));
		State2.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::Succeeded);

		FStateTreeCompilerLog Log;
		FStateTreeBaker Baker(Log);
		const bool bResult = Baker.Bake(StateTree);
		AITEST_TRUE("StateTree should get baked", bResult);

		EStateTreeRunStatus Status = EStateTreeRunStatus::Unset;
		FTestStateTreeExecutionContext Exec;
		const bool bInitSucceeded = Exec.Init(StateTree, StateTree, EStateTreeStorage::Internal);
		AITEST_TRUE("StateTree should init", bInitSucceeded);

		const FString TickStr(TEXT("Tick"));
		const FString EnterStateStr(TEXT("EnterState"));
		const FString ExitStateStr(TEXT("ExitState"));
		
		Exec.Start();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree Task1 should enter state and tick", Exec.Expect(Task1.Name, EnterStateStr).Then(Task1.Name, TickStr));
		AITEST_FALSE("StateTree Task2 should not tick", Exec.Expect(Task2.Name, TickStr));
		AITEST_TRUE("StateTree should be running", Status == EStateTreeRunStatus::Running);
		Exec.LogClear();
		
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree Task1 should exit state", Exec.Expect(Task1.Name, ExitStateStr));
		AITEST_FALSE("StateTree Task1 should not tick", Exec.Expect(Task1.Name, TickStr));
        AITEST_TRUE("StateTree Task2 should enter state and tick", Exec.Expect(Task2.Name, EnterStateStr).Then(Task2.Name, TickStr));
        AITEST_TRUE("StateTree should be running", Status == EStateTreeRunStatus::Running);
        Exec.LogClear();
		
		Status = Exec.Tick(0.1f);
        AITEST_TRUE("StateTree Task2 should exit state", Exec.Expect(Task2.Name, ExitStateStr));
		AITEST_FALSE("StateTree Task1 should not tick", Exec.Expect(Task1.Name, TickStr));
        AITEST_FALSE("StateTree Task2 should not tick", Exec.Expect(Task2.Name, TickStr));
        AITEST_TRUE("StateTree should be completed", Status == EStateTreeRunStatus::Succeeded);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FStateTreeTest_Sequence, "System.AI.StateTree.Sequence");

struct FStateTreeTest_Select : FAITestBase
{
	virtual bool InstantTest() override
	{
		UStateTree& StateTree = UE::StateTree::Tests::NewStateTree();
		UStateTreeEditorData& EditorData = *Cast<UStateTreeEditorData>(StateTree.EditorData);
		
		UStateTreeState& Root = EditorData.AddSubTree(FName(TEXT("Root")));
		UStateTreeState& State1 = Root.AddChildState(FName(TEXT("State1")));
		UStateTreeState& State1A = State1.AddChildState(FName(TEXT("State1A")));

		FTestTask_Stand& TaskRoot = Root.AddTask<FTestTask_Stand>(FName(TEXT("TaskRoot")));
		TaskRoot.TicksToCompletion = 2;

		FTestTask_Stand& Task1 = State1.AddTask<FTestTask_Stand>(FName(TEXT("Task1")));
		Task1.TicksToCompletion = 2;

		FTestTask_Stand& Task1A = State1A.AddTask<FTestTask_Stand>(FName(TEXT("Task1A")));
		Task1A.TicksToCompletion = 2;
		State1A.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &State1);

		FStateTreeCompilerLog Log;
		FStateTreeBaker Baker(Log);
		const bool bResult = Baker.Bake(StateTree);
		AITEST_TRUE("StateTree should get baked", bResult);

		EStateTreeRunStatus Status = EStateTreeRunStatus::Unset;
		FTestStateTreeExecutionContext Exec;
		const bool bInitSucceeded = Exec.Init(StateTree, StateTree, EStateTreeStorage::Internal);
		AITEST_TRUE("StateTree should init", bInitSucceeded);

		const FString TickStr(TEXT("Tick"));
		const FString EnterStateStr(TEXT("EnterState"));
		const FString ExitStateStr(TEXT("ExitState"));

		Exec.Start();
		
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree TaskRoot should enter state, and tick", Exec.Expect(TaskRoot.Name, EnterStateStr).Then(TaskRoot.Name, TickStr));
		AITEST_TRUE("StateTree Task1 should enter state, and tick", Exec.Expect(Task1.Name, EnterStateStr).Then(Task1.Name, TickStr));
		AITEST_TRUE("StateTree Task1A should enter state, and tick", Exec.Expect(Task1A.Name, EnterStateStr).Then(Task1A.Name, TickStr));
		AITEST_TRUE("StateTree should be running", Status == EStateTreeRunStatus::Running);
		Exec.LogClear();

		// Regular tick
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree tasks should update in order", Exec.Expect(TaskRoot.Name, TickStr).Then(Task1.Name, TickStr).Then(Task1A.Name, TickStr));
		AITEST_FALSE("StateTree TaskRoot should not EnterState", Exec.Expect(TaskRoot.Name, EnterStateStr));
		AITEST_FALSE("StateTree Task1 should not EnterState", Exec.Expect(Task1.Name, EnterStateStr));
		AITEST_FALSE("StateTree Task1A should not EnterState", Exec.Expect(Task1A.Name, EnterStateStr));
		AITEST_FALSE("StateTree TaskRoot should not ExitState", Exec.Expect(TaskRoot.Name, ExitStateStr));
		AITEST_FALSE("StateTree Task1 should not ExitState", Exec.Expect(Task1.Name, ExitStateStr));
		AITEST_FALSE("StateTree Task1A should not ExitState", Exec.Expect(Task1A.Name, ExitStateStr));
		AITEST_TRUE("StateTree should be running", Status == EStateTreeRunStatus::Running);
		Exec.LogClear();

		// Partial reselect, Root should not get EnterState
		Status = Exec.Tick(0.1f);
		AITEST_FALSE("StateTree TaskRoot should not enter state", Exec.Expect(TaskRoot.Name, EnterStateStr));
		AITEST_TRUE("StateTree Task1 should exit state, and enter state", Exec.Expect(Task1A.Name, ExitStateStr).Then(Task1.Name, EnterStateStr));
		AITEST_TRUE("StateTree Task1A should exit state, enter state, and tick", Exec.Expect(Task1A.Name, ExitStateStr).Then(Task1A.Name, EnterStateStr).Then(Task1A.Name, TickStr));
		AITEST_TRUE("StateTree should be running", Status == EStateTreeRunStatus::Running);
        Exec.LogClear();

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FStateTreeTest_Select, "System.AI.StateTree.Select");


PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE
