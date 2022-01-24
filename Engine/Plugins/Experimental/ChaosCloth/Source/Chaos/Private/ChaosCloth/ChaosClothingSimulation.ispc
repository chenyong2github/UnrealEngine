// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Math/Quat.isph"
#include "Math/Transform.isph"

export void GetClothingSimulationData(uniform FVector3f Positions[],
									uniform FVector3f Normals[],
									const uniform FVector4f &Rotation,
									const uniform FVector3f &Translation,
									const uniform FVector3f &Scale3D,
									const uniform FVector3f &LocalSpaceLocation,
									const uniform int NumPositions)
{
	const uniform FVector4f RotationInverse = QuatInverse(Rotation);
	const uniform FVector3f SafeReciprocal = TransformGetSafeScaleReciprocal(Scale3D, FLOAT_SMALL_NUMBER);

	foreach(i = 0 ... NumPositions)
	{
		const uniform int Index = extract(i, 0);
		const FVector3f Position = VectorLoad(&Positions[Index]);
		const FVector3f Normal = VectorLoad(&Normals[Index]);

		const FVector3f InputPositionVector = Position + LocalSpaceLocation;
		const FVector3f InputNormalVector = Normal * -FLOAT_ONE;

		FVector3f TranslatedPosition = InputPositionVector - Translation;

		const FVector3f VRPosition = VectorQuaternionRotateVector(RotationInverse, TranslatedPosition);
		const FVector3f VRNormal = VectorQuaternionRotateVector(RotationInverse, InputNormalVector);

		const FVector3f OutPosition = VRPosition * SafeReciprocal;
		const FVector3f OutNormal = VRNormal * SafeReciprocal;

		VectorStore(&Positions[Index], OutPosition);
		VectorStore(&Normals[Index], OutNormal);
	}
}
