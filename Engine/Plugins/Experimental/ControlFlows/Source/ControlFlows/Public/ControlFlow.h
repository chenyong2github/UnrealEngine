// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlFlowNode.h"
#include "HAL/Platform.h"
#include "Templates/UnrealTypeTraits.h"

class UObject;
class FControlFlowTask_Branch;

/**
 *  System/Tool to queue (asynchronous or synchronous) functions for modularity implemented via delegates.
 *  Allows code to be more easily read so there fewer 'Alt+G'-ing around to figure out what and where a class does it's thing
 *
 *  This system only supports non-const functions currently.
 *
 *  'QueueFunction': Queues a 'void (...)' function.
 *                   The Flow will execute this function and continue on with the next function in queue.
 *
 *  'QueueWait': Queues a 'void (FControlFlowNodeRef FlowHandle, ...)' function.
 *               The flow will stop until 'FlowHandle->ContinueFlow()' is called.
 *	             BE RESPONSIBLE and make sure all code paths call to continue, otherwise the flow will hang.
 *
 *  'QueueControlFlow': Queues a 'void (TSharedRef<FControlFlow> Subflow, ...)' function.
 * 
 *  'QueueStep': Usable in #UObject's or classes that derive from #TSharedFromThis<UserClass>. The Control Flow will automatically deduce if this is a
 *				 '#QueueFunction', '#QueueWait', or '#QueueControlFlow' based on the function signature.
 *
 *  Using the auto-deduction of 'QueueStep', you can change the queue from a synchronous function (QueueFunction) to an asynchronous one (QueueWait) or vice-versa
 *  by adding/removing the 'FControlFlowNodeRef FlowHandle' as your first parameter. And you can change it to (QueueControlFlow) if need be as well!
 * 
 *  Syntax:
 * 
 *  void MyFunction(...);
 *  void MyFunction(FControlFlowNodeRef FlowHandle, ...);
 *  void MyFunction(TSharedRef<FControlFlow> Subflow, ...);
 * 
 *  ControlFlowInstance->QueueStep(this, &UserClass:MyFunction1, ...);
 *  ControlFlowInstance->QueueStep(this, &UserClass:MyFunction2, ...);
 *  ControlFlowInstance->QueueStep(this, &UserClass:MyFunction3, ...);
 * 
 *  This allow ease of going from Synchronous Functionality to Asynchronously Functionality to Subflows as you build out your Flow.
 */

class CONTROLFLOWS_API FControlFlow : public TSharedFromThis<FControlFlow>
{
public:
	FControlFlow(const FString& FlowDebugName = TEXT(""));

public:
	/** This needs to be called, otherwise nothing will happen!! Call after you finish adding functions to the queue. Calling with an empty queue is safe. */
	void ExecuteFlow();
	void Reset();
	bool IsRunning() const { return CurrentNode.IsValid(); }
	size_t NumInQueue() const { return FlowQueue.Num(); }

	/** Will cancel ALL flows, both child ControlFlows and ControlFlows who owns this Flow. You've been warned. */
	void CancelFlow();

private:
	typedef UObject BindUObjectType;

public:

#define QueueFunction_Signature TMemFunPtrType<false, BindingObjectClassType, void(VarTypes...)>

	//#Delegate 'QueueFunction'
	FSimpleDelegate& QueueFunction(const FString& FlowNodeDebugName = TEXT(""));

	//#UObject 'QueueFunction'
	template <typename BindingObjectClassType, typename BindUObjectType, typename... VarTypes>
	FControlFlow& QueueStep(const FString& FlowNodeDebugName, BindingObjectClassType* InBindingObject, typename QueueFunction_Signature::Type InFunc, VarTypes... Vars)
	{
		QueueFunction(FlowNodeDebugName).BindUObject(InBindingObject, InFunc, Vars...);
		return *this;
	}

	//#TSharedFromThis 'QueueFunction'
	template <typename BindingObjectClassType, typename... VarTypes>
	FControlFlow& QueueStep(const FString& FlowNodeDebugName, TSharedRef<BindingObjectClassType> InBindingObject, typename QueueFunction_Signature::Type InFunc, VarTypes... Vars)
	{
		QueueFunction(FlowNodeDebugName).BindSP(InBindingObject, InFunc, Vars...);
		return *this;
	}

#undef QueueFunction_Signature

public:

#define QueueWait_Signature TMemFunPtrType<false, BindingObjectClassType, void(FControlFlowNodeRef FlowHandleRef, VarTypes...)>

