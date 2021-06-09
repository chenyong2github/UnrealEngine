// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IGameplayInsightsDebugView.h"
#include "Textures/SlateIcon.h"

namespace TraceServices
{
	class IAnalysisSession;
}

// Interface class which creates debug widgets
class ICreateGameplayInsightsDebugView
{
	public:
		// returns a unique name for identifying this type of widget (same value returned by IGameplayInsightsDebugView::GetName)
		virtual FName GetName() const = 0;

		// text for tab header
		virtual FText GetTitle() const = 0;

		// icon for tab header
		virtual FSlateIcon GetIcon() const = 0;

		// creates and returns a widget, which will be displayed in Rewind Debugger
		virtual TSharedPtr<IGameplayInsightsDebugView> CreateDebugView(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& InAnalysisSession) const = 0;
};

// this class handles creating debug view widgets for the Rewind Debugger.
// systems can register an ICreateGameplayInsightsDebugView implementation with a UObject type name, and when an object of that type is selected, that widget will be created and shown by the debugger.
class IGameplayInsightsDebugViewCreator
{
	public:
		// Register a creator for a Type name
		virtual void RegisterDebugViewCreator(FName TypeName, TSharedPtr<ICreateGameplayInsightsDebugView> Creator) = 0;

		// Create all views for a object Id based on it's type heirarchy
		virtual void CreateDebugViews(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& InAnalysisSession, TArray<TSharedPtr<IGameplayInsightsDebugView>>& OutDebugViews) const = 0;

		// Iterate over all registered creators and call a callback for each one
		virtual void EnumerateCreators(TFunctionRef<void(const TSharedPtr<ICreateGameplayInsightsDebugView>&)> Callback) const = 0;

		// Get a creator by its unique name
		virtual TSharedPtr<ICreateGameplayInsightsDebugView> GetCreator(FName CreatorName) const = 0;
};