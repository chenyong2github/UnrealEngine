// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Mesh/Structure/ModelMeshReport.h"

#include "CADKernel/Mesh/Structure/ModelMesh.h"
#include "CADKernel/Mesh/Structure/ModelMeshAnalyzer.h"

namespace CADKernel
{

void FModelMeshReport::Run()
{
	for (const FModelMesh* ModelMesh : ModelMeshes)
	{
		bool MeshGoodOrientation = true;
		int32 MeshBorderEdgeCount = 0;
		int32 MeshNonManifoldEdgeCount = 0;
		int32 MeshCycleCount = 0;
		int32 MeshChainCount = 0;
		double MeshMaxAngle = 0;

		FModelMeshAnalyzer ModelMeshAnalyzer(*ModelMesh);
		ModelMeshAnalyzer.BuildMesh();

		if (!ModelMeshAnalyzer.CheckOrientation())
		{
			GoodOrientation = false;
		}

		ModelMeshAnalyzer.ComputeBorderCount(MeshBorderEdgeCount, MeshNonManifoldEdgeCount);
		ModelMeshAnalyzer.ComputeMeshGapCount(MeshCycleCount, MeshChainCount);
		BorderEdgeCount += MeshBorderEdgeCount;
		NonManifoldEdgeCount += MeshNonManifoldEdgeCount;
		CycleCount += MeshCycleCount;
		ChainCount += MeshChainCount;

		MeshMaxAngle = ModelMeshAnalyzer.ComputeMaxAngle();
		if (MaxAngle < MeshMaxAngle)
		{
			MaxAngle = MeshMaxAngle;
		}
	}
}

void FModelMeshReport::Print()
{
	FMessage::FillReportFile(TEXT(""), GoodOrientation ? TEXT("True") : TEXT("False"));
	FMessage::FillReportFile(TEXT(""), BorderEdgeCount);
	FMessage::FillReportFile(TEXT(""), NonManifoldEdgeCount);
	FMessage::FillReportFile(TEXT(""), CycleCount);
	FMessage::FillReportFile(TEXT(""), ChainCount);
	FMessage::FillReportFile(TEXT(""), TEXT(""));
}

} // namespace CADKernel

