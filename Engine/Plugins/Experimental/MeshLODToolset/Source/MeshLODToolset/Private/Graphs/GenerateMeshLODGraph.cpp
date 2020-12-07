// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graphs/GenerateMeshLODGraph.h"


#include "GeometryFlowGraph.h"
#include "GeometryFlowGraphUtil.h"
#include "BaseNodes/TransferNode.h"

#include "MeshProcessingNodes/MeshSolidifyNode.h"
#include "MeshProcessingNodes/MeshVoxMorphologyNode.h"
#include "MeshProcessingNodes/MeshSimplifyNode.h"
#include "MeshProcessingNodes/MeshDeleteTrianglesNode.h"

#include "MeshProcessingNodes/MeshNormalsNodes.h"
#include "MeshProcessingNodes/MeshTangentsNodes.h"

#include "MeshProcessingNodes/MeshAutoGenerateUVsNode.h"
#include "MeshProcessingNodes/MeshRecalculateUVsNode.h"
#include "MeshProcessingNodes/MeshRepackUVsNode.h"

#include "DataTypes/MeshImageBakingData.h"
#include "MeshBakingNodes/BakeMeshNormalMapNode.h"
#include "MeshBakingNodes/BakeMeshTextureImageNode.h"

#include "MeshDecompositionNodes/MakeTriangleSetsNode.h"
#include "PhysicsNodes/GenerateConvexHullsCollisionNode.h"
#include "GeometryFlowExecutor.h"

using namespace UE::GeometryFlow;



void FGenerateMeshLODGraph::SetSourceMesh(const FDynamicMesh3& SourceMeshIn)
{
	UpdateSourceNodeValue<FDynamicMeshSourceNode>(*Graph, MeshSourceNode, SourceMeshIn);
}

void FGenerateMeshLODGraph::EvaluateResultParallel(
	FDynamicMesh3& ResultMesh,
	FMeshTangentsd& ResultTangents,
	FSimpleShapeSet3d& ResultCollision,
	UE::GeometryFlow::FNormalMapImage& NormalMap,
	TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>>& TextureImages)
{
	//FScopedDurationTimeLogger Timer(TEXT("FGenerateMeshLODGraph::EvaluateResult -- parallel execution"));

	TArray<FGeometryFlowExecutor::NodeOutputSpec> DesiredOutputs;

	DesiredOutputs.Add(FGeometryFlowExecutor::NodeOutputSpec{ BakeNormalMapNode, FBakeMeshNormalMapNode::OutParamNormalMap() });

	for (FBakeTextureGraphInfo& TexBakeStep : BakeTextureNodes)
	{
		DesiredOutputs.Add(FGeometryFlowExecutor::NodeOutputSpec{
			TexBakeStep.BakeNode,
			FBakeMeshTextureImageNode::OutParamTextureImage() });
	}

	DesiredOutputs.Add(FGeometryFlowExecutor::NodeOutputSpec{
			CollisionOutputNode,
			FCollisionGeometryTransferNode::OutParamValue() });

	DesiredOutputs.Add(FGeometryFlowExecutor::NodeOutputSpec{
		TangentsOutputNode,
		FMeshTangentsTransferNode::OutParamValue() });

	DesiredOutputs.Add(FGeometryFlowExecutor::NodeOutputSpec{
		MeshOutputNode,
		FDynamicMeshTransferNode::OutParamValue() });

	FGeometryFlowExecutor Exec(*Graph);

	TArray<TSafeSharedPtr<IData>> OutputDatas;
	Exec.ComputeOutputs(DesiredOutputs, OutputDatas);

	check(OutputDatas.Num() == DesiredOutputs.Num());

	TArray<TSafeSharedPtr<IData>>::TIterator OutputDataIter = OutputDatas.CreateIterator();

	// { BakeNormalMapNode, FBakeMeshNormalMapNode::OutParamNormalMap() }
	NormalMap = FNormalMapImage();
	bool bTakeNormalMap = false;
	EGeometryFlowResult ExtractResult = ExtractData(*OutputDataIter, NormalMap, (int)EMeshProcessingDataTypes::NormalMapImage, bTakeNormalMap);
	check(ExtractResult == EGeometryFlowResult::Ok);
	++OutputDataIter;

	for (FBakeTextureGraphInfo& TexBakeStep : BakeTextureNodes)
	{
		// { TexBakeStep.BakeNode, FBakeMeshTextureImageNode::OutParamTextureImage() }
		TUniquePtr<UE::GeometryFlow::FTextureImage> NewImage = MakeUnique<UE::GeometryFlow::FTextureImage>();
		bool bTakeTexBake = false;
		ExtractResult = ExtractData(*OutputDataIter, *NewImage, (int)EMeshProcessingDataTypes::TextureImage, bTakeTexBake);
		check(ExtractResult == EGeometryFlowResult::Ok);
		++OutputDataIter;
		TextureImages.Add(MoveTemp(NewImage));
	}

	// {CollisionOutputNode, FCollisionGeometryTransferNode::OutParamValue() }
	ResultCollision = FSimpleShapeSet3d();
	bool bTakeResultCollision = false;
	ExtractResult = ExtractData(*OutputDataIter, ResultCollision, (int)FCollisionGeometry::DataTypeIdentifier, bTakeResultCollision);
	check(ExtractResult == EGeometryFlowResult::Ok);
	++OutputDataIter;

	// { TangentsOutputNode, FMeshTangentsTransferNode::OutParamValue() }
	bool bTakeResultTangents = false;
	ResultTangents = FMeshTangentsd();
	ExtractResult = ExtractData(*OutputDataIter, ResultTangents, (int)EMeshProcessingDataTypes::MeshTangentSet, bTakeResultTangents);
	check(ExtractResult == EGeometryFlowResult::Ok);
	++OutputDataIter;

	// {MeshOutputNode, FDynamicMeshTransferNode::OutParamValue() }
	bool bTakeResultMesh = false;
	ResultMesh.Clear();
	ExtractResult = ExtractData(*OutputDataIter, ResultMesh, (int32)EMeshProcessingDataTypes::DynamicMesh, bTakeResultMesh);
	check(ExtractResult == EGeometryFlowResult::Ok);
	++OutputDataIter;
}


