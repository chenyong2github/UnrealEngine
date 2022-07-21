// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTest.h"
#include "AITestsCommon.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeCompiler.h"
#include "Conditions/StateTreeCondition_Common.h"
#include "StateTree.h"
#include "StateTreeTestTypes.h"
#include "StateTreeExecutionContext.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "AITestSuite_StateTreeTest"

PRAGMA_DISABLE_OPTIMIZATION

namespace UE::StateTree::Tests
{
	UStateTree& NewStateTree(UObject* Outer = GetTransientPackage())
	{
		UStateTree* StateTree = NewObject<UStateTree>(Outer);
		check(StateTree);
		UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(StateTree);
		check(EditorData);
		StateTree->EditorData = EditorData;
		EditorData->Schema = NewObject<UStateTreeTestSchema>();
		return *StateTree;
	}

}

struct FStateTreeTest_MakeAndBakeStateTree : FAITestBase
{
	virtual bool InstantTest() override
	{
		UStateTree& StateTree = UE::StateTree::Tests::NewStateTree(&GetWorld());
		UStateTreeEditorData& EditorData = *Cast<UStateTreeEditorData>(StateTree.EditorData);
		
		UStateTreeState& Root = EditorData.AddSubTree(FName(TEXT("Root")));
		UStateTreeState& StateA = Root.AddChildState(FName(TEXT("A")));
		UStateTreeState& StateB = Root.AddChildState(FName(TEXT("B")));

		// Root
		auto& EvalA = EditorData.AddEvaluator<FTestEval_A>();
		
		// State A
		auto& TaskB1 = StateA.AddTask<FTestTask_B>();
		EditorData.AddPropertyBinding(FStateTreeEditorPropertyPath(EvalA.ID, TEXT("IntA")), FStateTreeEditorPropertyPath(TaskB1.ID, TEXT("IntB")));

		auto& IntCond = StateA.AddEnterCondition<FStateTreeCondition_CompareInt>(EGenericAICheck::Less);
		IntCond.GetInstance().Right = 2;

		EditorData.AddPropertyBinding(FStateTreeEditorPropertyPath(EvalA.ID, TEXT("IntA")), FStateTreeEditorPropertyPath(IntCond.ID, TEXT("Left")));

		StateA.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &StateB);

		// State B
		auto& TaskB2 = StateB.AddTask<FTestTask_B>();
		EditorData.AddPropertyBinding(FStateTreeEditorPropertyPath(EvalA.ID, TEXT("bBoolA")), FStateTreeEditorPropertyPath(TaskB2.ID, TEXT("bBoolB")));

		FStateTreeTransition& Trans = StateB.AddTransition(EStateTreeTransitionEvent::OnCondition, EStateTreeTransitionType::GotoState, &Root);
		auto& TransFloatCond = Trans.AddCondition<FStateTreeCondition_CompareFloat>(EGenericAICheck::Less);
		TransFloatCond.GetInstance().Right = 13.0f;
		EditorData.AddPropertyBinding(FStateTreeEditorPropertyPath(EvalA.ID, TEXT("FloatA")), FStateTreeEditorPropertyPath(TransFloatCond.ID, TEXT("Left")));

		StateB.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::Succeeded);

		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		const bool bResult = Compiler.Compile(StateTree);

		AITEST_TRUE("StateTree should get compiled", bResult);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FStateTreeTest_MakeAndBakeStateTree, "System.StateTree.MakeAndBakeStateTree");

