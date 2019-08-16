// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Blueprints/MPCDIContainers.h"


void FMPCDIGeometryExportData::PostAddFace(int f0, int f1, int f2)
{
	Triangles.Add(f0);
	Triangles.Add(f1);
	Triangles.Add(f2);

	//Update normal
	const FVector FaceDir1 = Vertices[f1] - Vertices[f0];
	const FVector FaceDir2 = Vertices[f2] - Vertices[f0];

	const FVector FaceNornal = FVector::CrossProduct(FaceDir1, FaceDir2);

	Normal[f0] = FaceNornal;
	Normal[f1] = FaceNornal;
	Normal[f2] = FaceNornal;
}
