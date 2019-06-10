// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/RigidParticles.h"

using namespace Chaos;

//Note this has to be in the cpp to avoid allocating/freeing across DLLs
template<typename T, int d>
void TRigidParticles<T, d>::CollisionParticlesInitIfNeeded(const int32 Index, TArray<TVector<T, d>>* Points)
{
	if (MCollisionParticles[Index] == nullptr)
	{
		if (Points)
		{
			MCollisionParticles[Index] = MakeUnique<TBVHParticles<T, d>>(*Points);
		}
		else
		{
			MCollisionParticles[Index] = MakeUnique<TBVHParticles<T, d>>();
		}
	}
	else
	{
		// Make sure that if we were supposed to construct particles with a view to Points
		// that the existing particles already use the Points array.  Otherwise, maybe we
		// should make a copy?  Not sure...  At this point, we'll leave it an error so that
		// folks clean up their logic.
		check(!Points || MCollisionParticles[Index]->X().GetData() == Points->GetData());
	}
}

template class Chaos::TRigidParticles<float, 3>;