#if 0
// @todo: fix this test
struct FStateTreeTest_WanderLoop : FAITestBase
{
	virtual bool InstantTest() override
	{
		UStateTree& StateTree = UE::StateTree::Tests::NewStateTree(&GetWorld());
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
		auto& WanderEval = Wander.AddEvaluator<FTestEval_Wander>(FName(TEXT("WanderEval")));
		auto& SmartObjectEval = Wander.AddEvaluator<FTestEval_SmartObjectSensor>();

		//      |- UseSmartObjectOnLane
		auto& ReserveSOTask = UseSmartObjectOnLane.AddTask<FTestTask_ReserveSmartObject>(FName(TEXT("ReserveSOTask")));
		EditorData.AddPropertyBinding(FStateTreeEditorPropertyPath(SmartObjectEval.ID, TEXT("PotentialSmartObjects")), FStateTreeEditorPropertyPath(ReserveSOTask.ID, TEXT("PotentialSmartObjects")));
		auto& ReserveHasSmartObjects = UseSmartObjectOnLane.AddEnterCondition<FStateTreeCondition_CompareBool>();
		ReserveHasSmartObjects.GetInstance().bRight = true;
		EditorData.AddPropertyBinding(FStateTreeEditorPropertyPath(SmartObjectEval.ID, TEXT("bHasSmartObjects")), FStateTreeEditorPropertyPath(ReserveHasSmartObjects.ID, TEXT("bLeft")));
		UseSmartObjectOnLane.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &Wander); // This catches child states too

		//      |  |- WalkToSO
		auto& MoveToSOTask = WalkToSO.AddTask<FTestTask_MoveTo>(FName(TEXT("MoveToSOTask")));
		MoveToSOTask.GetItem().TicksToCompletion = 2;
		EditorData.AddPropertyBinding(FStateTreeEditorPropertyPath(ReserveSOTask.ID, TEXT("ReservedSmartObjectLocation")), FStateTreeEditorPropertyPath(MoveToSOTask.ID, TEXT("MoveLocation")));
		WalkToSO.AddTransition(EStateTreeTransitionEvent::OnSucceeded, EStateTreeTransitionType::NextState);

		//      |  \- UseSO
		auto& UseSOTask = UseSO.AddTask<FTestTask_UseSmartObject>(FName(TEXT("UseSOTask")));
		UseSOTask.GetItem().TicksToCompletion = 2;
		EditorData.AddPropertyBinding(FStateTreeEditorPropertyPath(ReserveSOTask.ID, TEXT("ReservedSmartObject")), FStateTreeEditorPropertyPath(UseSOTask.ID, TEXT("SmartObject")));
		// UseSO uses UseSmartObjectOnLane completed transition.

		//      |- Walk Along Lane
		auto& MoveAlongLaneTask = WalkAlongLane.AddTask<FTestTask_MoveTo>(FName(TEXT("MoveAlongLaneTask")));
		MoveAlongLaneTask.GetItem().TicksToCompletion = 2;
		EditorData.AddPropertyBinding(FStateTreeEditorPropertyPath(WanderEval.ID, TEXT("WanderLocation")), FStateTreeEditorPropertyPath(MoveAlongLaneTask.ID, TEXT("MoveLocation")));
		auto& MoveHasWanderLoc = WalkAlongLane.AddEnterCondition<FStateTreeCondition_CompareBool>();
		MoveHasWanderLoc.GetInstance().bRight = true;
		EditorData.AddPropertyBinding(FStateTreeEditorPropertyPath(WanderEval.ID, TEXT("bHasWanderLocation")), FStateTreeEditorPropertyPath(MoveHasWanderLoc.ID, TEXT("bLeft")));
		WalkAlongLane.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &Wander);

		//      \- StandOnLane
		auto& StandTask = StandOnLane.AddTask<FTestTask_Stand>(FName(TEXT("StandTask")));
		StandTask.GetItem().TicksToCompletion = 2;
		StandOnLane.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &Wander);

		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		bool bResult = Compiler.Compile(StateTree);

		AITEST_TRUE("StateTree should get compiled", bResult);

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
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.GetName(), TickStr));
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.GetName(), TickStr));
		Exec.LogClear();

		// Stand completed, SO sensor should have found SOs, Expect Move to SO
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree MoveToSOTask should enter state, and tick", Exec.Expect(MoveToSOTask.GetName(), EnterStateStr).Then(MoveToSOTask.GetName(), TickStr));
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree MoveToSOTask should tick", Exec.Expect(MoveToSOTask.GetName(), TickStr));
		Exec.LogClear();

		// Use SO
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree UseSOTask should enter state, and tick", Exec.Expect(UseSOTask.GetName(), EnterStateStr).Then(UseSOTask.GetName(), TickStr));
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree UseSOTask should tick", Exec.Expect(UseSOTask.GetName(), TickStr));
		Exec.LogClear();

		// SO done, should select Wander>UseSmartObjectOnLane>WalkToSO.
		// The next SO on the sensor is invalid, we try to reserve invalid SO, fail, then fallback to select Stand.
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree ReserveSOTask should fail enter state", Exec.Expect(ReserveSOTask.GetName(), EnterStateStr).Then(ReserveSOTask.GetName(), ReserveFailedStr));
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.GetName(), TickStr));
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.GetName(), TickStr));
		Exec.LogClear();

		// We should end up in stand again because of the failed cool down on SO sensor.
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.GetName(), TickStr));
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree StandTask should tick", Exec.Expect(StandTask.GetName(), TickStr));
		Exec.LogClear();
		
		// After stand is done we should have new SO ready to use.
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree MoveToSOTask should enter state, and tick", Exec.Expect(MoveToSOTask.GetName(), EnterStateStr).Then(MoveToSOTask.GetName(), TickStr));
		Exec.LogClear();

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FStateTreeTest_WanderLoop, "System.StateTree.WanderLoop");
#endif

