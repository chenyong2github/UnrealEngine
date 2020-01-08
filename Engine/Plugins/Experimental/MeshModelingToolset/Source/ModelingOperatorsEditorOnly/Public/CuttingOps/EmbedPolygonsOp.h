// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"
#include "ProxyLODVolume.h"

#include "Polygon2.h"

#include "EmbedPolygonsOp.generated.h"


UENUM()
enum class EEmbeddedPolygonOpMethod : uint8
{
	CutAndFill,
	CutThrough
	//, Extrude  // TODO: extrude(/intrude?) would also be easy/natural to support here
};


class MODELINGOPERATORSEDITORONLY_API FEmbedPolygonsOp : public FDynamicMeshOperator
{
public:
	virtual ~FEmbedPolygonsOp() {}

	
	// inputs
	FVector LocalPlaneOrigin, LocalPlaneNormal;
	float PolygonScale;

	// TODO: stop hardcoding the polygon shape, switch to FGeneralPolygon2d
	FPolygon2d GetPolygon()
	{
		return FPolygon2d::MakeCircle(PolygonScale, 20);
	}

	bool bDiscardAttributes;

	EEmbeddedPolygonOpMethod Operation;

	//float ExtrudeDistance; // TODO if we support extrude

	TSharedPtr<FDynamicMesh3> OriginalMesh;

	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;
};


