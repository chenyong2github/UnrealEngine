// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Chrono.h"
#include "CADKernel/Core/Types.h"
#include "CADKernel/UI/Message.h"


namespace CADKernel
{

class CADKERNEL_API FMesherLog
{
private:

	int32 CountOfRemoveLoopSelfIntersectionFailure = 0;
	int32 CountOfCrossingLoopsFailure = 0;

	int32 CountOfDegeneratedGrid = 0;
	int32 CountOfDegeneratedLoop = 0;
	int32 CountOfMeshingFailure = 0;
	int32 CountOfCycleMeshingFailure = 0;

public:
	FMesherLog()
	{}

	void PrintReport() const
	{
	}

	void AddRemoveCrossingLoopsFailure()
	{
		CountOfCrossingLoopsFailure++;
	}

	void AddRemoveSelfIntersectionFailure()
	{
		CountOfRemoveLoopSelfIntersectionFailure++;
	}

	void AddCycleMeshingFailure()
	{
		CountOfCycleMeshingFailure++;
	}

	void AddDegeneratedLoop()
	{
		CountOfDegeneratedLoop++;
		CountOfMeshingFailure++;
	}

	void AddDegeneratedGrid()
	{
		CountOfDegeneratedGrid++;
		CountOfMeshingFailure++;
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

	void PrintReport() const
	{
	}
};

struct CADKERNEL_API FMesherReport
{
	FMesherLog Logs;
	FMesherChronos Chronos;

	void Print()
	{
		Logs.PrintReport();
		Chronos.PrintReport();
	}
};

}
