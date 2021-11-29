// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Math/Matrix.isph"
#include "Math/Transform.isph"
#include "Chaos/Matrix33.isph"

#define MaxTotalInfluences 12

struct FClothVertBoneData
{
	int32 NumInfluences;
	uint16 BoneIndices[MaxTotalInfluences];
	float BoneWeights[MaxTotalInfluences];
};

static inline void AddInfluence(uniform FVector& OutPosition, uniform FVector& OutNormal, const uniform FVector& RefParticle, const uniform FVector& RefNormal, const uniform FMatrix *uniform BoneMatrix, const uniform float Weight)
{
	const uniform FVector4 M0M1M2 = *((uniform FVector4 *uniform)&BoneMatrix->M[0]);

	const uniform FVector4 VTempXP = RefParticle.V[0] * M0M1M2;
	const uniform FVector4 VTempXN = RefNormal.V[0] * M0M1M2;

	const uniform FVector4 M4M5M6 = *((uniform FVector4 *uniform)&BoneMatrix->M[4]);

	const uniform FVector4 VTempYP = RefParticle.V[1] * M4M5M6 + VTempXP;
	const uniform FVector4 VTempYN = RefNormal.V[1] * M4M5M6 + VTempXN;

	const uniform FVector4 M8M9M10 = *((uniform FVector4 *uniform)&BoneMatrix->M[8]);

	const uniform FVector4 VTempZP = RefParticle.V[2] * M8M9M10 + VTempYP;
	const uniform FVector4 Norm = RefNormal.V[2] * M8M9M10 + VTempYN;

	const uniform FVector4 VTempWP = *((uniform FVector4 *uniform)&BoneMatrix->M[12]);
	const uniform FVector4 Pos = VTempZP + VTempWP;

	OutPosition = OutPosition + SetVector(Pos.V[0], Pos.V[1], Pos.V[2]) * Weight;
	OutNormal = OutNormal + SetVector(Norm.V[0], Norm.V[1], Norm.V[2]) * Weight;
}

export void SkinPhysicsMesh(uniform FVector OutPositions[],
							uniform FVector OutNormals[],
							const uniform FVector Vertices[],
							const uniform FVector Normals[],
							const uniform FClothVertBoneData BoneData[],
							const uniform int32 BoneMap[],
							const uniform FMatrix BoneMatrices[],
							const uniform FTransform &ComponentToLocalSpace,
							const uniform int32 NumPoints)
{
	for(uniform int i = 0; i < NumPoints; ++i)
	{
		const uniform FVector RefParticle = Vertices[i];
		const uniform FVector RefNormal = Normals[i];

		uniform FVector OutPosition = OutPositions[i];
		uniform FVector OutNormal = OutNormals[i];

		OutPosition = SetVector(0.0f, 0.0f, 0.0f);
		OutNormal = SetVector(0.0f, 0.0f, 0.0f);

		const uniform int NumInfluences = BoneData[i].NumInfluences;

		for(uniform int j = 0; j < NumInfluences; ++j)
		{
			const uniform int BoneMatrixIndex = BoneMap[BoneData[i].BoneIndices[j]];
			const uniform float Weight = BoneData[i].BoneWeights[j];

			const uniform FMatrix *uniform BoneMatrix = &BoneMatrices[BoneMatrixIndex];
			AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrix, Weight);
		}

		OutPositions[i] = TransformPosition(ComponentToLocalSpace, OutPosition);
		OutNormals[i] = VectorNormalize(TransformVector(ComponentToLocalSpace, OutNormal));
	}
}
