// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graphs/GenerateMeshLODGraph.h"


#include "GeometryFlowGraph.h"
#include "GeometryFlowGraphUtil.h"
#include "BaseNodes/TransferNode.h"

#include "MeshProcessingNodes/MeshThickenNode.h"
#include "MeshProcessingNodes/MeshSolidifyNode.h"
#include "MeshProcessingNodes/MeshVoxMorphologyNode.h"
#include "MeshProcessingNodes/MeshSimplifyNode.h"
#include "MeshProcessingNodes/MeshDeleteTrianglesNode.h"
#include "MeshProcessingNodes/CompactMeshNode.h"
#include "MeshProcessingNodes/TransferMeshMaterialIDsNode.h"

#include "MeshProcessingNodes/MeshNormalsNodes.h"
#include "MeshProcessingNodes/MeshTangentsNodes.h"

#include "MeshProcessingNodes/MeshAutoGenerateUVsNode.h"
#include "MeshProcessingNodes/MeshRecalculateUVsNode.h"
#include "MeshProcessingNodes/MeshRepackUVsNode.h"

#include "DataTypes/MeshImageBakingData.h"
#include "MeshBakingNodes/BakeMeshNormalMapNode.h"
#include "MeshBakingNodes/BakeMeshTextureImageNode.h"

#include "MeshDecompositionNodes/MakeTriangleSetsNode.h"
#include "PhysicsNodes/GenerateSimpleCollisionNode.h"
#include "GeometryFlowExecutor.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;
using namespace UE::GeometryFlow;


void FGenerateMeshLODGraph::SetSourceMesh(const FDynamicMesh3& SourceMeshIn)
{
	UpdateSourceNodeValue<FDynamicMeshSourceNode>(*Graph, MeshSourceNode, SourceMeshIn);
}


#if 0

void FGenerateMeshLODGraph::EvaluateResultParallel(
	FDynamicMesh3& ResultMesh,
	FMeshTangentsd& ResultTangents,
	FSimpleShapeSet3d& ResultCollision,
	UE::GeometryFlow::FNormalMapImage& NormalMap,
	TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>>& TextureImages)
{
	//FScopedDurationTimeLogger Timer(TEXT("FGenerateMeshLODGraph::EvaluateResult -- parallel execution"));

	TArray<FGeometryFlowExecutor::NodeOutputSpec> DesiredOutputs;

	bool bTakeNormalMap = true;
	DesiredOutputs.Add(FGeometryFlowExecutor::NodeOutputSpec{ 
		BakeNormalMapNode, 
		FBakeMeshNormalMapNode::OutParamNormalMap(),
		bTakeNormalMap });

	bool bTakeTexBake = true;
	for (FBakeTextureGraphInfo& TexBakeStep : BakeTextureNodes)
	{
		DesiredOutputs.Add(FGeometryFlowExecutor::NodeOutputSpec{
			TexBakeStep.BakeNode,
			FBakeMeshTextureImageNode::OutParamTextureImage(),
			bTakeTexBake });
	}

	bool bTakeResultCollision = false;
	DesiredOutputs.Add(FGeometryFlowExecutor::NodeOutputSpec{
			CollisionOutputNode,
			FCollisionGeometryTransferNode::OutParamValue(),
			bTakeResultCollision });

	bool bTakeResultTangents = false;
	DesiredOutputs.Add(FGeometryFlowExecutor::NodeOutputSpec{
		TangentsOutputNode,
		FMeshTangentsTransferNode::OutParamValue(),
		bTakeResultTangents });

	bool bTakeResultMesh = true;
	DesiredOutputs.Add(FGeometryFlowExecutor::NodeOutputSpec{
		MeshOutputNode,
		FDynamicMeshTransferNode::OutParamValue(),
		bTakeResultMesh });

	FGeometryFlowExecutor Exec(*Graph);
	TArray<TSafeSharedPtr<IData>> OutputDatas;
	Exec.ComputeOutputs(DesiredOutputs, OutputDatas);

	check(OutputDatas.Num() == DesiredOutputs.Num());
	TArray<TSafeSharedPtr<IData>>::TIterator OutputDataIter = OutputDatas.CreateIterator();

	NormalMap = FNormalMapImage();
	EGeometryFlowResult ExtractResult = ExtractData(*OutputDataIter,
													NormalMap,
													(int)EMeshProcessingDataTypes::NormalMapImage,
													bTakeNormalMap);
	check(ExtractResult == EGeometryFlowResult::Ok);
	++OutputDataIter;

	for (FBakeTextureGraphInfo& TexBakeStep : BakeTextureNodes)
	{
		// { TexBakeStep.BakeNode, FBakeMeshTextureImageNode::OutParamTextureImage() }
		TUniquePtr<UE::GeometryFlow::FTextureImage> NewImage = MakeUnique<UE::GeometryFlow::FTextureImage>();
		
		ExtractResult = ExtractData(*OutputDataIter, *NewImage, (int)EMeshProcessingDataTypes::TextureImage, bTakeTexBake);
		check(ExtractResult == EGeometryFlowResult::Ok);
		TextureImages.Add(MoveTemp(NewImage));
		++OutputDataIter;
	}

	// {CollisionOutputNode, FCollisionGeometryTransferNode::OutParamValue() }
	ResultCollision = FSimpleShapeSet3d();
	ExtractResult = ExtractData(*OutputDataIter, ResultCollision, (int)FCollisionGeometry::DataTypeIdentifier, bTakeResultCollision);
	check(ExtractResult == EGeometryFlowResult::Ok);
	++OutputDataIter;

	// { TangentsOutputNode, FMeshTangentsTransferNode::OutParamValue() }
	ResultTangents = FMeshTangentsd();
	ExtractResult = ExtractData(*OutputDataIter, ResultTangents, (int)EMeshProcessingDataTypes::MeshTangentSet, bTakeResultTangents);
	check(ExtractResult == EGeometryFlowResult::Ok);
	++OutputDataIter;

	// {MeshOutputNode, FDynamicMeshTransferNode::OutParamValue() }
	ResultMesh.Clear();
	ExtractResult = ExtractData(*OutputDataIter, ResultMesh, (int32)EMeshProcessingDataTypes::DynamicMesh, bTakeResultMesh);
	check(ExtractResult == EGeometryFlowResult::Ok);
	++OutputDataIter;
}

