// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Math/Quat.isph"
#include "Math/Transform.isph"

export void GetClothingSimulationData(uniform FVector Positions[],
									uniform FVector Normals[],
									const uniform FTransform &OwnerTransform,
									const uniform FVector &LocalSpaceLocation,
									const uniform int NumPositions)
{
	const uniform FVector4 RotationInverse = QuatInverse(OwnerTransform.Rotation);
	const uniform FVector Translation = SetVector(OwnerTransform.Translation);
	const uniform FVector SafeReciprocal = SetVector(TransformGetSafeScaleReciprocal(OwnerTransform.Scale3D, SMALL_NUMBER));

	foreach(i = 0 ... NumPositions)
	{
		const uniform int Index = extract(i, 0);
		const FVector Position = VectorLoad(&Positions[Index]);
		const FVector Normal = VectorLoad(&Normals[Index]);

		const FVector InputPositionVector = Position + LocalSpaceLocation;
		const FVector InputNormalVector = Normal * -1.0f;

		FVector TranslatedPosition = InputPositionVector - Translation;

		const FVector VRPosition = VectorQuaternionRotateVector(RotationInverse, TranslatedPosition);
		const FVector VRNormal = VectorQuaternionRotateVector(RotationInverse, InputNormalVector);

		const FVector OutPosition = VRPosition * SafeReciprocal;
		const FVector OutNormal = VRNormal * SafeReciprocal;

		VectorStore(&Positions[Index], OutPosition);
		VectorStore(&Normals[Index], OutNormal);
	}
}