void FGenerateMeshLODGraph::UpdateSolidifySettings(const FMeshSolidifySettings& SolidifySettings)
{
	UpdateSettingsSourceNodeValue(*Graph, SolidifySettingsNode, SolidifySettings);
	CurrentSolidifySettings = SolidifySettings;
}

void FGenerateMeshLODGraph::UpdateMorphologySettings(const FVoxClosureSettings& MorphologySettings)
{
	UpdateSettingsSourceNodeValue(*Graph, MorphologySettingsNode, MorphologySettings);
	CurrentMorphologySettings = MorphologySettings;
}

void FGenerateMeshLODGraph::UpdateSimplifySettings(const FMeshSimplifySettings& SimplifySettings)
{
	UpdateSettingsSourceNodeValue(*Graph, SimplifySettingsNode, SimplifySettings);
	CurrentSimplifySettings = SimplifySettings;
}

void FGenerateMeshLODGraph::UpdateAutoUVSettings(const UE::GeometryFlow::FMeshAutoGenerateUVsSettings& AutoUVSettings)
{
	UpdateSettingsSourceNodeValue(*Graph, AutoUVSettingsNode, AutoUVSettings);
	CurrentAutoUVSettings = AutoUVSettings;
}

void FGenerateMeshLODGraph::UpdateBakeCacheSettings(const UE::GeometryFlow::FMeshMakeBakingCacheSettings& BakeCacheSettings)
{
	UpdateSettingsSourceNodeValue(*Graph, BakeCacheSettingsNode, BakeCacheSettings);
	CurrentBakeCacheSettings = BakeCacheSettings;
}


void FGenerateMeshLODGraph::UpdateGenerateConvexCollisionSettings(const FGenerateConvexHullsCollisionSettings& GenConvexesSettings)
{
	UpdateSettingsSourceNodeValue(*Graph, GenerateConvexesSettingsNode, GenConvexesSettings);
	CurrentGenerateConvexHullsSettings = GenConvexesSettings;
}


