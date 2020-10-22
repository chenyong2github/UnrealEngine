// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"

export void ApplySphericalConstraints(uniform FVector P[],
									const uniform FVector AnimationPositions[],
									const uniform float InvM[],
									const uniform float SphereRadii[],
									const uniform float SphereRadiiMultiplier,
									const uniform int32 ParticleOffset,
									const uniform int32 ParticleCount)
{
	static const float DeadZoneSquareRadius = SMALL_NUMBER; // We will not push the particle away in the dead zone

	foreach(Index = 0 ... ParticleCount)
	{
		const int32 ParticleIndex = ParticleOffset + Index;
		const float IM = InvM[ParticleIndex];

		if(all(IM == 0))
		{
			continue;
		}

		const uniform int32 Offset = extract(ParticleIndex, 0);
		
		const FVector Center = VectorLoad(&AnimationPositions[Offset]);
		const FVector Position = VectorLoad(&P[Offset]);

		const FVector CenterToParticle = Position - Center;
		const float DistanceSquared = VectorSizeSquared(CenterToParticle);
		const float Distance = sqrt(DistanceSquared);

		const float Radius = SphereRadii[Index] * SphereRadiiMultiplier;
		const FVector FinalPosition = ((Radius / Distance) * CenterToParticle) + Center;

		if (IM != 0 && DistanceSquared > ((Radius * Radius) + DeadZoneSquareRadius))
		{
			VectorStore(&P[Offset], FinalPosition);
		}
	}
}

export void ApplyLegacySphericalBackstopConstraints(uniform FVector P[],
													const uniform FVector AnimationPositions[],
													const uniform FVector AnimationNormals[],
													const uniform float InvM[],
													const uniform float SphereOffsetDistances[],
													const uniform float SphereRadii[],
													const uniform float SphereRadiiMultiplier,
													const uniform int32 ParticleOffset,
													const uniform int32 ParticleCount)
{
	static const float DeadZoneSquareRadius = SMALL_NUMBER; // We will not push the particle away in the dead zone

	foreach(Index = 0 ... ParticleCount)
	{
		const int32 ParticleIndex = ParticleOffset + Index;
		const float IM = InvM[ParticleIndex];

		if(all(IM == 0))
		{
			continue;
		}

		const FVector AnimationPosition = VectorLoad(&AnimationPositions[extract(Index, 0)]);
		const FVector AnimationNormal = VectorLoad(&AnimationNormals[extract(Index, 0)]);

		const float SphereOffsetDistance = SphereOffsetDistances[Index];
		const float Radius = SphereRadii[Index] * SphereRadiiMultiplier;

		const FVector Center = AnimationPosition - SphereOffsetDistance * AnimationNormal;  // Legacy version already includes the radius within the distance
		const FVector CenterToParticle = VectorLoad(&P[extract(ParticleIndex, 0)]) - Center;
		const float DistanceSquared = VectorSizeSquared(CenterToParticle);

		const FVector DistSquaredLessRadiusSquared = ((Radius / sqrt(DistanceSquared)) * CenterToParticle) + Center;
		const FVector DistSquaredLessDeadZoneRadius = AnimationPosition - (SphereOffsetDistance - Radius) * AnimationNormal;  // Legacy version already includes the radius to the distance

		FVector NewP = VectorSelect(DistanceSquared < Radius * Radius, DistSquaredLessRadiusSquared, ZeroVector);
		NewP = VectorSelect(DistanceSquared < DeadZoneSquareRadius, DistSquaredLessDeadZoneRadius, NewP);

		if (IM != 0 && NewP.V[0] != 0)
		{
			VectorStore(&P[extract(ParticleIndex, 0)], NewP);
		}
	}
}

export void ApplySphericalBackstopConstraints(uniform FVector P[],
												const uniform FVector AnimationPositions[],
												const uniform FVector AnimationNormals[],
												const uniform float InvM[],
												const uniform float SphereOffsetDistances[],
												const uniform float SphereRadii[],
												const uniform float SphereRadiiMultiplier,
												const uniform int32 ParticleOffset,
												const uniform int32 ParticleCount)
{
	static const float DeadZoneSquareRadius = SMALL_NUMBER; // We will not push the particle away in the dead zone

	foreach(Index = 0 ... ParticleCount)
	{
		const int32 ParticleIndex = ParticleOffset + Index;
		const float IM = InvM[ParticleIndex];

		if(all(IM == 0))
		{
			continue;
		}

		const FVector AnimationPosition = VectorLoad(&AnimationPositions[extract(Index, 0)]);
		const FVector AnimationNormal = VectorLoad(&AnimationNormals[extract(Index, 0)]);

		const float SphereOffsetDistance = SphereOffsetDistances[Index];
		const float Radius = SphereRadii[Index] * SphereRadiiMultiplier;

		const FVector Center = AnimationPosition - (Radius + SphereOffsetDistance) * AnimationNormal;  // Non legacy version adds radius to the distance
		const FVector CenterToParticle = VectorLoad(&P[extract(ParticleIndex, 0)]) - Center;
		const float DistanceSquared = VectorSizeSquared(CenterToParticle);

		const FVector DistSquaredLessRadiusSquared = ((Radius / sqrt(DistanceSquared)) * CenterToParticle) + Center;
		const FVector DistSquaredLessDeadZoneRadius = AnimationPosition - SphereOffsetDistance * AnimationNormal;  // Non legacy version adds radius to the distance

		FVector NewP = VectorSelect(DistanceSquared < Radius * Radius, DistSquaredLessRadiusSquared, ZeroVector);
		NewP = VectorSelect(DistanceSquared < DeadZoneSquareRadius, DistSquaredLessDeadZoneRadius, NewP);

		if (IM != 0 && NewP.V[0] != 0)
		{
			VectorStore(&P[extract(ParticleIndex, 0)], NewP);
		}
	}
}