struct FStateTreeTest_Sequence : FAITestBase
{
	virtual bool InstantTest() override
	{
		UStateTree& StateTree = UE::StateTree::Tests::NewStateTree(&GetWorld());
		UStateTreeEditorData& EditorData = *Cast<UStateTreeEditorData>(StateTree.EditorData);
		
		UStateTreeState& Root = EditorData.AddSubTree(FName(TEXT("Root")));
		UStateTreeState& State1 = Root.AddChildState(FName(TEXT("State1")));
		UStateTreeState& State2 = Root.AddChildState(FName(TEXT("State2")));

		auto& Task1 = State1.AddTask<FTestTask_Stand>(FName(TEXT("Task1")));
		State1.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::NextState);

		auto& Task2 = State2.AddTask<FTestTask_Stand>(FName(TEXT("Task2")));
		State2.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::Succeeded);

		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		const bool bResult = Compiler.Compile(StateTree);
		AITEST_TRUE("StateTree should get compiled", bResult);

		EStateTreeRunStatus Status = EStateTreeRunStatus::Unset;
		FTestStateTreeExecutionContext Exec;
		const bool bInitSucceeded = Exec.Init(StateTree, StateTree, EStateTreeStorage::Internal);
		AITEST_TRUE("StateTree should init", bInitSucceeded);

		const FString TickStr(TEXT("Tick"));
		const FString EnterStateStr(TEXT("EnterState"));
		const FString ExitStateStr(TEXT("ExitState"));
		
		Status = Exec.Start();
		AITEST_TRUE("StateTree Task1 should enter state", Exec.Expect(Task1.GetName(), EnterStateStr));
		AITEST_FALSE("StateTree Task1 should not tick", Exec.Expect(Task1.GetName(), TickStr));
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree Task1 should tick, and exit state", Exec.Expect(Task1.GetName(), TickStr).Then(Task1.GetName(), ExitStateStr));
		AITEST_TRUE("StateTree Task2 should enter state", Exec.Expect(Task2.GetName(), EnterStateStr));
		AITEST_FALSE("StateTree Task2 should not tick", Exec.Expect(Task2.GetName(), TickStr));
		AITEST_TRUE("StateTree should be running", Status == EStateTreeRunStatus::Running);
		Exec.LogClear();
		
		Status = Exec.Tick(0.1f);
        AITEST_TRUE("StateTree Task2 should tick, and exit state", Exec.Expect(Task2.GetName(), TickStr).Then(Task2.GetName(), ExitStateStr));
		AITEST_FALSE("StateTree Task1 should not tick", Exec.Expect(Task1.GetName(), TickStr));
        AITEST_TRUE("StateTree should be completed", Status == EStateTreeRunStatus::Succeeded);
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_FALSE("StateTree Task1 should not tick", Exec.Expect(Task1.GetName(), TickStr));
		AITEST_FALSE("StateTree Task2 should not tick", Exec.Expect(Task2.GetName(), TickStr));
		Exec.LogClear();

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FStateTreeTest_Sequence, "System.StateTree.Sequence");

