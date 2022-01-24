// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Math/Transform.isph"

export void ApplyPreSimulationTransforms(uniform FVector ParticlesP[],
										uniform FVector ParticlesV[],
										uniform FVector ParticlesX[],
										uniform FVector OldAnimationPositions[],
										const uniform uint32 ParticleGroupIds[],
										const uniform FTransform PreSimulationTransforms[],
										const uniform FVector &DeltaLocalSpaceLocation,
										const uniform int32 Offset,
										const uniform int32 Range)
{
	foreach(i = Offset ... Range)
	{
		const varying uint32 ParticleGroupId = ParticleGroupIds[i];
		#pragma ignore warning(perf)
		const varying FTransform GroupSpaceTransform = PreSimulationTransforms[ParticleGroupId];

		const uniform int32 Index = extract(i, 0);

		const varying FVector ParticleX = VectorLoad(&ParticlesX[Index]);
		const varying FVector PX = TransformPosition(GroupSpaceTransform, ParticleX) - DeltaLocalSpaceLocation;
		VectorStore(&ParticlesP[Index], PX);
		VectorStore(&ParticlesX[Index], PX);

		const varying FVector ParticleV = VectorLoad(&ParticlesV[Index]);
		const varying FVector V = TransformVector(GroupSpaceTransform, ParticleV);
		VectorStore(&ParticlesV[Index], V);

		const varying FVector OldAnimationPosition = VectorLoad(&OldAnimationPositions[Index]);
		const varying FVector AP = TransformPosition(GroupSpaceTransform, OldAnimationPosition) - DeltaLocalSpaceLocation;
		VectorStore(&OldAnimationPositions[Index], AP);
	}
}

export void CalculateBounds(uniform FVector &MMin,
							uniform FVector &MMax,
							const uniform FVector ParticlesX[],
							const uniform int32 Offset,
							const uniform int32 Range)
{
	varying FVector Min = MMin;
	varying FVector Max = MMax;

	foreach(i = Offset ... Range)
	{
		const uniform int32 Index = extract(i, 0);
		const varying FVector X = VectorLoad(&ParticlesX[Index]);

		Min = VectorMin(Min, X);
		Max = VectorMax(Max, X);
	}

	MMin = VectorReduceMin(Min);
	MMax = VectorReduceMax(Max);
}

export void CalculateSquaredRadius(uniform FReal &SquaredRadius,
									const uniform FVector &Center,
									const uniform FVector ParticlesX[],
									const uniform int32 Offset,
									const uniform int32 Range)
{
	varying FReal vSquaredRadius = SquaredRadius;

	foreach(i = Offset ... Range)
	{
		const uniform int32 Index = extract(i, 0);
		const varying FVector X = VectorLoad(&ParticlesX[Index]);

		vSquaredRadius = max(vSquaredRadius, VectorSizeSquared(X - Center));
	}

	SquaredRadius = reduce_max(vSquaredRadius);
}