#else

void FGenerateMeshLODGraph::EvaluateResultParallel(
	FDynamicMesh3& ResultMesh,
	FMeshTangentsd& ResultTangents,
	FSimpleShapeSet3d& ResultCollision,
	UE::GeometryFlow::FNormalMapImage& NormalMap,
	TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>>& TextureImages,
	FProgressCancel* Progress)
{
	//FScopedDurationTimeLogger Timer(TEXT("FGenerateMeshLODGraph::EvaluateResult -- parallel execution"));

	FGeometryFlowExecutor Exec(*Graph);
	Exec.AsyncRunGraph(Progress);

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	NormalMap = FNormalMapImage();
	bool bTakeNormalMap = true;
	EGeometryFlowResult ExtractResult = Exec.GetOutput(BakeNormalMapNode,
													   FBakeMeshNormalMapNode::OutParamNormalMap(),
													   NormalMap, 
													   (int)EMeshProcessingDataTypes::NormalMapImage, 
													   bTakeNormalMap);
	if (ExtractResult == EGeometryFlowResult::OperationCancelled)
	{
		return;
	}
	check(ExtractResult == EGeometryFlowResult::Ok);

	for (FBakeTextureGraphInfo& TexBakeStep : BakeTextureNodes)
	{
		TUniquePtr<UE::GeometryFlow::FTextureImage> NewImage = MakeUnique<UE::GeometryFlow::FTextureImage>();
		bool bTakeTexBake = true;
		ExtractResult = Exec.GetOutput(TexBakeStep.BakeNode,
									   FBakeMeshTextureImageNode::OutParamTextureImage(),
									   *NewImage,
									   (int)EMeshProcessingDataTypes::TextureImage,
									   bTakeNormalMap);
		if (ExtractResult == EGeometryFlowResult::OperationCancelled)
		{
			return;
		}
		check(ExtractResult == EGeometryFlowResult::Ok);
		TextureImages.Add(MoveTemp(NewImage));
	}

	ResultCollision = FSimpleShapeSet3d();
	bool bTakeResultCollision = false;
	ExtractResult = Exec.GetOutput(CollisionOutputNode,
								   FCollisionGeometryTransferNode::OutParamValue(),
								   ResultCollision,
								   (int)FCollisionGeometry::DataTypeIdentifier, bTakeResultCollision);
	if (ExtractResult == EGeometryFlowResult::OperationCancelled)
	{
		return;
	}
	check(ExtractResult == EGeometryFlowResult::Ok);

	bool bTakeResultTangents = false;
	ResultTangents = FMeshTangentsd();
	ExtractResult = Exec.GetOutput(TangentsOutputNode, 
								   FMeshTangentsTransferNode::OutParamValue(),
								   ResultTangents, 
								   (int)EMeshProcessingDataTypes::MeshTangentSet, 
								   bTakeResultTangents);
	if (ExtractResult == EGeometryFlowResult::OperationCancelled)
	{
		return;
	}
	check(ExtractResult == EGeometryFlowResult::Ok);
	

	bool bTakeResultMesh = true;
	ResultMesh.Clear();
	ExtractResult = Exec.GetOutput(MeshOutputNode, 
								   FDynamicMeshTransferNode::OutParamValue(),
								   ResultMesh, 
								   (int32)EMeshProcessingDataTypes::DynamicMesh, 
								   bTakeResultMesh);
	if (ExtractResult == EGeometryFlowResult::OperationCancelled)
	{
		return;
	}
	check(ExtractResult == EGeometryFlowResult::Ok);
}

