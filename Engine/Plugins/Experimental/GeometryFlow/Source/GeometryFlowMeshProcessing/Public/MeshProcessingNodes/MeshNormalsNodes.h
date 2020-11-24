// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshProcessingNodes/MeshProcessingBaseNodes.h"
#include "MeshProcessingNodes/MeshProcessingDataTypes.h"

#include "MeshNormals.h"


namespace UE
{
namespace GeometryFlow
{


enum class EComputeNormalsType
{
	PerTriangle = 0,
	PerVertex = 1,
	RecomputeExistingTopology = 2,
	FromFaceAngleThreshold = 3,
	FromGroups = 4
};

struct FMeshNormalsSettings
{
	DECLARE_GEOMETRYFLOW_DATA_TYPE_IDENTIFIER(EMeshProcessingDataTypes::NormalsSettings);

	EComputeNormalsType NormalsType = EComputeNormalsType::FromFaceAngleThreshold;

	bool bInvert = false;

	bool bAreaWeighted = true;
	bool bAngleWeighted = true;

	// for FromAngleThreshold type
	double AngleThresholdDeg = 180.0;
};
GEOMETRYFLOW_DECLARE_SETTINGS_TYPES(FMeshNormalsSettings, Normals);



class FComputeMeshNormalsNode : public TProcessMeshWithSettingsBaseNode<FMeshNormalsSettings>
{
public:
	FComputeMeshNormalsNode() : TProcessMeshWithSettingsBaseNode<FMeshNormalsSettings>()
	{
		// we can mutate input mesh
		ConfigureInputFlags(InParamMesh(), FNodeInputFlags::Transformable());
	}


	virtual void ProcessMesh(
		const FNamedDataMap& DatasIn,
		const FMeshNormalsSettings& Settings,
		const FDynamicMesh3& MeshIn,
		FDynamicMesh3& MeshOut) override
	{
		MeshOut = MeshIn;
		ComputeNormals(Settings, MeshOut);
	}

	virtual void ProcessMeshInPlace(
		const FNamedDataMap& DatasIn,
		const FMeshNormalsSettings& Settings,
		FDynamicMesh3& MeshInOut)
	{
		ComputeNormals(Settings, MeshInOut);
	}


	void ComputeNormals(const FMeshNormalsSettings& Settings, FDynamicMesh3& MeshInOut)
	{
		if (MeshInOut.HasAttributes() == false)
		{
			MeshInOut.EnableAttributes();
		}
		FDynamicMeshNormalOverlay* Normals = MeshInOut.Attributes()->PrimaryNormals();

		if (Settings.NormalsType == EComputeNormalsType::PerTriangle)
		{
			ensure(Settings.bInvert == false);		// not supported
			FMeshNormals::InitializeMeshToPerTriangleNormals(&MeshInOut);
			return;
		}
		else if (Settings.NormalsType == EComputeNormalsType::PerVertex)
		{
			ensure(Settings.bInvert == false);		// not supported
			FMeshNormals::InitializeOverlayToPerVertexNormals(Normals, false);
			return;
		}


		if (Settings.NormalsType == EComputeNormalsType::FromFaceAngleThreshold)
		{
			FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&MeshInOut, Normals, Settings.AngleThresholdDeg);
		}
		else if (Settings.NormalsType == EComputeNormalsType::FromGroups)
		{
			FMeshNormals::InitializeOverlayTopologyFromFaceGroups(&MeshInOut, Normals);
		}
		else if (Settings.NormalsType != EComputeNormalsType::RecomputeExistingTopology)
		{
			ensure(false);
		}

		FMeshNormals MeshNormals(&MeshInOut);
		MeshNormals.RecomputeOverlayNormals(Normals, Settings.bAreaWeighted, Settings.bAngleWeighted);
		MeshNormals.CopyToOverlay(Normals, Settings.bInvert);
	}

};





}	// end namespace GeometryFlow
}	// end namespace UE