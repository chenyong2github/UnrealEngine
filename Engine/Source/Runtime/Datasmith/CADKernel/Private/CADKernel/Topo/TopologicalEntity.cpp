// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Topo/TopologicalEntity.h"

namespace CADKernel
{

#ifdef CADKERNEL_DEV
FInfoEntity& FTopologicalEntity::GetInfo(FInfoEntity& Info) const
{
	return FEntity::GetInfo(Info)
		.Add(TEXT("IsMesh"), IsMeshed());
}
#endif

}
