// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestSpatialHashing.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Modules/ModuleManager.h"
#include "Chaos/Matrix.h"
#include "Chaos/PBDRigidClustering.h"

DEFINE_LOG_CATEGORY_STATIC(AHSP_Test, Verbose, All);


namespace ChaosTest {
	using namespace Chaos;

	template<class T, int Dimension>
	TArray< TVec3<T> > GenerateSamplePoints(int32 NumPoints, int32 InitialDistance) {

		TArray< TVec3<T> > ReturnValue;
		for (int i = 1; i <= NumPoints/2; i++) {
			int32 iCubed = i * i * i;
			TVec3<T> Vec1(0), Vec2(0);
			if (0<Dimension && Dimension >= 3)
			{
				Vec1 = TVec3<T>(float(FMath::Rand()) / RAND_MAX, float(FMath::Rand()) / RAND_MAX, float(FMath::Rand()) / RAND_MAX)*InitialDistance;
				Vec2 = TVec3<T>(float(FMath::Rand()) / RAND_MAX, float(FMath::Rand()) / RAND_MAX, float(FMath::Rand()) / RAND_MAX)*InitialDistance;
				TVec3<T> Mid((Vec1 - Vec2) / 2);
				Vec1 = Mid + (Mid - Vec1).Normalize()*(InitialDistance / float(iCubed));
				Vec2 = Mid - (Mid - Vec1).Normalize()*(InitialDistance / float(iCubed));
			}
			else
			{
				int32 OtherDim = (Dimension + 1) % 3;
				Vec1[Dimension] = InitialDistance / float(iCubed);
				Vec2[Dimension] = -InitialDistance / float(iCubed);
				Vec1[OtherDim] = Vec2[OtherDim] = i;
			}
			ReturnValue.Add(Vec1);
			ReturnValue.Add(Vec2);
		}
		return ReturnValue;
	}

	template<class T>
	void SpatialHashing()
	{
		UE_LOG(AHSP_Test, Verbose, TEXT("SpatialHashing"));
		for (int Dimension = 0; Dimension <= 3; Dimension++)
		{
			UE_LOG(AHSP_Test, Verbose, TEXT("::Dimension[%d]"), Dimension);
			TArray< TVec3<T> > Samples = Chaos::CleanCollisionParticles(GenerateSamplePoints<T, 3>(100, 1000), 1.0);
			for (int32 Index1 = 0; Index1 < Samples.Num(); Index1++)
			{
				for (int32 Index2 = 0; Index2 < Samples.Num(); Index2++)
				{
					if (Index1 != Index2 && Index1<Index2)
					{
						TVec3<T> Sample1(Samples[Index1]), Sample2(Samples[Index2]);
						float Delta = (Sample2 - Sample1).Size();
						if (Delta < 1.0)
						{
							UE_LOG(AHSP_Test, Verbose, TEXT("... [%d](%3.5f,%3.5f,%3.5f)"), Index1, Sample1.X, Sample1.Y, Sample1.Z);
							UE_LOG(AHSP_Test, Verbose, TEXT("... [%d](%3.5f,%3.5f,%3.5f)"), Index2, Sample2.X, Sample2.Y, Sample2.Z);
							UE_LOG(AHSP_Test, Verbose, TEXT("... ... %3.5f"), Delta);
							EXPECT_TRUE(false);
						}
					}
				}
			}
		}
	}
	template void SpatialHashing<float>();


}