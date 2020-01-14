// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/TabManager.h"

/** Interface for an Unreal Insights module. */
class IUnrealInsightsModule : public IModuleInterface
{
public:
	/**
	 * Called when the application starts in "Browser" mode.
	 */
	virtual void CreateSessionBrowser(bool bAllowDebugTools, bool bSingleProcess) = 0;

	/**
	 * Called when the application starts in "Viewer" mode.
	 */
	virtual void CreateSessionViewer(bool bAllowDebugTools) = 0;

	/**
	 * Starts analysis of the specified *.utrace file. Called when the application starts in "Viewer" mode.
	 *
	 * @param InTraceFile The file path to the *.utrace file to analyze.
	 */
	virtual void StartAnalysisForTraceFile(const TCHAR* InTraceFile) = 0;

	/**
	 * Starts analysis of the specified session. Called when the application starts in "Viewer" mode.
	 *
	 * @param InSessionId The id of the session to analyze. If nullptr, the app will wait for a live session.
	 */
	virtual void StartAnalysisForSession(const TCHAR* InSessionId) = 0;

	/**
	 * Called when the application shutsdown.
	 */
	virtual void ShutdownUserInterface() = 0;
};