#endif


void FGenerateMeshLODGraph::UpdatePreFilterSettings(const FMeshLODGraphPreFilterSettings& PreFilterSettings)
{
	UpdateSourceNodeValue<FNameSourceNode>(*Graph, FilterGroupsLayerNameNode, PreFilterSettings.FilterGroupLayerName);
	CurrentPreFilterSettings = PreFilterSettings;
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


void FGenerateMeshLODGraph::UpdateGenerateSimpleCollisionSettings(const FGenerateSimpleCollisionSettings& GenSimpleCollisionSettings)
{
	UpdateSettingsSourceNodeValue(*Graph, GenerateSimpleCollisionSettingsNode, GenSimpleCollisionSettings);
	CurrentGenerateSimpleCollisionSettings = GenSimpleCollisionSettings;
}

void FGenerateMeshLODGraph::UpdateThickenWeightMap(const TArray<float>& ThickenWeightMap)
{
	FWeightMap WeightMap;
	WeightMap.Weights = ThickenWeightMap;
	UpdateSourceNodeValue<FWeightMapSourceNode>(*Graph, ThickenWeightMapNode, WeightMap);
}

void FGenerateMeshLODGraph::UpdateThickenSettings(const UE::GeometryFlow::FMeshThickenSettings& ThickenSettings)
{
	UpdateSettingsSourceNodeValue(*Graph, ThickenSettingsNode, ThickenSettings);
	CurrentThickenSettings = ThickenSettings;
}

void FGenerateMeshLODGraph::UpdateCollisionGroupLayerName(const FName& NewCollisionGroupLayerName)
{
	CollisionGroupLayerName = NewCollisionGroupLayerName;
	UpdateSourceNodeValue<FNameSourceNode>(*Graph, GroupLayerNameNode, CollisionGroupLayerName);
}


void FGenerateMeshLODGraph::EvaluateResult(
	FDynamicMesh3& ResultMesh,
	FMeshTangentsd& ResultTangents,
	FSimpleShapeSet3d& ResultCollision,
	UE::GeometryFlow::FNormalMapImage& NormalMap,
	TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>>& TextureImages,
	FProgressCancel* Progress)
{
	//FScopedDurationTimeLogger Timer(TEXT("FGenerateMeshLODGraph::EvaluateResult -- serial execution"));

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	//
	// evaluate normal map
	//

	NormalMap = FNormalMapImage();
	TUniquePtr<FEvaluationInfo> NormalMapEvalInfo = MakeUnique<FEvaluationInfo>();
	NormalMapEvalInfo->Progress = Progress;
	EGeometryFlowResult NormalMapEvalResult = Graph->EvaluateResult(BakeNormalMapNode, FBakeMeshNormalMapNode::OutParamNormalMap(),
		NormalMap, (int)EMeshProcessingDataTypes::NormalMapImage, NormalMapEvalInfo, true);

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	ensure(NormalMapEvalResult == EGeometryFlowResult::Ok);
	UE_LOG(LogTemp, Warning, TEXT("NormalMapPass - Evaluated %d Nodes, Recomputed %d"), NormalMapEvalInfo->NumEvaluations(), NormalMapEvalInfo->NumComputes());


	//
	// evaluate transferred textures
	//

	for (FBakeTextureGraphInfo& TexBakeStep : BakeTextureNodes)
	{
		TUniquePtr<UE::GeometryFlow::FTextureImage> NewImage = MakeUnique<UE::GeometryFlow::FTextureImage>();
		TUniquePtr<FEvaluationInfo> TexBakeEvalInfo = MakeUnique<FEvaluationInfo>();
		TexBakeEvalInfo->Progress = Progress;
		EGeometryFlowResult TexBakeEvalResult = Graph->EvaluateResult(TexBakeStep.BakeNode, FBakeMeshTextureImageNode::OutParamTextureImage(),
			*NewImage, (int)EMeshProcessingDataTypes::TextureImage, TexBakeEvalInfo, true);

		if (Progress && Progress->Cancelled())
		{
			return;
		}

		TextureImages.Add(MoveTemp(NewImage));
		ensure(TexBakeEvalResult == EGeometryFlowResult::Ok);

		UE_LOG(LogTemp, Warning, TEXT("TextureBakePass %s - Evaluated %d Nodes, Recomputed %d"), *TexBakeStep.Identifier, TexBakeEvalInfo->NumEvaluations(), TexBakeEvalInfo->NumComputes());
	}

	// 
	// evaluate tangents
	//

	bool bTakeResultTangents = false;
	ResultTangents = FMeshTangentsd();

	TUniquePtr<FEvaluationInfo> TangentsEvalInfo = MakeUnique<FEvaluationInfo>();
	TangentsEvalInfo->Progress = Progress;
	EGeometryFlowResult TangentsEvalResult = Graph->EvaluateResult(TangentsOutputNode, FMeshTangentsTransferNode::OutParamValue(),
		ResultTangents, (int)EMeshProcessingDataTypes::MeshTangentSet, TangentsEvalInfo, bTakeResultTangents);

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	ensure(TangentsEvalResult == EGeometryFlowResult::Ok);
	UE_LOG(LogTemp, Warning, TEXT("OutputTangentsPass - Evaluated %d Nodes, Recomputed %d"), TangentsEvalInfo->NumEvaluations(), TangentsEvalInfo->NumComputes());


	//
	// evaluate result mesh
	// 


	bool bTakeResultMesh = true;
	ResultMesh.Clear();

	TUniquePtr<FEvaluationInfo> MeshEvalInfo = MakeUnique<FEvaluationInfo>();
	MeshEvalInfo->Progress = Progress;
	EGeometryFlowResult EvalResult = Graph->EvaluateResult(MeshOutputNode, FDynamicMeshTransferNode::OutParamValue(),
		ResultMesh, (int32)EMeshProcessingDataTypes::DynamicMesh, MeshEvalInfo, bTakeResultMesh);

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	ensure(EvalResult == EGeometryFlowResult::Ok);
	UE_LOG(LogTemp, Warning, TEXT("OutputMeshPass - Evaluated %d Nodes, Recomputed %d"), MeshEvalInfo->NumEvaluations(), MeshEvalInfo->NumComputes());

	//
	// evaluate collision
	//

	bool bTakeResultCollision = false;
	ResultCollision = FSimpleShapeSet3d();

	TUniquePtr<FEvaluationInfo> CollisionEvalInfo = MakeUnique<FEvaluationInfo>();
	CollisionEvalInfo->Progress = Progress;
	EGeometryFlowResult CollisionEvalResult = Graph->EvaluateResult(CollisionOutputNode, FCollisionGeometryTransferNode::OutParamValue(),
																	ResultCollision, FCollisionGeometry::DataTypeIdentifier, CollisionEvalInfo, bTakeResultCollision);

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	ensure(CollisionEvalResult == EGeometryFlowResult::Ok);
	UE_LOG(LogTemp, Warning, TEXT("OutputCollisionPass - Evaluated %d Nodes, Recomputed %d"), CollisionEvalInfo->NumEvaluations(), CollisionEvalInfo->NumComputes());

}





void FGenerateMeshLODGraph::BuildGraph()
{
	Graph = MakeUnique<FGraph>();

	MeshSourceNode = Graph->AddNodeOfType<FDynamicMeshSourceNode>(TEXT("SourceMesh"));

	// remove detail triangles

	FGraph::FHandle FilterGroupsNode = Graph->AddNodeOfType<FIndexSetsSourceNode>(TEXT("FilterGroups"));
	FilterGroupsLayerNameNode = Graph->AddNodeOfType<FNameSourceNode>(TEXT("FilterGroupsLayerNameSource"));

	FGraph::FHandle MakeFilterTriangleSetsNode = Graph->AddNodeOfType<FMakeTriangleSetsFromGroupsNode>(TEXT("MakeFilterTriangles"));
	Graph->InferConnection(MeshSourceNode, MakeFilterTriangleSetsNode);
	Graph->InferConnection(FilterGroupsNode, MakeFilterTriangleSetsNode);
	Graph->InferConnection(FilterGroupsLayerNameNode, MakeFilterTriangleSetsNode);

	FilterTrianglesNode = Graph->AddNodeOfType<FMeshDeleteTrianglesNode>(TEXT("FilterMesh"));
	Graph->InferConnection(MeshSourceNode, FilterTrianglesNode);
	Graph->InferConnection(MakeFilterTriangleSetsNode, FilterTrianglesNode);

	// generating low-poly mesh

	// optionally thicken some parts of the mesh before solidifying
	ThickenNode = Graph->AddNodeOfType<FMeshThickenNode>(TEXT("Thicken"));
	ThickenWeightMapNode = Graph->AddNodeOfType<FWeightMapSourceNode>(TEXT("ThickenWeightMapNode"));
	ThickenSettingsNode = Graph->AddNodeOfType<FThickenSettingsSourceNode>(TEXT("ThickenSettingsSource"));

	Graph->InferConnection(ThickenWeightMapNode, ThickenNode);
	Graph->InferConnection(ThickenSettingsNode, ThickenNode);
	Graph->InferConnection(FilterTrianglesNode, ThickenNode);

	SolidifyNode = Graph->AddNodeOfType<FSolidifyMeshNode>(TEXT("Solidify"));
	Graph->InferConnection(ThickenNode, SolidifyNode);
	SolidifySettingsNode = Graph->AddNodeOfType<FSolidifySettingsSourceNode>(TEXT("SolidifySettings"));
	Graph->InferConnection(SolidifySettingsNode, SolidifyNode);

	MorphologyNode = Graph->AddNodeOfType<FVoxClosureMeshNode>(TEXT("Closure"));
	Graph->InferConnection(SolidifyNode, MorphologyNode);
	MorphologySettingsNode = Graph->AddNodeOfType<FVoxClosureSettingsSourceNode>(TEXT("ClosureSettings"));
	Graph->InferConnection(MorphologySettingsNode, MorphologyNode);

	// todo: if we only have one material ID we can skip this...
	FGraph::FHandle MatIDTransferNode = Graph->AddNodeOfType<FTransferMeshMaterialIDsNode>(TEXT("TransferMaterialIDs"));
	Graph->AddConnection(MeshSourceNode, FDynamicMeshSourceNode::OutParamValue(), MatIDTransferNode, FTransferMeshMaterialIDsNode::InParamMaterialSourceMesh());
	Graph->InferConnection(MorphologyNode, MatIDTransferNode);

	// need to compute valid normals before Simplify, Morphology node does not necessarily do it
	FGraph::FHandle PerVertexNormalsNode = Graph->AddNodeOfType<FComputeMeshPerVertexOverlayNormalsNode>(TEXT("PerVertexNormals"));
	Graph->InferConnection(MatIDTransferNode, PerVertexNormalsNode);

	SimplifyNode = Graph->AddNodeOfType<FSimplifyMeshNode>(TEXT("Simplify"));
	Graph->InferConnection(PerVertexNormalsNode, SimplifyNode);
	SimplifySettingsNode = Graph->AddNodeOfType<FSimplifySettingsSourceNode>(TEXT("SimplifySettings"));
	Graph->InferConnection(SimplifySettingsNode, SimplifyNode);

	FGraph::FHandle CompactNode = Graph->AddNodeOfType<FCompactMeshNode>(TEXT("Compact"));
	Graph->InferConnection(SimplifyNode, CompactNode);

	NormalsNode = Graph->AddNodeOfType<FComputeMeshNormalsNode>(TEXT("Normals"));
	Graph->InferConnection(CompactNode, NormalsNode);
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


	// collision generation

	FGraph::FHandle IgnoreGroupsForCollisionNode = Graph->AddNodeOfType<FIndexSetsSourceNode>(TEXT("CollisionIgnoreGroups"));

	//DecomposeMeshForCollisionNode = Graph->AddNodeOfType<FMakeTriangleSetsFromMeshNode>(TEXT("Decompose"));
	DecomposeMeshForCollisionNode = Graph->AddNodeOfType<FMakeTriangleSetsFromGroupsNode>(TEXT("Decompose"));
	Graph->InferConnection(FilterTrianglesNode, DecomposeMeshForCollisionNode);
	Graph->InferConnection(IgnoreGroupsForCollisionNode, DecomposeMeshForCollisionNode);

	GroupLayerNameNode = Graph->AddNodeOfType<FNameSourceNode>(TEXT("GroupLayerNameNode"));
	Graph->AddConnection(GroupLayerNameNode, FNameSourceNode::OutParamValue(), 
						 DecomposeMeshForCollisionNode, FMakeTriangleSetsFromGroupsNode::InParamGroupLayer());

	GenerateSimpleCollisionNode = Graph->AddNodeOfType<FGenerateSimpleCollisionNode>(TEXT("GenerateSimpleCollision"));
	Graph->InferConnection(FilterTrianglesNode, GenerateSimpleCollisionNode);
	Graph->InferConnection(DecomposeMeshForCollisionNode, GenerateSimpleCollisionNode);
	GenerateSimpleCollisionSettingsNode = Graph->AddNodeOfType<FGenerateSimpleCollisionSettingsSourceNode>(TEXT("GenerateSimpleCollisionSettings"));
	Graph->InferConnection(GenerateSimpleCollisionSettingsNode, GenerateSimpleCollisionNode);

	// final collision output

	CollisionOutputNode = Graph->AddNodeOfType<FCollisionGeometryTransferNode>(TEXT("OutputCollision"));
	Graph->InferConnection(GenerateSimpleCollisionNode, CollisionOutputNode);


	//
	// parameters
	//


	FIndexSets IgnoreGroupsForDelete;
	IgnoreGroupsForDelete.AppendSet({ 0 });
	UpdateSettingsSourceNodeValue(*Graph, FilterGroupsNode, IgnoreGroupsForDelete);

	FMeshLODGraphPreFilterSettings PreFilterSettings;
	PreFilterSettings.FilterGroupLayerName = FName(TEXT("PreFilterGroups"));
	UpdatePreFilterSettings(PreFilterSettings);

	FMeshSolidifySettings SolidifySettings;
	UpdateSolidifySettings(SolidifySettings);

	FVoxClosureSettings MorphologySettings;
	MorphologySettings.Distance = 5.0;
	UpdateMorphologySettings(MorphologySettings);

	FMeshSimplifySettings SimplifySettings;
	SimplifySettings.bDiscardAttributes = false;
	SimplifySettings.SimplifyType = EMeshSimplifyType::AttributeAware;
	SimplifySettings.TargetType = EMeshSimplifyTargetType::TriangleCount;
	SimplifySettings.TargetCount = 500;
	SimplifySettings.MaterialBorderConstraints = EEdgeRefineFlags::NoFlip;
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

	UpdateCollisionGroupLayerName(CollisionGroupLayerName);

	FGenerateSimpleCollisionSettings GenSimpleCollisionSettings;
	UpdateGenerateSimpleCollisionSettings(GenSimpleCollisionSettings);

	TArray<float> Weights;
	UpdateThickenWeightMap(Weights);


	//FString GraphDump = Graph->DebugDumpGraph([](TSafeSharedPtr<FNode> Node)
	//{
	//	return !Node->GetIdentifier().EndsWith("Settings");
	//});
	//UE_LOG(LogTemp, Warning, TEXT("GRAPH:\n%s"), *GraphDump);

}




int32 FGenerateMeshLODGraph::AppendTextureBakeNode(const TImageBuilder<UE::Geometry::FVector4f>& SourceImage, const FString& Identifier)
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