	//#Delegate 'QueueWait'
	FControlFlowWaitDelegate& QueueWait(const FString& FlowNodeDebugName = TEXT(""));

	//#UObject 'QueueWait'
	template <typename BindingObjectClassType, typename BindUObjectType, typename... VarTypes>
	FControlFlow& QueueStep(const FString& FlowNodeDebugName, BindingObjectClassType* InBindingObject, typename QueueWait_Signature::Type InFunc, VarTypes... Vars)
	{
		QueueWait(FlowNodeDebugName).BindUObject(InBindingObject, InFunc, Vars...);
		return *this;
	}

	//#TSharedFromThis 'QueueWait'
	template <typename BindingObjectClassType, typename... VarTypes>
	FControlFlow& QueueStep(const FString& FlowNodeDebugName, TSharedRef<BindingObjectClassType> InBindingObject, typename QueueWait_Signature::Type InFunc, VarTypes... Vars)
	{
		QueueWait(FlowNodeDebugName).BindSP(InBindingObject, InFunc, Vars...);
		return *this;
	}

#undef QueueWait_Signature

public:
	/* Subflows can be used to setup branches or loops or simply re-organize your flow to be more readable. */

#define QueueControlFlow_Signature TMemFunPtrType<false, BindingObjectClassType, void(TSharedRef<FControlFlow> SubFlow, VarTypes...)>
	
	//#Delegate 'QueueWait'
	FControlFlowPopulator& QueueControlFlow(const FString& TaskName = TEXT(""), const FString& FlowNodeDebugName = TEXT(""));
	
	//#UObject 'QueueWait'
	template <typename BindingObjectClassType, typename BindUObjectType, typename... VarTypes>
	FControlFlow& QueueStep(const FString& FlowNodeDebugName, BindingObjectClassType* InBindingObject, typename QueueControlFlow_Signature::Type InFunc, VarTypes... Vars)
	{
		QueueControlFlow(FlowNodeDebugName, FlowNodeDebugName).BindUObject(InBindingObject, InFunc, Vars...);
		return *this;
	}
	
	//#TSharedFromThis 'QueueWait'
	template <typename BindingObjectClassType, typename... VarTypes>
	FControlFlow& QueueStep(const FString& FlowNodeDebugName, TSharedRef<BindingObjectClassType> InBindingObject, typename QueueControlFlow_Signature::Type InFunc, VarTypes... Vars)
	{
		QueueControlFlow(FlowNodeDebugName, FlowNodeDebugName).BindSP(InBindingObject, InFunc, Vars...);
		return *this;
	}

#undef QueueControlFlow_Signature

public:
	//#UObject deduction
	template <typename BindingObjectClassType, typename... VarTypes>
	FControlFlow& QueueStep(const FString& FlowNodeDebugName,
		typename TEnableIf<TIsDerivedFrom<BindingObjectClassType, UObject>::IsDerived, BindingObjectClassType*>::Type InBindingObject,
		VarTypes... Vars)
	{
		return QueueStep<BindingObjectClassType, BindUObjectType>(FlowNodeDebugName, InBindingObject, Vars...);
	}

	//#TSharedFromThis deduction
	template <typename BindingObjectClassType, typename... VarTypes>
	FControlFlow& QueueStep(const FString& FlowNodeDebugName,
		typename TEnableIf<TIsDerivedFrom<BindingObjectClassType, TSharedFromThis<BindingObjectClassType>>::IsDerived, BindingObjectClassType*>::Type InBindingObject,
		VarTypes... Vars)
	{
		return QueueStep<BindingObjectClassType>(FlowNodeDebugName, InBindingObject->AsShared(), Vars...);
	}

public:
	/** Optional Debug Name for 'QueueStep'. TODO: Do we make a Variadic Macro to grab the function name and use that as the debug name? */

