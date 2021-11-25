// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversionUtils/DynamicMeshViaMeshDescriptionUtil.h"

#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshConversionOptions.h" //FConversionToMeshDescriptionOptions
#include "MeshDescriptionToDynamicMesh.h"

FDynamicMesh3 UE::Geometry::GetDynamicMeshViaMeshDescription(
	IMeshDescriptionProvider& MeshDescriptionProvider)
{
	FDynamicMesh3 DynamicMesh;
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(MeshDescriptionProvider.GetMeshDescription(), DynamicMesh);
	return DynamicMesh;
}

void UE::Geometry::CommitDynamicMeshViaMeshDescription(
	IMeshDescriptionCommitter& MeshDescriptionCommitter, 
	const FDynamicMesh3& Mesh, const IDynamicMeshCommitter::FDynamicMeshCommitInfo& CommitInfo)
{
	FConversionToMeshDescriptionOptions ConversionOptions;
	ConversionOptions.bSetPolyGroups = CommitInfo.bPolygroupsChanged;
	ConversionOptions.bUpdatePositions = CommitInfo.bPositionsChanged;
	ConversionOptions.bUpdateNormals = CommitInfo.bNormalsChanged;
	ConversionOptions.bUpdateTangents = CommitInfo.bTangentsChanged;
	ConversionOptions.bUpdateUVs = CommitInfo.bUVsChanged;
	ConversionOptions.bUpdateVtxColors = CommitInfo.bVertexColorsChanged;
	ConversionOptions.bTransformVtxColorsSRGBToLinear = CommitInfo.bTransformVertexColorsSRGBToLinear;

	MeshDescriptionCommitter.CommitMeshDescription([&CommitInfo, &ConversionOptions, &Mesh](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
	{
		FDynamicMeshToMeshDescription Converter(ConversionOptions);

		if (!CommitInfo.bTopologyChanged)
		{
			Converter.UpdateUsingConversionOptions(&Mesh, *CommitParams.MeshDescriptionOut);
		}
		else
		{
			// Do a full conversion.
			Converter.Convert(&Mesh, *CommitParams.MeshDescriptionOut);
		}
	});
}