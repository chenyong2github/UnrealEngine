// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryFlowCoreNodes.h"
#include "GeometryFlowNodeUtil.h"
#include "MeshProcessingNodes/MeshProcessingDataTypes.h"
#include "DataTypes/CollisionGeometryData.h"
#include "DataTypes/IndexSetsData.h"


namespace UE
{
namespace GeometryFlow
{




struct GEOMETRYFLOWMESHPROCESSING_API FGenerateConvexHullsCollisionSettings
{
	DECLARE_GEOMETRYFLOW_DATA_TYPE_IDENTIFIER(EMeshProcessingDataTypes::GenerateCollisionConvexHullsSettings);

	int32 SimplifyToTriangleCount = 50;
	
};
GEOMETRYFLOW_DECLARE_SETTINGS_TYPES(FGenerateConvexHullsCollisionSettings, GenerateConvexHullsCollision);



class GEOMETRYFLOWMESHPROCESSING_API FGenerateConvexHullsCollisionNode : public FNode
{
protected:
	using SettingsDataType = TMovableData<FGenerateConvexHullsCollisionSettings, FGenerateConvexHullsCollisionSettings::DataTypeIdentifier>;

public:
	static const FString InParamMesh() { return TEXT("Mesh"); }
	static const FString InParamIndexSets() { return TEXT("TriangleSets"); }
	static const FString InParamSettings() { return TEXT("Settings"); }

	static const FString OutParamGeometry() { return TEXT("Geometry"); }

public:
	FGenerateConvexHullsCollisionNode()
	{
		AddInput(InParamMesh(), MakeUnique<FDynamicMeshInput>());
		AddInput(InParamIndexSets(), MakeBasicInput<FIndexSets>());
		AddInput(InParamSettings(), MakeBasicInput<FGenerateConvexHullsCollisionSettings>());

		AddOutput(OutParamGeometry(), MakeBasicOutput<FCollisionGeometry>());
	}

	virtual void Evaluate(
		const FNamedDataMap& DatasIn,
		FNamedDataMap& DatasOut,
		TUniquePtr<FEvaluationInfo>& EvaluationInfo) override;
};






}	// end namespace GeometryFlow
}	// end namespace UE