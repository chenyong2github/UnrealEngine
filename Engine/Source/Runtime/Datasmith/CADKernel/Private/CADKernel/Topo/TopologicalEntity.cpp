// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Topo/TopologicalEntity.h"


#ifdef CADKERNEL_DEV
CADKernel::FInfoEntity& CADKernel::FTopologicalEntity::GetInfo(FInfoEntity& Info) const
{
	return FEntityGeom::GetInfo(Info)
		.Add(TEXT("IsMesh"), IsMeshed());
}
#endif


