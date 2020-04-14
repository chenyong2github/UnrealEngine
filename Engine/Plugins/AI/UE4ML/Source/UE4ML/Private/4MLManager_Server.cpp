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


void U4MLManager::ConfigureAsServer()
{
	UE_LOG(LogUE4ML, Log, TEXT("\tconfiguring as server"));

	AddCommonFunctions();

#if WITH_RPCLIB

	AddClientFunctionBind(UE4_RPC_BIND("enable_manual_world_tick", [this](bool bEnable) {
		SetManualWorldTickEnabled(bEnable);
	})
		, TEXT("(), Controlls whether the world is running real time or it\'s being ticked manually with calls to \'step\' or \'request_world_tick\' functions. Default is \'real time\'."));

	AddClientFunctionBind(UE4_RPC_BIND("request_world_tick", [this](int32 TickCount, bool bWaitForWorldTick) {
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
	})
		, TEXT("(bool bWaitForWorldTick), Requests a single world tick. This has meaning only if \'enable_manual_world_tick(true)\' has been called prior to this function. If bWaitForWorldTick is true then the call will not return until the world has been ticked"));

#endif // WITH_RPCLIB

	if (Session)
	{
		Session->ConfigureAsServer();
	}
	OnAddServerFunctions.Broadcast();
}