struct FStateTreeTest_Select : FAITestBase
{
	virtual bool InstantTest() override
	{
		UStateTree& StateTree = UE::StateTree::Tests::NewStateTree(&GetWorld());
		UStateTreeEditorData& EditorData = *Cast<UStateTreeEditorData>(StateTree.EditorData);
		
		UStateTreeState& Root = EditorData.AddSubTree(FName(TEXT("Root")));
		UStateTreeState& State1 = Root.AddChildState(FName(TEXT("State1")));
		UStateTreeState& State1A = State1.AddChildState(FName(TEXT("State1A")));

		auto& TaskRoot = Root.AddTask<FTestTask_Stand>(FName(TEXT("TaskRoot")));
		TaskRoot.GetItem().TicksToCompletion = 2;

		auto& Task1 = State1.AddTask<FTestTask_Stand>(FName(TEXT("Task1")));
		Task1.GetItem().TicksToCompletion = 2;

		auto& Task1A = State1A.AddTask<FTestTask_Stand>(FName(TEXT("Task1A")));
		Task1A.GetItem().TicksToCompletion = 2;
		State1A.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &State1);

		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		const bool bResult = Compiler.Compile(StateTree);
		AITEST_TRUE("StateTree should get compiled", bResult);

		EStateTreeRunStatus Status = EStateTreeRunStatus::Unset;
		FTestStateTreeExecutionContext Exec;
		const bool bInitSucceeded = Exec.Init(StateTree, StateTree, EStateTreeStorage::Internal);
		AITEST_TRUE("StateTree should init", bInitSucceeded);

		const FString TickStr(TEXT("Tick"));
		const FString EnterStateStr(TEXT("EnterState"));
		const FString ExitStateStr(TEXT("ExitState"));

		// Start and enter state
		Status = Exec.Start();
		AITEST_TRUE("StateTree TaskRoot should enter state", Exec.Expect(TaskRoot.GetName(), EnterStateStr));
		AITEST_TRUE("StateTree Task1 should enter state", Exec.Expect(Task1.GetName(), EnterStateStr));
		AITEST_TRUE("StateTree Task1A should enter state", Exec.Expect(Task1A.GetName(), EnterStateStr));
		AITEST_FALSE("StateTree TaskRoot should not tick", Exec.Expect(TaskRoot.GetName(), TickStr));
		AITEST_FALSE("StateTree Task1 should not tick", Exec.Expect(Task1.GetName(), TickStr));
		AITEST_FALSE("StateTree Task1A should not tick", Exec.Expect(Task1A.GetName(), TickStr));
		AITEST_TRUE("StateTree should be running", Status == EStateTreeRunStatus::Running);
		Exec.LogClear();

		// Regular tick
		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree tasks should update in order", Exec.Expect(TaskRoot.GetName(), TickStr).Then(Task1.GetName(), TickStr).Then(Task1A.GetName(), TickStr));
		AITEST_FALSE("StateTree TaskRoot should not EnterState", Exec.Expect(TaskRoot.GetName(), EnterStateStr));
		AITEST_FALSE("StateTree Task1 should not EnterState", Exec.Expect(Task1.GetName(), EnterStateStr));
		AITEST_FALSE("StateTree Task1A should not EnterState", Exec.Expect(Task1A.GetName(), EnterStateStr));
		AITEST_FALSE("StateTree TaskRoot should not ExitState", Exec.Expect(TaskRoot.GetName(), ExitStateStr));
		AITEST_FALSE("StateTree Task1 should not ExitState", Exec.Expect(Task1.GetName(), ExitStateStr));
		AITEST_FALSE("StateTree Task1A should not ExitState", Exec.Expect(Task1A.GetName(), ExitStateStr));
		AITEST_TRUE("StateTree should be running", Status == EStateTreeRunStatus::Running);
		Exec.LogClear();

