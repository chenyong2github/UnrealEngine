// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_STATETREE_DEBUGGER

#include "Debugger/StateTreeTraceTypes.h"
#include "Widgets/SCompoundWidget.h"

namespace UE::StateTreeDebugger { struct FFrameIndexSpan; }
struct FStateTreeDebugger;
struct FStateTreeInstanceDebugId;
class FStateTreeEditor;
class FStateTreeViewModel;
class FUICommandList;
class IMessageLogListing;
class UStateTree;

class SStateTreeDebuggerView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SStateTreeDebuggerView) {}
	SLATE_END_ARGS()

	SStateTreeDebuggerView();
	~SStateTreeDebuggerView();

	void Construct(const FArguments& InArgs, const UStateTree* InStateTree, TSharedRef<FStateTreeViewModel> InStateTreeViewModel, TSharedRef<FUICommandList> InCommandList);

private:
	TSharedRef<SWidget> OnGetDebuggerInstancesMenu() const;
	TSharedRef<SWidget> OnGetDebuggerTracesMenu() const;

	void OnPIEStarted(bool bIsSimulating) const;
	void OnPIEStopped(bool bIsSimulating) const;
	void OnPIEPaused(bool bIsSimulating) const;
	void OnPIEResumed(bool bIsSimulating) const;
	void OnPIESingleStepped(bool bIsSimulating) const;
	
	void OnTracesUpdated(const TConstArrayView<const FStateTreeTraceEventVariantType> Events, const TConstArrayView<UE::StateTreeDebugger::FFrameIndexSpan> Spans) const;
	void OnBreakpointHit(const FStateTreeInstanceDebugId InstanceId, const FStateTreeStateHandle StateHandle, const TSharedPtr<FUICommandList> ActionList) const;

	void BindDebuggerToolbarCommands(const TSharedRef<FUICommandList> ToolkitCommands);

	/**
	 * Stores raw log information so it can be processed
	 */
	struct FPendingLogMessage
	{
		FString Message;
		ELogVerbosity::Type Verbosity = ELogVerbosity::NoLogging;
		FName Category;

		FPendingLogMessage()
		{
		}

		FPendingLogMessage(const TCHAR* InMessage, const ELogVerbosity::Type InVerbosity, const FName& InCategory)
			: Message(InMessage)
			, Verbosity(InVerbosity)
			, Category(InCategory)
		{
		}
	};

	bool CanStepBack() const;
	void StepBack() const;

	bool CanStepForward() const;
	void StepForward() const;
	
	bool CanToggleBreakpoint() const;
	void ToggleBreakpoint() const;

	TSharedPtr<FStateTreeDebugger> Debugger;
	TSharedPtr<FStateTreeViewModel> StateTreeViewModel;
	TWeakObjectPtr<const UStateTree> StateTree;
	
	TSharedPtr<SWidget> DebuggerTraces;
	TSharedPtr<IMessageLogListing> DebuggerTraceListing;
};

#endif // WITH_STATETREE_DEBUGGER