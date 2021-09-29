// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityUtils.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"

namespace UE { namespace Pipe { namespace Utils
{

EProcessorExecutionFlags GetProcessorExecutionFlagsForWold(const UWorld& World)
{
	EProcessorExecutionFlags ExecutionFlags = EProcessorExecutionFlags::None;
	const ENetMode NetMode = World.GetNetMode();
	switch (NetMode)
	{
		case NM_ListenServer:
			ExecutionFlags = EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Server;
			break;
		case NM_DedicatedServer:
			ExecutionFlags = EProcessorExecutionFlags::Server;
			break;
		case NM_Client:
			ExecutionFlags = EProcessorExecutionFlags::Client;
			break;
		default:
			check(NetMode == NM_Standalone);
			ExecutionFlags = EProcessorExecutionFlags::Standalone;
			break;
	}

	return ExecutionFlags;
}

} } }// namespace UE::Pipe::Utils