void FGenerateMeshLODGraph::EvaluateResult(
	FDynamicMesh3& ResultMesh,
	FMeshTangentsd& ResultTangents,
	FSimpleShapeSet3d& ResultCollision,
	UE::GeometryFlow::FNormalMapImage& NormalMap,
	TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>>& TextureImages)
{
	//FScopedDurationTimeLogger Timer(TEXT("FGenerateMeshLODGraph::EvaluateResult -- serial execution"));

	//
	// evaluate normal map
	//

	NormalMap = FNormalMapImage();
	TUniquePtr<FEvaluationInfo> NormalMapEvalInfo = MakeUnique<FEvaluationInfo>();
	EGeometryFlowResult NormalMapEvalResult = Graph->EvaluateResult(BakeNormalMapNode, FBakeMeshNormalMapNode::OutParamNormalMap(),
		NormalMap, (int)EMeshProcessingDataTypes::NormalMapImage, NormalMapEvalInfo, true);
	ensure(NormalMapEvalResult == EGeometryFlowResult::Ok);

	UE_LOG(LogTemp, Warning, TEXT("NormalMapPass - Evaluated %d Nodes, Recomputed %d"), NormalMapEvalInfo->NumEvaluations(), NormalMapEvalInfo->NumComputes());

	//
	// evaluate transferred textures
	//

	for (FBakeTextureGraphInfo& TexBakeStep : BakeTextureNodes)
	{
		TUniquePtr<UE::GeometryFlow::FTextureImage> NewImage = MakeUnique<UE::GeometryFlow::FTextureImage>();
		TUniquePtr<FEvaluationInfo> TexBakeEvalInfo = MakeUnique<FEvaluationInfo>();
		EGeometryFlowResult TexBakeEvalResult = Graph->EvaluateResult(TexBakeStep.BakeNode, FBakeMeshTextureImageNode::OutParamTextureImage(),
			*NewImage, (int)EMeshProcessingDataTypes::TextureImage, TexBakeEvalInfo, true);
		TextureImages.Add(MoveTemp(NewImage));
		ensure(TexBakeEvalResult == EGeometryFlowResult::Ok);

		UE_LOG(LogTemp, Warning, TEXT("TextureBakePass %s - Evaluated %d Nodes, Recomputed %d"), *TexBakeStep.Identifier, TexBakeEvalInfo->NumEvaluations(), TexBakeEvalInfo->NumComputes());
	}

	//
	// evaluate collision
	//

	bool bTakeResultCollision = false;
	ResultCollision = FSimpleShapeSet3d();

	TUniquePtr<FEvaluationInfo> CollisionEvalInfo = MakeUnique<FEvaluationInfo>();
	EGeometryFlowResult CollisionEvalResult = Graph->EvaluateResult(CollisionOutputNode, FCollisionGeometryTransferNode::OutParamValue(),
		ResultCollision, FCollisionGeometry::DataTypeIdentifier, CollisionEvalInfo, bTakeResultCollision);
	ensure(CollisionEvalResult == EGeometryFlowResult::Ok);

	UE_LOG(LogTemp, Warning, TEXT("OutputCollisionPass - Evaluated %d Nodes, Recomputed %d"), CollisionEvalInfo->NumEvaluations(), CollisionEvalInfo->NumComputes());

	// 
	// evaluate tangents
	//

	bool bTakeResultTangents = false;
	ResultTangents = FMeshTangentsd();

	TUniquePtr<FEvaluationInfo> TangentsEvalInfo = MakeUnique<FEvaluationInfo>();
	EGeometryFlowResult TangentsEvalResult = Graph->EvaluateResult(TangentsOutputNode, FMeshTangentsTransferNode::OutParamValue(),
		ResultTangents, (int)EMeshProcessingDataTypes::MeshTangentSet, TangentsEvalInfo, bTakeResultTangents);
	ensure(TangentsEvalResult == EGeometryFlowResult::Ok);

	UE_LOG(LogTemp, Warning, TEXT("OutputTangentsPass - Evaluated %d Nodes, Recomputed %d"), TangentsEvalInfo->NumEvaluations(), TangentsEvalInfo->NumComputes());


	//
	// evaluate result mesh
	// 


	bool bTakeResultMesh = true;
	ResultMesh.Clear();

	TUniquePtr<FEvaluationInfo> MeshEvalInfo = MakeUnique<FEvaluationInfo>();
	EGeometryFlowResult EvalResult = Graph->EvaluateResult(MeshOutputNode, FDynamicMeshTransferNode::OutParamValue(),
		ResultMesh, (int32)EMeshProcessingDataTypes::DynamicMesh, MeshEvalInfo, bTakeResultMesh);
	ensure(EvalResult == EGeometryFlowResult::Ok);

	UE_LOG(LogTemp, Warning, TEXT("OutputMeshPass - Evaluated %d Nodes, Recomputed %d"), MeshEvalInfo->NumEvaluations(), MeshEvalInfo->NumComputes());
}





