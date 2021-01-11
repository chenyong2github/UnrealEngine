// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryFlowCoreNodes.h"
#include "GeometryFlowNodeUtil.h"
#include "MeshProcessingNodes/MeshProcessingDataTypes.h"
#include "DataTypes/DynamicMeshData.h"
#include "DataTypes/CollisionGeometryData.h"
#include "DataTypes/IndexSetsData.h"


namespace UE
{
namespace GeometryFlow
{

enum class ESimpleCollisionGeometryType
{
	AlignedBoxes = 1,
	OrientedBoxes = 2,
	MinimalSpheres = 3,
	Capsules = 4
};


struct GEOMETRYFLOWMESHPROCESSING_API FGenerateSimpleCollisionSettings
{
	DECLARE_GEOMETRYFLOW_DATA_TYPE_IDENTIFIER(EMeshProcessingDataTypes::GenerateSimpleCollisionSettings);

	ESimpleCollisionGeometryType Type = ESimpleCollisionGeometryType::MinimalSpheres;
};
GEOMETRYFLOW_DECLARE_SETTINGS_TYPES(FGenerateSimpleCollisionSettings, GenerateSimpleCollision);


class GEOMETRYFLOWMESHPROCESSING_API FGenerateSimpleCollisionNode : public FNode
{
protected:
	using SettingsDataType = TMovableData<FGenerateSimpleCollisionSettings, FGenerateSimpleCollisionSettings::DataTypeIdentifier>;

public:
	static const FString InParamMesh() { return TEXT("Mesh"); }
	// TODO: static const FString InParamIndexSets() { return TEXT("TriangleSets"); }	
	static const FString InParamSettings() { return TEXT("Settings"); }
	static const FString OutParamGeometry() { return TEXT("Geometry"); }

public:

	FGenerateSimpleCollisionNode()
	{
		AddInput(InParamMesh(), MakeUnique<FDynamicMeshInput>());
		// TODO: AddInput(InParamIndexSets(), MakeBasicInput<FIndexSets>());
		AddInput(InParamSettings(), MakeBasicInput<FGenerateSimpleCollisionSettings>());

		AddOutput(OutParamGeometry(), MakeBasicOutput<FCollisionGeometry>());
	}

	virtual void Evaluate(
		const FNamedDataMap& DatasIn,
		FNamedDataMap& DatasOut,
		TUniquePtr<FEvaluationInfo>& EvaluationInfo) override;
};

}	// end namespace GeometryFlow
}	// end namespace UE
