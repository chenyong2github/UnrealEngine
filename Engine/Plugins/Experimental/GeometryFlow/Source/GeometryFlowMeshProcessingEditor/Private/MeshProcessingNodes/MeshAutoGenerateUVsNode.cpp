// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshProcessingNodes/MeshAutoGenerateUVsNode.h"

#include "ParameterizationOps/ParameterizeMeshOp.h"


using namespace UE::GeometryFlow;


void FMeshAutoGenerateUVsNode::GenerateUVs(const FDynamicMesh3& MeshIn, const FMeshAutoGenerateUVsSettings& Settings, FDynamicMesh3& MeshOut)
{
	// this is horrible - have to copy input mesh so that we can pass a TSharedPtr
	TSharedPtr<FDynamicMesh3> InputMesh = MakeShared<FDynamicMesh3>(MeshIn);

	FParameterizeMeshOp ParameterizeMeshOp;
	ParameterizeMeshOp.Stretch = Settings.Stretch;
	ParameterizeMeshOp.NumCharts = Settings.NumCharts;
	ParameterizeMeshOp.InputMesh = InputMesh;

	ParameterizeMeshOp.IslandMode = EParamOpIslandMode::Auto;
	ParameterizeMeshOp.UnwrapType = EParamOpUnwrapType::MinStretch;

	FProgressCancel Progress;
	ParameterizeMeshOp.CalculateResult(&Progress);
	TUniquePtr<FDynamicMesh3> ResultMesh = ParameterizeMeshOp.ExtractResult();

	MeshOut = MoveTemp(*ResultMesh);
}