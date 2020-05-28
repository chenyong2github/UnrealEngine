// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTypes.h"

DEFINE_LOG_CATEGORY( LogMeshDescription );


const FElementID FElementID::Invalid( INDEX_NONE );
const FVertexID FVertexID::Invalid( INDEX_NONE );
const FVertexInstanceID FVertexInstanceID::Invalid( INDEX_NONE );
const FEdgeID FEdgeID::Invalid( INDEX_NONE );
const FTriangleID FTriangleID::Invalid( INDEX_NONE );
const FPolygonGroupID FPolygonGroupID::Invalid( INDEX_NONE );
const FPolygonID FPolygonID::Invalid( INDEX_NONE );

#if 0
const FName UEditableMeshAttribute::VertexPositionName( "VertexPosition" );
const FName UEditableMeshAttribute::VertexCornerSharpnessName( "VertexCornerSharpness" );
const FName UEditableMeshAttribute::VertexNormalName( "VertexNormal" );
const FName UEditableMeshAttribute::VertexTangentName( "VertexTangent" );
const FName UEditableMeshAttribute::VertexBinormalSignName( "VertexBinormalSign" );
const FName UEditableMeshAttribute::VertexTextureCoordinateName( "VertexTextureCoordinate" );
const FName UEditableMeshAttribute::VertexColorName( "VertexColor" );
const FName UEditableMeshAttribute::EdgeIsHardName( "EdgeIsHard" );
const FName UEditableMeshAttribute::EdgeCreaseSharpnessName( "EdgeCreaseSharpness" );
const FName UEditableMeshAttribute::PolygonNormalName( "PolygonNormal" );
const FName UEditableMeshAttribute::PolygonCenterName( "PolygonCenter" );
#endif