		// Partial reselect, Root should not get EnterState
		Status = Exec.Tick(0.1f);
		AITEST_FALSE("StateTree TaskRoot should not enter state", Exec.Expect(TaskRoot.GetName(), EnterStateStr));
		AITEST_TRUE("StateTree Task1 should tick, exit state, and enter state", Exec.Expect(Task1.GetName(), TickStr).Then(Task1.GetName(), ExitStateStr).Then(Task1.GetName(), EnterStateStr));
		AITEST_TRUE("StateTree Task1A should tick, exit state, and enter state", Exec.Expect(Task1A.GetName(), TickStr).Then(Task1A.GetName(), ExitStateStr).Then(Task1A.GetName(), EnterStateStr));
		AITEST_TRUE("StateTree should be running", Status == EStateTreeRunStatus::Running);
        Exec.LogClear();

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FStateTreeTest_Select, "System.StateTree.Select");


struct FStateTreeTest_FailEnterState : FAITestBase
{
	virtual bool InstantTest() override
	{
		UStateTree& StateTree = UE::StateTree::Tests::NewStateTree(&GetWorld());
		UStateTreeEditorData& EditorData = *Cast<UStateTreeEditorData>(StateTree.EditorData);
		
		UStateTreeState& Root = EditorData.AddSubTree(FName(TEXT("Root")));
		UStateTreeState& State1 = Root.AddChildState(FName(TEXT("State1")));
		UStateTreeState& State1A = State1.AddChildState(FName(TEXT("State1A")));

		auto& TaskRoot = Root.AddTask<FTestTask_Stand>(FName(TEXT("TaskRoot")));

		auto& Task1 = State1.AddTask<FTestTask_Stand>(FName(TEXT("Task1")));
		auto& Task2 = State1.AddTask<FTestTask_Stand>(FName(TEXT("Task2")));
		Task2.GetItem().EnterStateResult = EStateTreeRunStatus::Failed;
		auto& Task3 = State1.AddTask<FTestTask_Stand>(FName(TEXT("Task3")));

		auto& Task1A = State1A.AddTask<FTestTask_Stand>(FName(TEXT("Task1A")));
		State1A.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &State1);

		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		const bool bResult = Compiler.Compile(StateTree);
		AITEST_TRUE("StateTree should get compiled", bResult);

		EStateTreeRunStatus Status = EStateTreeRunStatus::Unset;
		FTestStateTreeExecutionContext Exec;
		const bool bInitSucceeded = Exec.Init(StateTree, StateTree, EStateTreeStorage::Internal);
		AITEST_TRUE("StateTree should init", bInitSucceeded);

		const FString TickStr(TEXT("Tick"));
		const FString EnterStateStr(TEXT("EnterState"));
		const FString ExitStateStr(TEXT("ExitState"));
		const FString StateCompletedStr(TEXT("StateCompleted"));

		// Start and enter state
		Status = Exec.Start();
		AITEST_TRUE("StateTree TaskRoot should enter state", Exec.Expect(TaskRoot.GetName(), EnterStateStr));
		AITEST_TRUE("StateTree Task1 should enter state", Exec.Expect(Task1.GetName(), EnterStateStr));
		AITEST_TRUE("StateTree Task2 should enter state", Exec.Expect(Task2.GetName(), EnterStateStr));
		AITEST_FALSE("StateTree Task3 should not enter state", Exec.Expect(Task3.GetName(), EnterStateStr));
		AITEST_TRUE("StateTree Should execute StateCompleted in reverse order", Exec.Expect(Task2.GetName(), StateCompletedStr).Then(Task1.GetName(), StateCompletedStr).Then(TaskRoot.GetName(), StateCompletedStr));
		AITEST_FALSE("StateTree Task3 should not state complete", Exec.Expect(Task3.GetName(), StateCompletedStr));
		AITEST_TRUE("StateTree exec status should be failed", Exec.GetLastTickStatus() == EStateTreeRunStatus::Failed);
		Exec.LogClear();

