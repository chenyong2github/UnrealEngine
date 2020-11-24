// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsNodes/GenerateConvexHullsCollisionNode.h"

#include "MeshIndexUtil.h"
#include "Operations/MeshConvexHull.h"

#include "Async/ParallelFor.h"


using namespace UE::GeometryFlow;

void FGenerateConvexHullsCollisionNode::Evaluate(
	const FNamedDataMap& DatasIn,
	FNamedDataMap& DatasOut,
	TUniquePtr<FEvaluationInfo>& EvaluationInfo)
{
	if (ensure(DatasOut.Contains(OutParamGeometry())))
	{
		bool bAllInputsValid = true;
		bool bRecomputeRequired = (IsOutputAvailable(OutParamGeometry()) == false);
		TSafeSharedPtr<IData> MeshArg = FindAndUpdateInputForEvaluate(InParamMesh(), DatasIn, bRecomputeRequired, bAllInputsValid);
		TSafeSharedPtr<IData> TriSetsArg = FindAndUpdateInputForEvaluate(InParamIndexSets(), DatasIn, bRecomputeRequired, bAllInputsValid);
		TSafeSharedPtr<IData> SettingsArg = FindAndUpdateInputForEvaluate(InParamSettings(), DatasIn, bRecomputeRequired, bAllInputsValid);
		if (bAllInputsValid)
		{
			if (bRecomputeRequired)
			{
				// always make a copy of settings
				FGenerateConvexHullsCollisionSettings Settings;
				SettingsArg->GetDataCopy(Settings, FGenerateConvexHullsCollisionSettings::DataTypeIdentifier);

				const FDynamicMesh3& Mesh = MeshArg->GetDataConstRef<FDynamicMesh3>((int)EMeshProcessingDataTypes::DynamicMesh);
				const FIndexSets& IndexData = TriSetsArg->GetDataConstRef<FIndexSets>((int)EMeshProcessingDataTypes::IndexSets);

				int32 NumShapes = IndexData.IndexSets.Num();
				TArray<FConvexShape3d> Convexes;
				TArray<bool> ConvexOk;
				Convexes.SetNum(NumShapes);
				ConvexOk.SetNum(NumShapes);
				ParallelFor(NumShapes, [&](int32 k)
				{
					ConvexOk[k] = false;
					FMeshConvexHull Hull(&Mesh);
					MeshIndexUtil::TriangleToVertexIDs(&Mesh, IndexData.IndexSets[k], Hull.VertexSet);
					Hull.bPostSimplify = Settings.SimplifyToTriangleCount > 0;
					Hull.MaxTargetFaceCount = FMath::Max(1, Settings.SimplifyToTriangleCount);
					if (Hull.Compute())
					{
						FConvexShape3d NewConvex;
						NewConvex.Mesh = MoveTemp(Hull.ConvexHull);
						Convexes[k] = MoveTemp(NewConvex);
						ConvexOk[k] = true;
					}
				});

				FCollisionGeometry Result;
				Result.Geometry.Convexes = MoveTemp(Convexes);

				SetOutput(OutParamGeometry(), MakeMovableData<FCollisionGeometry>(MoveTemp(Result)));
				EvaluationInfo->CountCompute(this);
			}
			DatasOut.SetData(OutParamGeometry(), GetOutput(OutParamGeometry()));
		}
	}
}