void FGenerateMeshLODGraph::BuildGraph()
{
	Graph = MakeUnique<FGraph>();

	MeshSourceNode = Graph->AddNodeOfType<FDynamicMeshSourceNode>(TEXT("SourceMesh"));

	// generating low-poly mesh

	SolidifyNode = Graph->AddNodeOfType<FSolidifyMeshNode>(TEXT("Solidify"));
	Graph->InferConnection(MeshSourceNode, SolidifyNode);
	SolidifySettingsNode = Graph->AddNodeOfType<FSolidifySettingsSourceNode>(TEXT("SolidifySettings"));
	Graph->InferConnection(SolidifySettingsNode, SolidifyNode);

	MorphologyNode = Graph->AddNodeOfType<FVoxClosureMeshNode>(TEXT("Closure"));
	Graph->InferConnection(SolidifyNode, MorphologyNode);
	MorphologySettingsNode = Graph->AddNodeOfType<FVoxClosureSettingsSourceNode>(TEXT("ClosureSettings"));
	Graph->InferConnection(MorphologySettingsNode, MorphologyNode);

	SimplifyNode = Graph->AddNodeOfType<FSimplifyMeshNode>(TEXT("Simplify"));
	Graph->InferConnection(MorphologyNode, SimplifyNode);
	SimplifySettingsNode = Graph->AddNodeOfType<FSimplifySettingsSourceNode>(TEXT("SimplifySettings"));
	Graph->InferConnection(SimplifySettingsNode, SimplifyNode);

	NormalsNode = Graph->AddNodeOfType<FComputeMeshNormalsNode>(TEXT("Normals"));
	Graph->InferConnection(SimplifyNode, NormalsNode);
	NormalsSettingsNode = Graph->AddNodeOfType<FNormalsSettingsSourceNode>(TEXT("NormalsSettings"));
	Graph->InferConnection(NormalsSettingsNode, NormalsNode);

	// computing UVs

	AutoUVNode = Graph->AddNodeOfType<FMeshAutoGenerateUVsNode>(TEXT("AutoUV"));
	Graph->InferConnection(NormalsNode, AutoUVNode);
	AutoUVSettingsNode = Graph->AddNodeOfType<FMeshAutoGenerateUVsSettingsSourceNode>(TEXT("AutoUVSettings"));
	Graph->InferConnection(AutoUVSettingsNode, AutoUVNode);

	RecomputeUVNode = Graph->AddNodeOfType<FMeshRecalculateUVsNode>(TEXT("RecalcUV"));
	Graph->InferConnection(AutoUVNode, RecomputeUVNode);
	RecomputeUVSettingsNode = Graph->AddNodeOfType<FMeshRecalculateUVsSettingsSourceNode>(TEXT("RecalcUVSettings"));
	Graph->InferConnection(RecomputeUVSettingsNode, RecomputeUVNode);

	RepackUVNode = Graph->AddNodeOfType<FMeshRepackUVsNode>(TEXT("RepackUV"));
	Graph->InferConnection(RecomputeUVNode, RepackUVNode);
	RepackUVSettingsNode = Graph->AddNodeOfType<FMeshRepackUVsSettingsSourceNode>(TEXT("RepackUVSettings"));
	Graph->InferConnection(RepackUVSettingsNode, RepackUVNode);


	// final mesh output

	MeshOutputNode = Graph->AddNodeOfType<FDynamicMeshTransferNode>(TEXT("OutputMesh"));
	Graph->InferConnection(RepackUVNode, MeshOutputNode);


	// create tangents

	TangentsNode = Graph->AddNodeOfType<FComputeMeshTangentsNode>(TEXT("Tangents"));
	Graph->InferConnection(RepackUVNode, TangentsNode);
	TangentsSettingsNode = Graph->AddNodeOfType<FTangentsSettingsSourceNode>(TEXT("TangentsSettings"));
	Graph->InferConnection(TangentsSettingsNode, TangentsNode);

	// tangents output
	TangentsOutputNode = Graph->AddNodeOfType<FMeshTangentsTransferNode>(TEXT("OutputTangents"));
	Graph->InferConnection(TangentsNode, TangentsOutputNode);

	// create bake cache

	BakeCacheNode = Graph->AddNodeOfType<FMakeMeshBakingCacheNode>(TEXT("MakeBakeCache"));
	Graph->AddConnection(MeshSourceNode, FDynamicMeshSourceNode::OutParamValue(), BakeCacheNode, FMakeMeshBakingCacheNode::InParamDetailMesh());
	Graph->AddConnection(RepackUVNode, FMeshRepackUVsNode::OutParamResultMesh(), BakeCacheNode, FMakeMeshBakingCacheNode::InParamTargetMesh());
	BakeCacheSettingsNode = Graph->AddNodeOfType<FMeshMakeBakingCacheSettingsSourceNode>(TEXT("BakeCacheSettings"));
	Graph->InferConnection(BakeCacheSettingsNode, BakeCacheNode);

	// normal map baker

	BakeNormalMapNode = Graph->AddNodeOfType<FBakeMeshNormalMapNode>(TEXT("BakeNormalMap"));
	Graph->InferConnection(BakeCacheNode, BakeNormalMapNode);
	Graph->InferConnection(TangentsNode, BakeNormalMapNode);
	BakeNormalMapSettingsNode = Graph->AddNodeOfType<FBakeMeshNormalMapSettingsSourceNode>(TEXT("BakeNormalMapSettings"));
	Graph->InferConnection(BakeNormalMapSettingsNode, BakeNormalMapNode);


	Graph->AddNodeOfType<FMeshDeleteTrianglesNode>(TEXT("TestDeleteTrisNode"));

	// collision generation

	FGraph::FHandle IgnoreGroupsForCollisionNode = Graph->AddNodeOfType<FIndexSetsSourceNode>(TEXT("CollisionIgnoreGroups"));

	//DecomposeMeshForCollisionNode = Graph->AddNodeOfType<FMakeTriangleSetsFromMeshNode>(TEXT("Decompose"));
	DecomposeMeshForCollisionNode = Graph->AddNodeOfType<FMakeTriangleSetsFromGroupsNode>(TEXT("Decompose"));
	Graph->InferConnection(MeshSourceNode, DecomposeMeshForCollisionNode);
	Graph->InferConnection(IgnoreGroupsForCollisionNode, DecomposeMeshForCollisionNode);

	GenerateConvexesNode = Graph->AddNodeOfType<FGenerateConvexHullsCollisionNode>(TEXT("GenerateConvexes"));
	Graph->InferConnection(MeshSourceNode, GenerateConvexesNode);
	Graph->InferConnection(DecomposeMeshForCollisionNode, GenerateConvexesNode);
	GenerateConvexesSettingsNode = Graph->AddNodeOfType<FGenerateConvexHullsCollisionSettingsSourceNode>(TEXT("GenerateConvexesSettings"));
	Graph->InferConnection(GenerateConvexesSettingsNode, GenerateConvexesNode);

	// final collision output

	CollisionOutputNode = Graph->AddNodeOfType<FCollisionGeometryTransferNode>(TEXT("OutputCollision"));
	Graph->InferConnection(GenerateConvexesNode, CollisionOutputNode);


	//
	// parameters
	//


	FMeshSolidifySettings SolidifySettings;
	UpdateSolidifySettings(SolidifySettings);

	FVoxClosureSettings MorphologySettings;
	MorphologySettings.Distance = 5.0;
	UpdateMorphologySettings(MorphologySettings);

	FMeshSimplifySettings SimplifySettings;
	SimplifySettings.bDiscardAttributes = true;
	SimplifySettings.SimplifyType = EMeshSimplifyType::VolumePreserving;
	SimplifySettings.TargetType = EMeshSimplifyTargetType::TriangleCount;
	SimplifySettings.TargetCount = 500;
	UpdateSimplifySettings(SimplifySettings);

	FMeshNormalsSettings NormalsSettings;
	NormalsSettings.NormalsType = EComputeNormalsType::FromFaceAngleThreshold;
	NormalsSettings.AngleThresholdDeg = 45.0;
	UpdateSettingsSourceNodeValue(*Graph, NormalsSettingsNode, NormalsSettings);

	FMeshAutoGenerateUVsSettings AutoUVSettings;
	AutoUVSettings.NumCharts = 20;
	AutoUVSettings.Stretch = 0.1;
	UpdateAutoUVSettings(AutoUVSettings);

	FMeshRecalculateUVsSettings RecomputeUVSettings;
	UpdateSettingsSourceNodeValue(*Graph, RecomputeUVSettingsNode, RecomputeUVSettings);

	FMeshRepackUVsSettings RepackUVSettings;
	UpdateSettingsSourceNodeValue(*Graph, RepackUVSettingsNode, RepackUVSettings);


	FMeshTangentsSettings TangentsSettings;
	UpdateSettingsSourceNodeValue(*Graph, TangentsSettingsNode, TangentsSettings);


	FMeshMakeBakingCacheSettings BakeCacheSettings;
	BakeCacheSettings.Dimensions = FImageDimensions(512, 512);
	BakeCacheSettings.Thickness = 5.0;
	UpdateBakeCacheSettings(BakeCacheSettings);


	FBakeMeshNormalMapSettings NormalMapSettings;
	UpdateSettingsSourceNodeValue(*Graph, BakeNormalMapSettingsNode, NormalMapSettings);

	FIndexSets IgnoreGroupsForCollision;
	IgnoreGroupsForCollision.AppendSet({ 0 });
	UpdateSettingsSourceNodeValue(*Graph, IgnoreGroupsForCollisionNode, IgnoreGroupsForCollision);

	FGenerateConvexHullsCollisionSettings GenConvexesSettings;
	UpdateGenerateConvexCollisionSettings(GenConvexesSettings);
}




