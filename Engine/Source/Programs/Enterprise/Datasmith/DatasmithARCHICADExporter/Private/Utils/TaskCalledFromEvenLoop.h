// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

BEGIN_NAMESPACE_UE_AC

// Schedule an idle task
class FTaskCalledFromEvenLoop
{
  public:
	// Destructor
	virtual ~FTaskCalledFromEvenLoop() {}

	// Run the task
	virtual void Run() = 0;

	// If the task is retain or not
	enum ERetainType
	{
		kSharedRef = 0, // Retain the task
		kWeakPtr // The task is retain by another reference or we will not call it
	};

	// Schedule InTask to be executed on next event.
	static void CallTaskFromEvenLoop(const TSharedRef< FTaskCalledFromEvenLoop >& InTask, ERetainType InRetainType);

	// Schedule functor to be executed on next event.
	template < typename Functor > static void CallFunctorFromEvenLoop(Functor InFunctor);

	// Register the task service
	static GSErrCode Register();

	// Initialize
	static GSErrCode Initialize();

	// Uninitialize the task service
	static void Uninitialize();

  private:
	static GSErrCode DoTasks(GSHandle ParamHandle);
	static GSErrCode DoTasksCallBack(GSHandle ParamHandle, GSPtr OutResultData, bool bSilentMode);
	static void		 DeleteParamHandle(GSHandle ParamHandle);
};

// Template class for function
template < typename Functor > class TFunctorCalledFromEventLoop : public FTaskCalledFromEvenLoop
{
  public:
    // Constructor
	TFunctorCalledFromEventLoop(Functor InFunctor)
		: TheFunctor(InFunctor)
	{
	}

    // Execute the functor
	virtual void Run() override { TheFunctor(); }

  private:
	Functor TheFunctor;
};

// Implementation of schedule functor to be executed on next event.
template < typename Functor > void FTaskCalledFromEvenLoop::CallFunctorFromEvenLoop(Functor InFunctor)
{
	CallTaskFromEvenLoop(MakeShared< TFunctorCalledFromEventLoop< Functor > >(InFunctor), kSharedRef);
}

END_NAMESPACE_UE_AC
