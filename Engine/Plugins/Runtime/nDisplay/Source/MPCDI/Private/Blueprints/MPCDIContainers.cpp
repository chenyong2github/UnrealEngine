// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Blueprints/MPCDIContainers.h"


void FMPCDIGeometryExportData::PostAddFace(int f0, int f1, int f2)
{
	Triangles.Add(f0);
	Triangles.Add(f1);
	Triangles.Add(f2);

	//Update normal
	const FVector dir1 = Vertices[f1] - Vertices[f0];
	const FVector dir2 = Vertices[f2] - Vertices[f0];

	const FVector N = FVector::CrossProduct(dir1, dir2);

	Normal[f0] = N;
	Normal[f1] = N;
	Normal[f2] = N;
}
