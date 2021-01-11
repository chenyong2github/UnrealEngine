// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsNodes/GenerateSimpleCollisionNode.h"
#include "ShapeApproximation/MeshSimpleShapeApproximation.h"

using namespace UE::GeometryFlow;

void FGenerateSimpleCollisionNode::Evaluate(
	const FNamedDataMap& DatasIn,
	FNamedDataMap& DatasOut,
	TUniquePtr<FEvaluationInfo>& EvaluationInfo)
{
	if (ensure(DatasOut.Contains(OutParamGeometry())))
	{
		bool bAllInputsValid = true;
		bool bRecomputeRequired = (IsOutputAvailable(OutParamGeometry()) == false);

		TSafeSharedPtr<IData> MeshArg = FindAndUpdateInputForEvaluate(InParamMesh(), 
																	  DatasIn, 
																	  bRecomputeRequired, 
																	  bAllInputsValid);
		const FDynamicMesh3& Mesh = MeshArg->GetDataConstRef<FDynamicMesh3>((int)EMeshProcessingDataTypes::DynamicMesh);

		TSafeSharedPtr<IData> SettingsArg = FindAndUpdateInputForEvaluate(InParamSettings(), 
																		  DatasIn, 
																		  bRecomputeRequired, bAllInputsValid);
		FGenerateSimpleCollisionSettings Settings;
		SettingsArg->GetDataCopy(Settings, FGenerateSimpleCollisionSettings::DataTypeIdentifier);
	
		if (bAllInputsValid)
		{
			if (bRecomputeRequired)
			{
				FMeshSimpleShapeApproximation ShapeApproximator;
				ShapeApproximator.bDetectSpheres = false;
				ShapeApproximator.bDetectBoxes = false;
				ShapeApproximator.bDetectCapsules = false;
				ShapeApproximator.bDetectConvexes = false;

				ShapeApproximator.InitializeSourceMeshes({ &Mesh });

				FCollisionGeometry CollisionGeometry;
				switch (Settings.Type)
				{
				case ESimpleCollisionGeometryType::AlignedBoxes:
					ShapeApproximator.bDetectBoxes = true;
					ShapeApproximator.Generate_AlignedBoxes(CollisionGeometry.Geometry);
					break;
				case ESimpleCollisionGeometryType::OrientedBoxes:
					ShapeApproximator.bDetectBoxes = true;
					ShapeApproximator.Generate_OrientedBoxes(CollisionGeometry.Geometry);
					break;
				case ESimpleCollisionGeometryType::MinimalSpheres:
					ShapeApproximator.bDetectSpheres = true;
					ShapeApproximator.Generate_MinimalSpheres(CollisionGeometry.Geometry);
					break;
				case ESimpleCollisionGeometryType::Capsules:
					ShapeApproximator.bDetectCapsules = true;
					ShapeApproximator.Generate_Capsules(CollisionGeometry.Geometry);
					break;
				}

				SetOutput(OutParamGeometry(), MakeMovableData<FCollisionGeometry>(MoveTemp(CollisionGeometry)));
			}

			DatasOut.SetData(OutParamGeometry(), GetOutput(OutParamGeometry()));
		}
	}
}