	template <typename BindingObjectClassType, typename... VarTypes>
	typename TEnableIf<!TIsCharType<typename TRemoveConst<BindingObjectClassType>::Type>::Value,
		FControlFlow&>::Type QueueStep(BindingObjectClassType* InBindingObject, VarTypes... Vars)
	{
		return QueueStep<BindingObjectClassType>(FormatOrGetNewNodeDebugName(), InBindingObject, Forward<VarTypes>(Vars)...);
	}
	template <typename BindingObjectClassType, typename... VarTypes>
	FControlFlow& QueueStep(const char* NodeDebugName, BindingObjectClassType* InBindingObject, VarTypes... Vars)
	{
		return QueueStep<BindingObjectClassType>(FormatOrGetNewNodeDebugName(FString(NodeDebugName)), InBindingObject, Vars...);
	}
	template <typename BindingObjectClassType, typename... VarTypes>
	FControlFlow& QueueStep(const TCHAR* NodeDebugName, BindingObjectClassType* InBindingObject, VarTypes... Vars)
	{
		return QueueStep<BindingObjectClassType>(FormatOrGetNewNodeDebugName(FString(NodeDebugName)), InBindingObject, Vars...);
	}

private:
	friend class FControlFlowNode;
	friend class FControlFlowNode_RequiresCallback;
	friend class FControlFlowNode_SelfCompleting;
	friend class FControlFlowSimpleSubTask;
	friend class FControlFlowTask_Loop;
	friend class FControlFlowTask_Branch;

public:
	/** These work, but they are a bit clunky to use. The heart of the issue is that it requires the caller to define two functions. We want only the caller to use one function.
	  * Not making templated versions of them until a better API is figured out. */	

	//TODO:: Implement "#define QueueBranch_Signature TMemFunPtrType<false, BindingObjectClassType, int32(TMap<int32, TSharedRef<FControlFlow>> FlowBranches, VarTypes...)>" and delete QueueBranch

	/** Adds a branch to your flow. The flow will use FControlFlowBranchDecider to determine which flow branch to execute */
	TSharedRef<FControlFlowTask_Branch> QueueBranch(FControlFlowBranchDecider& BranchDecider, const FString& TaskName = TEXT(""), const FString& FlowNodeDebugName = TEXT(""));

	//TODO: Implement #define QueueLoop_Signature TMemFunPtrType<false, BindingObjectClassType, bool(TSharedRef<FControlFlow> SubFlow, VarTypes...)> and delete

	/** Adds a Loop to your flow. The flow will use FControlFlowLoopComplete - if this returns false, the flow will execute FControlTaskQueuePopulator until true is returned */
	FControlFlowPopulator& QueueLoop(FControlFlowLoopComplete& LoopCompleteDelgate, const FString& TaskName = TEXT(""), const FString& FlowNodeDebugName = TEXT(""));

private:

	void HandleControlFlowNodeCompleted(TSharedRef<const FControlFlowNode> NodeCompleted);

	void ExecuteNextNodeInQueue();

	FSimpleDelegate& OnComplete() const { return OnCompleteDelegate; }
	FSimpleDelegate& OnExecutedWithoutAnyNodes() const { return OnExecutedWithoutAnyNodesDelegate; }
	FSimpleDelegate& OnCancelled() const { return OnCancelledDelegate; }

	mutable FSimpleDelegate OnCompleteDelegate;
	mutable FSimpleDelegate OnExecutedWithoutAnyNodesDelegate;
	mutable FSimpleDelegate OnCancelledDelegate;

private:
	void ExecuteNode(TSharedRef<FControlFlowNode_SelfCompleting> SelfCompletingNode);

private:

	void HandleTaskNodeExecuted(TSharedRef<FControlFlowNode_Task> TaskNode);
	void HandleTaskNodeCancelled(TSharedRef<FControlFlowNode_Task> TaskNode);

	void HandleOnTaskComplete();
	void HandleOnTaskCancelled();

private:

	void LogNodeExecution(const FControlFlowNode& NodeExecuted);

	FString GetFlowPath() const;

	int32 GetRepeatedFlowCount() const;

public:
	const FString& GetDebugName() const { return DebugName; }

private:
	FString FormatOrGetNewNodeDebugName(const FString& FlowNodeDebugName = TEXT(""));

private:

	static int32 UnnamedControlFlowCounter;

	FString DebugName;

	int32 UnnamedNodeCounter = 0;

	int32 UnnamedBranchCounter = 0;

	TSharedPtr<FControlFlowNode_Task> CurrentlyRunningTask = nullptr;

	TSharedPtr<FControlFlowNode> CurrentNode = nullptr;

	//TODO: Put behind some args, because this is expensive.
	TArray<TSharedRef<FControlFlow>> SubFlowStack_ForDebugging;

	TArray<TSharedRef<FControlFlowNode>> FlowQueue;
};