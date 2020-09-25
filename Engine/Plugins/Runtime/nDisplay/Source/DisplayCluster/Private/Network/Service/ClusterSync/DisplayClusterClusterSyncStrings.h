// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Cluster synchronization messages
 */
namespace DisplayClusterClusterSyncStrings
{
	constexpr static auto ProtocolName = "ClusterSync";
	
	constexpr static auto TypeRequest  = "request";
	constexpr static auto TypeResponse = "response";

	constexpr static auto ArgumentsDefaultCategory = "CS";
	constexpr static auto ArgumentsJsonEvents      = "CS_JE";
	constexpr static auto ArgumentsBinaryEvents    = "CS_BE";

	namespace WaitForGameStart
	{
		constexpr static auto Name = "WaitForGameStart";
		constexpr static auto ArgThreadTime  = "ThreadTime";
		constexpr static auto ArgBarrierTime = "BarrierTime";
	}

	namespace WaitForFrameStart
	{
		constexpr static auto Name = "WaitForFrameStart";
		constexpr static auto ArgThreadTime  = "ThreadTime";
		constexpr static auto ArgBarrierTime = "BarrierTime";
	}

	namespace WaitForFrameEnd
	{
		constexpr static auto Name = "WaitForFrameEnd";
		constexpr static auto ArgThreadTime  = "ThreadTime";
		constexpr static auto ArgBarrierTime = "BarrierTime";
	}

	namespace GetDeltaTime
	{
		constexpr static auto Name = "GetDeltaTime";
		constexpr static auto ArgDeltaSeconds = "DeltaSeconds";
	}

	namespace GetFrameTime
	{
		constexpr static auto Name = "GetFrameTime";
		constexpr static auto ArgIsValid    = "IsValid";
		constexpr static auto ArgFrameTime  = "FrameTime";
	}

	namespace GetSyncData
	{
		constexpr static auto Name = "GetSyncData";
		constexpr static auto ArgSyncGroup = "SyncGroup";
	}

	namespace GetInputData
	{
		constexpr static auto Name = "GetInputData";
	}

	namespace GetEventsData
	{
		constexpr static auto Name = "GetEventsData";
	}

	namespace GetNativeInputData
	{
		constexpr static auto Name = "GetNativeInputData";
	}
};