		// Stop and exit state
		Status = Exec.Stop();
		AITEST_TRUE("StateTree TaskRoot should exit state", Exec.Expect(TaskRoot.GetName(), ExitStateStr));
		AITEST_TRUE("StateTree Task1 should exit state", Exec.Expect(Task1.GetName(), ExitStateStr));
		AITEST_TRUE("StateTree Task2 should exit state", Exec.Expect(Task2.GetName(), ExitStateStr));
		AITEST_FALSE("StateTree Task3 should not exit state", Exec.Expect(Task3.GetName(), ExitStateStr));
		Exec.LogClear();

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FStateTreeTest_FailEnterState, "System.StateTree.FailEnterState");

struct FStateTreeTest_SubTree : FAITestBase
{
	virtual bool InstantTest() override
	{
		UStateTree& StateTree = UE::StateTree::Tests::NewStateTree(&GetWorld());
		UStateTreeEditorData& EditorData = *Cast<UStateTreeEditorData>(StateTree.EditorData);
		
		UStateTreeState& Root = EditorData.AddSubTree(FName(TEXT("Root")));
		UStateTreeState& State1 = Root.AddChildState(FName(TEXT("State1")), EStateTreeStateType::Linked);
		UStateTreeState& State2 = Root.AddChildState(FName(TEXT("State2")));
		UStateTreeState& State3 = Root.AddChildState(FName(TEXT("State3")), EStateTreeStateType::Subtree);
		UStateTreeState& State3A = State3.AddChildState(FName(TEXT("State3A")));
		UStateTreeState& State3B = State3.AddChildState(FName(TEXT("State3B")));

		State1.LinkedState.Set(&State3);

		auto& Task2 = State2.AddTask<FTestTask_Stand>(FName(TEXT("Task2")));

		auto& Task3A = State3A.AddTask<FTestTask_Stand>(FName(TEXT("Task3A")));
		State3A.AddTransition(EStateTreeTransitionEvent::OnCompleted, EStateTreeTransitionType::GotoState, &State3B);

		auto& Task3B = State3B.AddTask<FTestTask_Stand>(FName(TEXT("Task3B")));


		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		const bool bResult = Compiler.Compile(StateTree);
		AITEST_TRUE("StateTree should get compiled", bResult);

		EStateTreeRunStatus Status = EStateTreeRunStatus::Unset;
		FTestStateTreeExecutionContext Exec;
		const bool bInitSucceeded = Exec.Init(StateTree, StateTree, EStateTreeStorage::Internal);
		AITEST_TRUE("StateTree should init", bInitSucceeded);

		const FString TickStr(TEXT("Tick"));
		const FString EnterStateStr(TEXT("EnterState"));
		const FString ExitStateStr(TEXT("ExitState"));
		const FString StateCompletedStr(TEXT("StateCompleted"));

		// Start and enter state
		Status = Exec.Start();

		AITEST_TRUE("StateTree Active States should be in Root/State1/State3/State3A", Exec.ExpectInActiveStates(Root.Name, State1.Name, State3.Name, State3A.Name));
		AITEST_TRUE("StateTree Task2 should enter state", !Exec.Expect(Task2.GetName(), EnterStateStr));
		AITEST_TRUE("StateTree Task3A should enter state", Exec.Expect(Task3A.GetName(), EnterStateStr));
		AITEST_TRUE("StateTree should be running", Status == EStateTreeRunStatus::Running);
		Exec.LogClear();

		Status = Exec.Tick(0.1f);
		AITEST_TRUE("StateTree Active States should be in Root/State1/State3/State3B", Exec.ExpectInActiveStates(Root.Name, State1.Name, State3.Name, State3B.Name));
		AITEST_TRUE("StateTree Task3B should enter state", Exec.Expect(Task3B.GetName(), EnterStateStr));
		AITEST_TRUE("StateTree should be running", Status == EStateTreeRunStatus::Running);
		
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FStateTreeTest_SubTree, "System.StateTree.SubTree");

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE
