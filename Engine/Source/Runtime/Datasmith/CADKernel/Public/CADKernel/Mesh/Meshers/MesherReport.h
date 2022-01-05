// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Chrono.h"
#include "CADKernel/Core/Types.h"
#include "CADKernel/UI/Message.h"


namespace CADKernel
{

struct CADKERNEL_API FMesherLog
{
	int32 CountOfIntersectingLoopCorrectionFailures = 0;
	int32 CountOfSelfIntersectingCycles = 0;
	int32 CountOfDegeneratedGrids = 0;
	int32 CountOfDegeneratedLoops = 0;

	FMesherLog()
	{}

	void PrintBilan() const
	{
	}
};

struct CADKERNEL_API FMesherChronos
{
	FDuration GlobalDuration;
	FDuration ApplyCriteriaDuration;
	FDuration IsolateQuadPatchDuration;
	FDuration GlobalMeshDuration;
	FDuration GlobalPointCloudDuration;
	FDuration GlobalGeneratePointCloudDuration;
	FDuration GlobalTriangulateDuration;
	FDuration GlobalDelaunayDuration;
	FDuration GlobalMeshAndGetLoopNodes;
	FDuration GlobalMeshEdges;
	FDuration GlobalThinZones;

	FDuration GlobalFindThinZones;
	//FDuration GlobalScaleGrid;
	FDuration GlobalMeshThinZones;

	FMesherChronos()
		: GlobalDuration(FChrono::Init())
		, ApplyCriteriaDuration(FChrono::Init())
		, IsolateQuadPatchDuration(FChrono::Init())
		, GlobalMeshDuration(FChrono::Init())
		, GlobalPointCloudDuration(FChrono::Init())
		, GlobalGeneratePointCloudDuration(FChrono::Init())
		, GlobalTriangulateDuration(FChrono::Init())
		, GlobalDelaunayDuration(FChrono::Init())
		, GlobalMeshAndGetLoopNodes(FChrono::Init())
		, GlobalMeshEdges(FChrono::Init())
		, GlobalThinZones(FChrono::Init())
		, GlobalFindThinZones(FChrono::Init())
		//, GlobalScaleGrid(FChrono::Init())
		, GlobalMeshThinZones(FChrono::Init())
	{}

	void PrintTimeElapse() const
	{
		FMessage::Printf(Log, TEXT("\n\n\n"));
		FChrono::PrintClockElapse(Log, TEXT(""), TEXT("Total"), GlobalDuration);
		FChrono::PrintClockElapse(Log, TEXT("  |  "), TEXT("Apply Criteria"), ApplyCriteriaDuration);
		FChrono::PrintClockElapse(Log, TEXT("  |  "), TEXT("Find Quad Surfaces"), IsolateQuadPatchDuration);
		FChrono::PrintClockElapse(Log, TEXT("  |  "), TEXT("Mesh Time"), GlobalMeshDuration);
		FChrono::PrintClockElapse(Log, TEXT("  |   |  "), TEXT("GeneratePoint Cloud "), GlobalGeneratePointCloudDuration);
		FChrono::PrintClockElapse(Log, TEXT("  |   |  |  "), TEXT("Point Cloud "), GlobalPointCloudDuration);
		FChrono::PrintClockElapse(Log, TEXT("  |   |  "), TEXT("ThinZones "), GlobalThinZones);
		FChrono::PrintClockElapse(Log, TEXT("  |   |  "), TEXT("Mesh ThinZones "), GlobalMeshThinZones);
		FChrono::PrintClockElapse(Log, TEXT("  |   |  "), TEXT("MeshEdges "), GlobalMeshEdges);
		FChrono::PrintClockElapse(Log, TEXT("  |   |  "), TEXT("TriangulateDuration "), GlobalTriangulateDuration);
		FChrono::PrintClockElapse(Log, TEXT("  |   |   |  "), TEXT("Delaunay Duration "), GlobalDelaunayDuration);
	}
};

struct CADKERNEL_API FMesherReport
{
	FMesherLog Logs;
	FMesherChronos Chronos;
};

}
