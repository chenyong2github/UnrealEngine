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

// Helper function since we only need 3x3
static inline FMatrix LoadMatrixHelper(const uniform FMatrix *varying M)
{
	#pragma ignore warning(perf)
	const varying float M0 = M->M[0];
	#pragma ignore warning(perf)
	const varying float M1 = M->M[1];
	#pragma ignore warning(perf)
	const varying float M2 = M->M[2];
	#pragma ignore warning(perf)
	const varying float M4 = M->M[4];
	#pragma ignore warning(perf)
	const varying float M5 = M->M[5];
	#pragma ignore warning(perf)
	const varying float M6 = M->M[6];
	#pragma ignore warning(perf)
	const varying float M8 = M->M[8];
	#pragma ignore warning(perf)
	const varying float M9 = M->M[9];
	#pragma ignore warning(perf)
	const varying float M10 = M->M[10];
	#pragma ignore warning(perf)
	const varying float M12 = M->M[12];
	#pragma ignore warning(perf)
	const varying float M13 = M->M[13];
	#pragma ignore warning(perf)
	const varying float M14 = M->M[14];

	return SetMatrix(
		SetVector4(M0 , M1 , M2 , 0.f),
		SetVector4(M4 , M5 , M6 , 0.f),
		SetVector4(M8 , M9 , M10, 0.f),
		SetVector4(M12, M13, M14, 1.f));
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
	foreach(i = 0 ... NumPoints)
	{
		const uniform int VertIndex = extract(i, 0);
		const FVector RefParticle = VectorLoad(&Vertices[VertIndex]);
		const FVector RefNormal = VectorLoad(&Normals[VertIndex]);

		FVector OutPosition = VectorLoad(&OutPositions[VertIndex]);
		FVector OutNormal = VectorLoad(&OutNormals[VertIndex]);

		#pragma ignore warning(perf)
		const varying int NumInfluences = BoneData[i].NumInfluences;

		for(uniform int j = 0; j < NumInfluences; ++j)
		{
			#pragma ignore warning(perf)
			const varying int BoneMatrixIndex = BoneMap[BoneData[i].BoneIndices[j]];
			#pragma ignore warning(perf)
			const varying float Weight = BoneData[i].BoneWeights[j];

			const varying FMatrix BoneMatrix = LoadMatrixHelper(&BoneMatrices[BoneMatrixIndex]);
			const FVector Pos = MatrixTransformPosition(RefParticle, BoneMatrix);
			const FVector Norm = MatrixTransformVector(RefNormal, BoneMatrix);

			// AddInfluence
			OutPosition = OutPosition + Pos * Weight;
			OutNormal = OutNormal + Norm * Weight;
		}

		OutPosition = TransformPosition(ComponentToLocalSpace, OutPosition);
		OutNormal = VectorNormalize(TransformVector(ComponentToLocalSpace, OutNormal));

		VectorStore(&OutPositions[VertIndex], OutPosition);
		VectorStore(&OutNormals[VertIndex], OutNormal);
	}
}
