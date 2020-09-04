// Copyright Epic Games, Inc. All Rights Reserved.

#include "4MLManager.h"
#include "CoreGlobals.h"
#include "Misc/CoreDelegates.h"
#include "Engine/Engine.h"
#include "GameFramework/GameModeBase.h"
#include "4MLTypes.h"
#include "4MLAsync.h"
#include "4MLSession.h"
#include "4MLJson.h"
#include "Agents/4MLAgent.h"
#include "Sensors/4MLSensor.h"
#include "UE4MLSettings.h"
#include <string>
#if WITH_EDITORONLY_DATA
#include "Editor.h"
#endif // WITH_EDITORONLY_DATA

#include "RPCWrapper/Server.h"


void U4MLManager::ConfigureAsServer(FRPCServer& Server)
{
	UE_LOG(LogUE4ML, Log, TEXT("\tconfiguring as server"));

	AddCommonFunctions(Server);

#if WITH_RPCLIB
	Server.bind("enable_manual_world_tick", [this](bool bEnable) {
		SetManualWorldTickEnabled(bEnable);
	});
	Librarian.AddRPCFunctionDescription(TEXT("enable_manual_world_tick"), TEXT("(), Controlls whether the world is running real time or it\'s being ticked manually with calls to \'step\' or \'request_world_tick\' functions. Default is \'real time\'."));

	Server.bind("request_world_tick", [this](int32 TickCount, bool bWaitForWorldTick) {
		if (bTickWorldManually == false)
		{
			return;
		}
		StepsRequested = TickCount;
		while (bWaitForWorldTick && StepsRequested > 0)
		{
			FPlatformProcess::Sleep(0.f);
		}
		return;
	});
	Librarian.AddRPCFunctionDescription(TEXT("request_world_tick"), TEXT("(int TickCount, bool bWaitForWorldTick), Requests a TickCount world ticks. This has meaning only if \'enable_manual_world_tick(true)\' has been called prior to this function. If bWaitForWorldTick is true then the call will not return until the world has been ticked required number of times"));

	Server.bind("close_session", [this]() { U4MLManager::Get().SetSession(nullptr); });
	Librarian.AddRPCFunctionDescription(TEXT("close_session"), TEXT("(), shuts down the current session (along with all the agents)."));
#endif // WITH_RPCLIB

	if (Session)
	{
		Session->ConfigureAsServer();
	}
	OnAddServerFunctions.Broadcast(Server);
}