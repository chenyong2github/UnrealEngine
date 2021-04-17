// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/**
 * Auxiliary class. Responsible for terminating application.
 */
class FDisplayClusterAppExit
{
public:
	enum class EExitType
	{
		// Kills current process. No resource cleaning performed.
		KillImmediately,
		// UE based soft exit (game thread). Full resource cleaning.
		NormalSoft,
		// UE game termination. Error window and dump file should appear after exit.
		NormalForce
	};

public:
	static void ExitApplication(EExitType ExitType, const FString& Msg);

private:
	static auto ExitTypeToStr(EExitType ExitType);

private:
	static FCriticalSection InternalsSyncScope;
};
