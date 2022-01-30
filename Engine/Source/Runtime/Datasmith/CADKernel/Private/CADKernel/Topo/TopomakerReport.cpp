// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Topo/TopomakerReport.h"

#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalEdge.h"

#include "CADKernel/UI/Message.h"

namespace CADKernel
{

void FTopomakerReport::Print()
{
	FMessage::FillReportFile(TEXT(""), TEXT(""));
	FMessage::FillReportFile(TEXT("Sew"), SewDuration);
	FMessage::FillReportFile(TEXT("Orient"), OrientationDuration);
	FMessage::FillReportFile(TEXT(""), TEXT(""));
	FMessage::FillReportFile(TEXT("ShellOrient"), OrientationFixedCount);
	FMessage::FillReportFile(TEXT("FaceOrient"), SwappedFaceCount);
}

}
