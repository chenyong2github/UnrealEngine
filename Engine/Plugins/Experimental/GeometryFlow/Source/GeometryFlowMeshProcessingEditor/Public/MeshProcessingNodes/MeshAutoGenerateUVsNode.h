// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshProcessingNodes/MeshProcessingBaseNodes.h"
#include "MeshProcessingNodes/MeshProcessingDataTypesEditor.h"

namespace UE
{
namespace GeometryFlow
{


struct GEOMETRYFLOWMESHPROCESSINGEDITOR_API FMeshAutoGenerateUVsSettings
{
	DECLARE_GEOMETRYFLOW_DATA_TYPE_IDENTIFIER(EMeshProcessingDataTypesEditor::MeshAutoGenerateUVsSettings);

	double Stretch = 0.5;
	int NumCharts = 0;

};
GEOMETRYFLOW_DECLARE_SETTINGS_TYPES(FMeshAutoGenerateUVsSettings, MeshAutoGenerateUVs);



class GEOMETRYFLOWMESHPROCESSINGEDITOR_API FMeshAutoGenerateUVsNode : public TProcessMeshWithSettingsBaseNode<FMeshAutoGenerateUVsSettings>
{
public:
	FMeshAutoGenerateUVsNode() : TProcessMeshWithSettingsBaseNode<FMeshAutoGenerateUVsSettings>()
	{
	}

	virtual void ProcessMesh(
		const FNamedDataMap& DatasIn,
		const FMeshAutoGenerateUVsSettings& Settings,
		const FDynamicMesh3& MeshIn,
		FDynamicMesh3& MeshOut) override
	{
		GenerateUVs(MeshIn, Settings, MeshOut);
	}

	virtual void GenerateUVs(const FDynamicMesh3& MeshIn, const FMeshAutoGenerateUVsSettings& Settings, FDynamicMesh3& MeshOut);

};





}	// end namespace GeometryFlow
}	// end namespace UE