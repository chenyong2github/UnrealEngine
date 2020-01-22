// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Math/Matrix.isph"
#include "Math/Quat.isph"
#include "Math/Transform.isph"

#define INDEX_NONE -1

static const uniform float DELTA = 0.00001f;
static const uniform float ZERO_ANIMWEIGHT_THRESH = DELTA;
static const uniform struct FVector4 DefaultScale = { {1.f, 1.f, 1.f, 0.f} };

struct FPerBoneBlendWeight
{
	int SourceIndex;
	float BlendWeight;
};

export void BlendTransformOverwrite(const uniform FTransform SourcePose[],
									uniform FTransform ResultPose[],
									const uniform float BlendWeight,
									const uniform int NumBones)
{
	for(uniform int BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
	{
		ResultPose[BoneIndex] = SourcePose[BoneIndex] * BlendWeight;
	}
}

export void BlendTransformAccumulate(const uniform FTransform SourcePose[],
									uniform FTransform ResultPose[],
									const uniform float BlendWeight,
									const uniform int NumBones)
{
	for(uniform int BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
	{
		const uniform FTransform Source = SourcePose[BoneIndex];
		uniform FTransform Dest = ResultPose[BoneIndex];

		const uniform FVector4 BlendedRotation = Source.Rotation * BlendWeight;
		Dest.Rotation = VectorAccumulateQuaternionShortestPath(Dest.Rotation, BlendedRotation);

		Dest.Translation = VectorMultiplyAdd(Source.Translation, BlendWeight, Dest.Translation);
		Dest.Scale3D = VectorMultiplyAdd(Source.Scale3D, BlendWeight, Dest.Scale3D);

		ResultPose[BoneIndex] = Dest;
	}
}

export void ConvertPoseToMeshRotation(uniform FTransform LocalPoses[],
									const uniform int ParentBones[],
									const uniform int NumBones)
{
	for(uniform int BoneIndex = 1; BoneIndex < NumBones; ++BoneIndex)
	{
		const uniform int ParentIndex = ParentBones[BoneIndex];
		const uniform FVector4 LocalPoseRotation = LocalPoses[BoneIndex].Rotation;
		const uniform FVector4 LocalParentRotation = LocalPoses[ParentIndex].Rotation;

		const uniform FVector4 MeshSpaceRotation = VectorQuaternionMultiply2(LocalParentRotation, LocalPoseRotation);
		LocalPoses[BoneIndex].Rotation = MeshSpaceRotation;
	}
}

export void ConvertMeshRotationPoseToLocalSpace(uniform FTransform Poses[],
												const uniform int ParentBones[],
												const uniform int NumBones)
{
	for(uniform int BoneIndex = NumBones - 1; BoneIndex > 0; --BoneIndex)
	{
		const uniform int ParentIndex = ParentBones[BoneIndex];
		const uniform FVector4 PoseRotation = Poses[BoneIndex].Rotation;
		const uniform FVector4 ParentRotation = Poses[ParentIndex].Rotation;

		const uniform FVector4 LocalSpaceRotation = VectorQuaternionMultiply2(QuatInverse(ParentRotation), PoseRotation);
		Poses[BoneIndex].Rotation = LocalSpaceRotation;
	}
}

export void AccumulateWithAdditiveScale(uniform FTransform BasePose[],
										const uniform FTransform AdditivePose[],
										const uniform float BlendWeight,
										const uniform int NumBones)
{
	for(uniform int BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
	{
		const uniform FTransform Atom = AdditivePose[BoneIndex];
		uniform FTransform Base = BasePose[BoneIndex];

		// SourceAtom = Atom * BlendWeight;
		const uniform FVector4 BlendedRotation = Atom.Rotation * BlendWeight;

		// Translation += SourceAtom.Translation;
		// Scale *= SourceAtom.Scale;
		const uniform FVector4 TranslationResult = Base.Translation + (Atom.Translation * BlendWeight);
		const uniform FVector4 Scale3DResult = Base.Scale3D * ((Atom.Scale3D * BlendWeight) + DefaultScale);

		const uniform float SquareRotationW = BlendedRotation.V[3] * BlendedRotation.V[3];

		// Add ref pose relative animation to base animation, only if rotation is significant.
		if(SquareRotationW < 1.f - DELTA * DELTA)
		{
			// Rotation = SourceAtom.Rotation * Rotation;
			BasePose[BoneIndex].Rotation = VectorQuaternionMultiply2(BlendedRotation, Base.Rotation);
		}

		BasePose[BoneIndex].Translation = TranslationResult;
		BasePose[BoneIndex].Scale3D = Scale3DResult;
	}
}

static inline uniform bool IsRelevant(const uniform float InWeight)
{
	return (InWeight > ZERO_ANIMWEIGHT_THRESH);
}

static inline uniform bool IsFullWeight(const uniform float InWeight)
{
	return (InWeight >= (1.f - ZERO_ANIMWEIGHT_THRESH));
}

// Set this Transform to the weighted blend of it and the supplied Transform.
static inline void BlendWith(uniform FTransform &Atom, const uniform FTransform& OtherAtom, const uniform float BlendWeight)
{
	if( BlendWeight > ZERO_ANIMWEIGHT_THRESH )
	{
		if( BlendWeight >= 1.f - ZERO_ANIMWEIGHT_THRESH )
		{
			// if blend is all the way for child2, then just copy its bone atoms
			Atom = OtherAtom;
		}
		else 
		{
			const uniform FVector4 VRotation = VectorLerpQuat(Atom.Rotation, OtherAtom.Rotation, BlendWeight);
			
			// ..and renormalize
			Atom.Rotation = VectorNormalizeQuaternion(VRotation);
			
			// Simple linear interpolation for translation and scale.
			Atom.Translation = VectorLerp(Atom.Translation, OtherAtom.Translation, BlendWeight);
			
			Atom.Scale3D = VectorLerp(Atom.Scale3D, OtherAtom.Scale3D, BlendWeight);
		}
	}
}

extern "C" unmasked const uniform unsigned int8 *uniform GetTransformFromArray(const uniform unsigned int8 *uniform BlendPoseBase, const uniform int PoseIndex, const uniform int BoneIndex);

// blend poses with both mesh space rotation and scaling (we assume uniform scale)
export void BlendPosesPerBoneFilterScaleRotation(uniform FTransform OutPose[],
												const uniform FTransform BasePose[],
												const uniform unsigned int8 *uniform BlendPoseBase,
												uniform FVector4 SourceRotations[],
												uniform FVector SourceScales[],
												uniform FVector4 TargetRotations[],
												uniform FVector TargetScales[],
												uniform FVector4 BlendRotations[],
												uniform FVector BlendScales[],
												uniform float MaxPoseWeights[],
												const uniform FPerBoneBlendWeight BoneBlendWeights[],
												const uniform int ParentBoneArray[],
												const uniform int NumBones)
{
	for(uniform int BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
	{
		const uniform int PoseIndex = BoneBlendWeights[BoneIndex].SourceIndex;
		const uniform int ParentIndex = ParentBoneArray[BoneIndex];

		const uniform FTransform BaseAtom = BasePose[BoneIndex];
		const uniform FTransform TargetAtom = *((const uniform FTransform *uniform)GetTransformFromArray(BlendPoseBase, PoseIndex, BoneIndex));

		uniform FVector4 SourceRotation, TargetRotation;
		uniform FVector SourceScale, TargetScale;
	
		if (ParentIndex != INDEX_NONE)
		{
			SourceRotation = VectorQuaternionMultiply2(SourceRotations[ParentIndex], BaseAtom.Rotation);
			TargetRotation = VectorQuaternionMultiply2(TargetRotations[ParentIndex], TargetAtom.Rotation);
			SourceScale = SourceScales[ParentIndex] * SetVector(BaseAtom.Scale3D);
			TargetScale = TargetScales[ParentIndex] * SetVector(TargetAtom.Scale3D);
		}
		else
		{
			SourceRotation = VectorQuaternionMultiply2(Float0001, BaseAtom.Rotation);
			TargetRotation = VectorQuaternionMultiply2(Float0001, TargetAtom.Rotation);
			SourceScale = OneVector * SetVector(BaseAtom.Scale3D);
			TargetScale = OneVector * SetVector(TargetAtom.Scale3D);
		}

		const uniform float BlendWeight = clamp(BoneBlendWeights[BoneIndex].BlendWeight, 0.f, 1.f);
		MaxPoseWeights[PoseIndex] = max(MaxPoseWeights[PoseIndex], BlendWeight);

		uniform FTransform BlendAtom;
		uniform FVector4 BlendRotation;
		uniform FVector BlendScale;
	
		if (!IsRelevant(BlendWeight))
		{
			BlendAtom = BaseAtom;
			BlendRotation = SourceRotation;
			BlendScale = SourceScale;
		}
		else if (IsFullWeight(BlendWeight))
		{
			BlendAtom = TargetAtom;
			BlendRotation = TargetRotation;
			BlendScale = TargetScale;
		}
		else
		{
			BlendAtom = BaseAtom;
			BlendWith(BlendAtom, TargetAtom, BlendWeight);

			BlendRotation = VectorNormalizeQuaternion(QuatFastLerp(SourceRotation, TargetRotation, BlendWeight));

			BlendScale = VectorLerp(SourceScale, TargetScale, BlendWeight);
		}

		SourceRotations[BoneIndex] = SourceRotation;
		TargetRotations[BoneIndex] = TargetRotation;
		SourceScales[BoneIndex] = SourceScale;
		TargetScales[BoneIndex] = TargetScale;
		
		if (ParentIndex != INDEX_NONE)
		{
			// local -> mesh -> local transformations can cause loss of precision for long bone chains, we have to normalize rotation there.
			const uniform FVector4 LocalBlendQuat = VectorQuaternionMultiply2(QuatInverse(BlendRotations[ParentIndex]), BlendRotation);
			BlendAtom.Rotation = VectorNormalizeQuaternion(LocalBlendQuat);

			const uniform FVector ParentScaleInv = TransformGetSafeScaleReciprocal(BlendScales[ParentIndex], SMALL_NUMBER);
			const uniform FVector LocalBlendScale = ParentScaleInv * BlendScale;
			BlendAtom.Scale3D = SetVector4(LocalBlendScale.V[0], LocalBlendScale.V[1], LocalBlendScale.V[2], 0.0f);
		}

		BlendRotations[BoneIndex] = BlendRotation;
		BlendScales[BoneIndex] = BlendScale;

		OutPose[BoneIndex] = BlendAtom;
	}
}