int32 FGenerateMeshLODGraph::AppendTextureBakeNode(const TImageBuilder<FVector4f>& SourceImage, const FString& Identifier)
{
	FBakeTextureGraphInfo NewNode;
	NewNode.Index = BakeTextureNodes.Num();
	NewNode.Identifier = Identifier;

	// add source node
	NewNode.TexSourceNode = Graph->AddNodeOfType<FTextureImageSourceNode>(FString::Printf(TEXT("TextureSource%d_%s"), NewNode.Index, *NewNode.Identifier));

	// normal map baker
	NewNode.BakeNode = Graph->AddNodeOfType<FBakeMeshTextureImageNode>(FString::Printf(TEXT("BakeTexImage%d_%s"), NewNode.Index, *NewNode.Identifier));
	ensure(Graph->InferConnection(BakeCacheNode, NewNode.BakeNode) == EGeometryFlowResult::Ok);
	ensure(Graph->InferConnection(NewNode.TexSourceNode, NewNode.BakeNode) == EGeometryFlowResult::Ok);
	FGraph::FHandle BakeTextureImageSettingsNode = Graph->AddNodeOfType<FBakeMeshTextureImageSettingsSourceNode>(TEXT("BakeTextureImageSettings"));
	ensure(Graph->InferConnection(BakeTextureImageSettingsNode, NewNode.BakeNode) == EGeometryFlowResult::Ok);

	FTextureImage InputTexImage;
	InputTexImage.Image = SourceImage;
	UpdateSourceNodeValue<FTextureImageSourceNode>(*Graph, NewNode.TexSourceNode, InputTexImage);

	BakeTextureNodes.Add(NewNode);

	return NewNode.Index;
}
