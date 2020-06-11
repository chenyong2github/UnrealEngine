// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"
#include "MeshTangents.h"
#include "CalculateTangentsOp.generated.h"



UENUM()
enum class EMeshTangentsType : uint8
{
	/** Standard MikkTSpace Tangent Calculation */
	MikkTSpace = 0,
	/** MikkT-like Blended Per-Triangle Tangents, with Blending based on existing Mesh/UV/Normal Topology */
	FastMikkTSpace = 1,
	/** Project per-triangle Tangents projected onto Normals */
	PerTriangle = 2,
	/** Use Source Mesh Tangents */
	CopyExisting = 3
};



class MODELINGOPERATORSEDITORONLY_API FCalculateTangentsOp : public TGenericDataOperator<FMeshTangentsd>
{
public:
	virtual ~FCalculateTangentsOp() {}

	// inputs
	TSharedPtr<FDynamicMesh3> SourceMesh;
	TSharedPtr<FMeshTangentsf> SourceTangents;

	// parameters
	EMeshTangentsType CalculationMethod;

	// error flags
	bool bNoAttributesError = false;

	//
	// TGenericDataOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;


	virtual void CalculateStandard(FProgressCancel* Progress, TUniquePtr<FMeshTangentsd>& Tangents);
	virtual void CalculateMikkT(FProgressCancel* Progress, TUniquePtr<FMeshTangentsd>& Tangents);
	virtual void CopyFromSource(FProgressCancel* Progress, TUniquePtr<FMeshTangentsd>& Tangents);
};


