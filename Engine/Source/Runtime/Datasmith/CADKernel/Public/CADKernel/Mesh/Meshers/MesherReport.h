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
		FMessage::FillReportFile(TEXT("SelfInterFail"), CountOfRemoveLoopSelfIntersectionFailure);
		FMessage::FillReportFile(TEXT("CrossingFail"), CountOfCrossingLoopsFailure);
		FMessage::FillReportFile(TEXT(""), TEXT(""));
		FMessage::FillReportFile(TEXT("MeshingFail"), CountOfMeshingFailure);
		FMessage::FillReportFile(TEXT("DegenGrid"), CountOfDegeneratedGrid);
		FMessage::FillReportFile(TEXT("DegenLoop"), CountOfDegeneratedLoop);
		FMessage::FillReportFile(TEXT("CycleFailure"), CountOfCycleMeshingFailure);
		FMessage::FillReportFile(TEXT(""), TEXT(""));
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
		FMessage::FillReportFile(TEXT("Criteria"), ApplyCriteriaDuration);
		FMessage::FillReportFile(TEXT("Find Quad"), IsolateQuadPatchDuration);
		FMessage::FillReportFile(TEXT("GenPoint"), GlobalGeneratePointCloudDuration);
		FMessage::FillReportFile(TEXT("FindThin"), GlobalThinZones);
		FMessage::FillReportFile(TEXT("MeshThin"), GlobalMeshThinZones);
		FMessage::FillReportFile(TEXT("MeshEdges"), GlobalMeshEdges);
		FMessage::FillReportFile(TEXT("Triangul"),  GlobalTriangulateDuration);
		FMessage::FillReportFile(TEXT("MeshTotal"), GlobalDuration);
		FMessage::FillReportFile(TEXT(""), TEXT(""));

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
