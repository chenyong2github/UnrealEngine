// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ContainersFwd.h"
#include "UObject/WeakObjectPtr.h"
#include "AnimationDataRegistry.h"
#include <algorithm>
#include "Animation/AnimTypes.h"
#include "ReferenceSkeleton.h"
#include "BoneIndices.h"
#include "AnimationReferencePose.generated.h"

class USkeletalMesh;
class USkeleton;

namespace UE::AnimNext
{

#define ANIM_ENABLE_POINTER_ITERATION 1

using FAnimTranslation = FVector;
using FAnimRotation = FQuat;
using FAnimScale = FVector;
using FAnimTransform = FTransform;

extern const FAnimTransform ANIMNEXTINTERFACE_API TransformAdditiveIdentity;

//****************************************************************************

template <typename AllocatorType>
struct TAnimTransformArrayAoS
{
	TArray<FAnimTransform, AllocatorType> Transforms;

	// Default empty (no memory) Transform
	TAnimTransformArrayAoS() = default;

	// Create a TransformArray of N elements
	explicit TAnimTransformArrayAoS(const int NumTransforms)
	{
		SetNum(NumTransforms);
	}

	~TAnimTransformArrayAoS() = default;
	TAnimTransformArrayAoS(const TAnimTransformArrayAoS& Other) = default;
	TAnimTransformArrayAoS& operator= (const TAnimTransformArrayAoS& Other) = default;
	TAnimTransformArrayAoS(TAnimTransformArrayAoS&& Other) = default;
	TAnimTransformArrayAoS& operator= (TAnimTransformArrayAoS&& Other) = default;

	inline void Reset(int NumTransforms)
	{
		Transforms.Reset(NumTransforms);
	}

	void SetNum(int32 NumTransforms, bool bAllowShrinking = true)
	{
		constexpr int32 TransformSize = sizeof(FAnimTranslation) + sizeof(FAnimRotation) + sizeof(FAnimScale);
		Transforms.SetNum(NumTransforms, bAllowShrinking);
	}

	inline void SetIdentity(bool bAdditiveIdentity = false)
	{
		std::fill(Transforms.begin(), Transforms.end(), bAdditiveIdentity ? TransformAdditiveIdentity : FTransform::Identity);
	}

	inline void CopyTransforms(const TAnimTransformArrayAoS& Other, int32 Index, int32 NumTransforms)
	{
		check(Index < Num() && Index < Other.Num());
		check(Index + (NumTransforms - 1) < Num() && Index + (NumTransforms - 1) < Other.Num());
		
		std::copy(Other.Transforms.GetData() + Index, Other.Transforms.GetData() + (Index + NumTransforms), Transforms.GetData() + Index);
	}

	inline int32 Num() const
	{
		return Transforms.Num();
	}

	inline TArray<FTransform>& GetTransforms()
	{
		return Transforms;
	}
	inline const TArray<FTransform>& GetTransforms() const
	{
		return Transforms;
	}

	inline FAnimTransform& operator[](int32 Index)
	{
		return Transforms[Index];
	}

	inline const FAnimTransform& operator[](int32 Index) const
	{
		return Transforms[Index];
	}

	/** Set this transform array to the weighted blend of the supplied two transforms. */
	void Blend(const TAnimTransformArrayAoS& AtomArray1, const TAnimTransformArrayAoS& AtomArray2, float BlendWeight)
	{
		if (FMath::Abs(BlendWeight) <= ZERO_ANIMWEIGHT_THRESH)
		{
			// if blend is all the way for child1, then just copy its bone atoms
			//(*this) = Atom1;
			CopyTransforms(AtomArray1, 0, Num());
		}
		else if (FMath::Abs(BlendWeight - 1.0f) <= ZERO_ANIMWEIGHT_THRESH)
		{
			// if blend is all the way for child2, then just copy its bone atoms
			//(*this) = Atom2;
			CopyTransforms(AtomArray2, 0, Num());
		}
		else
		{
			const int32 NumTransforms = Num();

			// Right now we only support same size blend
			check(AtomArray1.Num() == NumTransforms);
			check(AtomArray2.Num() == NumTransforms);

#if ANIM_ENABLE_POINTER_ITERATION
			// This version is faster, but without the range checks the TArray has on operator[]
			const FAnimTransform* RESTRICT Transforms1 = AtomArray1.Transforms.GetData();
			const FAnimTransform* RESTRICT Transforms1End = AtomArray1.Transforms.GetData() + NumTransforms;
			const FAnimTransform* RESTRICT Transforms2 = AtomArray2.Transforms.GetData();
			FAnimTransform* RESTRICT TransformsResult = Transforms.GetData();

			for (; Transforms1 != Transforms1End; ++Transforms1, ++Transforms2, ++TransformsResult)
			{
				//#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
				//				// Check that all bone atoms coming from animation are normalized
				//				check(Transforms1[i].IsRotationNormalized());
				//				check(Transforms2[i].IsRotationNormalized());
				//#endif
				TransformsResult->Blend(*Transforms1, *Transforms2, BlendWeight);
			}
#else
			// This version is a bit slower, but has range checks on the array
			const TArray<FAnimTransform, AllocatorType>& Transforms1 = AtomArray1.Transforms;
			const TArray<FAnimTransform, AllocatorType>& Transforms2 = AtomArray2.Transforms;
			for (int32 i = 0; i < NumTransforms; ++i)
			{
				//#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
				//				// Check that all bone atoms coming from animation are normalized
				//				check(Transforms1[i].IsRotationNormalized());
				//				check(Transforms2[i].IsRotationNormalized());
				//#endif
				Transforms[i].Blend(Transforms1[i], Transforms2[i], BlendWeight);
			}
#endif // ANIM_ENABLE_POINTER_ITERATION

			DiagnosticCheckNaN_All();
		}
	}

	bool ContainsNaN() const
	{
		for (const auto& Transform : Transforms)
		{
			if (Transform.GetRotation().ContainsNaN())
			{
				return true;
			}
			if (Transform.GetTranslation().ContainsNaN())
			{
				return true;
			}
			if (Transform.GetScale3D().ContainsNaN())
			{
				return true;
			}
		}

		return false;
	}

	bool IsValid() const
	{
		if (ContainsNaN())
		{
			return false;
		}

		for (const auto& Transform : Transforms)
		{
			if (Transform.GetRotation().IsNormalized() == false)
			{
				return false;
			}
		}

		return true;
	}

private:
#if ENABLE_NAN_DIAGNOSTIC
	FORCEINLINE static void DiagnosticCheckNaN_Rotate(const FQuat& Rotation)
	{
		if (Rotation.ContainsNaN())
		{
			logOrEnsureNanError(TEXT("TAnimTransformArraySoA Rotation contains NaN"));
		}
	}

	FORCEINLINE static void DiagnosticCheckNaN_Scale3D(const FVector& Scale3D)
	{
		if (Scale3D.ContainsNaN())
		{
			logOrEnsureNanError(TEXT("TAnimTransformArraySoA Scale3D contains NaN"));
		}
	}

	FORCEINLINE static void DiagnosticCheckNaN_Translate(const FVector& Translation)
	{
		if (Translation.ContainsNaN())
		{
			logOrEnsureNanError(TEXT("TAnimTransformArraySoA Translation contains NaN"));
		}
	}

	FORCEINLINE void DiagnosticCheckNaN_All() const
	{
		for (const auto& Transform : Transforms)
		{
			DiagnosticCheckNaN_Rotate(Transform.GetRotation());
			DiagnosticCheckNaN_Translate(Transform.GetTranslation());
			DiagnosticCheckNaN_Scale3D(Transform.GetScale3D());
		}
	}

	FORCEINLINE void DiagnosticCheck_IsValid() const
	{
		DiagnosticCheckNaN_All();
		if (!IsValid())
		{
			logOrEnsureNanError(TEXT("TAnimTransformArraySoA is not valid"));
		}
	}

#else
	FORCEINLINE static void DiagnosticCheckNaN_Translate(const FVector& Translation) {}
	FORCEINLINE static void DiagnosticCheckNaN_Rotate(const FQuat& Rotation) {}
	FORCEINLINE static void DiagnosticCheckNaN_Scale3D(const FVector& Scale3D) {}
	FORCEINLINE void DiagnosticCheckNaN_All() const {}
	FORCEINLINE void DiagnosticCheck_IsValid() const {}
#endif
};

/**
* Transform Array using ArrayOfStructs model
*/
using FAnimTransformArrayAoSHeap = TAnimTransformArrayAoS<FDefaultAllocator>;
using FAnimTransformArrayAoSStack = TAnimTransformArrayAoS<FAnimStackAllocator>;


// This enables using a SoA array with operator[]
struct FTransformSoAAdapter
{
	FQuat& Rotation;
	FVector& Translation;
	FVector& Scale3D;

	FTransformSoAAdapter(FQuat& InRotation, FVector& InTranslation, FVector& InScale3D)
		: Rotation(InRotation)
		, Translation(InTranslation)
		, Scale3D(InScale3D)
	{
	}

	FORCEINLINE FQuat& GetRotation() const
	{
		return Rotation;
	}

	FORCEINLINE void SetRotation(const FQuat& InRotation)
	{
		Rotation = InRotation;
	}

	FORCEINLINE FVector& GetTranslation() const
	{
		return Translation;
	}

	FORCEINLINE void SetTranslation(const FVector& InTranslation)
	{
		Translation = InTranslation;
	}

	FORCEINLINE FVector& GetScale3D() const
	{
		return Scale3D;
	}

	FORCEINLINE void SetScale3D(const FVector& InScale3D)
	{
		Scale3D = InScale3D;
	}

	FORCEINLINE operator FTransform()
	{
		return FTransform(Rotation, Translation, Scale3D);
	}

	FORCEINLINE operator FTransform() const
	{
		return FTransform(Rotation, Translation, Scale3D);
	}

	FORCEINLINE void operator= (const FTransform& Transform)
	{
		Rotation = Transform.GetRotation();
		Translation = Transform.GetTranslation();
		Scale3D = Transform.GetScale3D();
	}

	FORCEINLINE void ScaleTranslation(const FVector::FReal& Scale)
	{
		Translation *= Scale;
		//DiagnosticCheckNaN_Translate();
	}

	FORCEINLINE void NormalizeRotation()
	{
		Rotation.Normalize();
		//DiagnosticCheckNaN_Rotate();
	}
};

/**
* Transform Array Test using StructOfArrays model
*/
template <typename AllocatorType>
struct TAnimTransformArraySoA
{
	TArrayView<FAnimTranslation> Translations;
	TArrayView<FAnimRotation> Rotations;
	TArrayView<FAnimScale> Scales3D;

	// Default empty (no memory) Transform
	TAnimTransformArraySoA()
	{
		UpdateViews(nullptr, 0);
	}

	// Create a TransformArray of N elements. Optionally initializes to identity (default)
	explicit TAnimTransformArraySoA(int NumTransforms, bool bSetIdentity = true, bool bAdditiveIdentity = false)
	{
		SetNum(NumTransforms);

		if (bSetIdentity)
		{
			SetIdentity(bAdditiveIdentity);
		}
	}

	~TAnimTransformArraySoA() = default;

	TAnimTransformArraySoA(const TAnimTransformArraySoA& Other)
	{
		AllocatedMemory = Other.AllocatedMemory;

		UpdateViews(AllocatedMemory.GetData(), Other.Num());
	}

	TAnimTransformArraySoA& operator= (const TAnimTransformArraySoA& Other)
	{
		TAnimTransformArraySoA Tmp(Other);

		Swap(*this, Tmp);
		return *this;
	}

	TAnimTransformArraySoA(TAnimTransformArraySoA&& Other)
	{
		Swap(*this, Other);
	}

	TAnimTransformArraySoA& operator= (TAnimTransformArraySoA&& Other)
	{
		Swap(*this, Other);
		return *this;
	}

	inline void Reset(int32 NumTransforms)
	{
		constexpr int32 TransformSize = sizeof(FAnimTranslation) + sizeof(FAnimRotation) + sizeof(FAnimScale);
		AllocatedMemory.Reset(NumTransforms * TransformSize);
	}

	void SetNum(int32 NumTransforms, bool bAllowShrinking = true)
	{
		constexpr int32 TransformSize = sizeof(FAnimTranslation) + sizeof(FAnimRotation) + sizeof(FAnimScale);
		AllocatedMemory.SetNum(NumTransforms * TransformSize, bAllowShrinking);

		UpdateViews(AllocatedMemory.GetData(), NumTransforms);
	}

	inline void SetIdentity(bool bAdditiveIdentity = false)
	{
		std::fill(Translations.begin(), Translations.end(), FVector::ZeroVector);
		std::fill(Rotations.begin(), Rotations.end(), FQuat::Identity);
		std::fill(Scales3D.begin(), Scales3D.end(), bAdditiveIdentity ? FVector::ZeroVector : FVector::OneVector);
	}

	inline void CopyTransforms(const TAnimTransformArraySoA& Other, int32 Index, int32 NumTransforms)
	{
		check(Index < Num() && Index < Other.Num());
		check(Index + (NumTransforms - 1) < Num() && Index + (NumTransforms - 1) < Other.Num());

		std::copy(Other.Translations.begin() + Index, Other.Translations.begin() + (Index + NumTransforms), Translations.begin());
		std::copy(Other.Rotations.begin() + Index, Other.Rotations.begin() + (Index + NumTransforms), Rotations.begin());
		std::copy(Other.Scales3D.begin() + Index, Other.Scales3D.begin() + (Index + NumTransforms), Scales3D.begin());
	}

	/** Set this transform array to the weighted blend of the supplied two transforms. */
	void Blend(const TAnimTransformArraySoA& AtomArray1, const TAnimTransformArraySoA& AtomArray2, float BlendWeight)
	{
		if (FMath::Abs(BlendWeight) <= ZERO_ANIMWEIGHT_THRESH)
		{
			// if blend is all the way for child1, then just copy its bone atoms
			//(*this) = Atom1;
			CopyTransforms(AtomArray1, 0, Num());
		}
		else if (FMath::Abs(BlendWeight - 1.0f) <= ZERO_ANIMWEIGHT_THRESH)
		{
			// if blend is all the way for child2, then just copy its bone atoms
			//(*this) = Atom2;
			CopyTransforms(AtomArray2, 0, Num());
		}
		else
		{
			const int32 NumTransforms = Num();

#if ANIM_ENABLE_POINTER_ITERATION
			using TransformVectorRegister = TVectorRegisterType<double>;

			// Right now we only support same size blend
			check(AtomArray1.Translations.Num() == Translations.Num() && AtomArray1.Rotations.Num() == Rotations.Num() && AtomArray1.Scales3D.Num() == Scales3D.Num());
			check(AtomArray2.Translations.Num() == Translations.Num() && AtomArray2.Rotations.Num() == Rotations.Num() && AtomArray2.Scales3D.Num() == Scales3D.Num());

			const FAnimTranslation* RESTRICT Translations1 = AtomArray1.Translations.GetData();
			const FAnimTranslation* RESTRICT Translations1End = AtomArray1.Translations.GetData() + NumTransforms;
			const FAnimTranslation* RESTRICT Translations2 = AtomArray2.Translations.GetData();
			FAnimTranslation* RESTRICT TranslationsResult = Translations.GetData();
			for (; Translations1 != Translations1End; ++Translations1, ++Translations2, ++TranslationsResult)
			{
				*TranslationsResult = FMath::Lerp(*Translations1, *Translations2, BlendWeight);
			}

			const FAnimRotation* RESTRICT Rotations1 = AtomArray1.Rotations.GetData();
			const FAnimRotation* RESTRICT Rotations1End = AtomArray1.Rotations.GetData() + NumTransforms;
			const FAnimRotation* RESTRICT Rotations2 = AtomArray2.Rotations.GetData();
			FAnimRotation* RESTRICT RotationsResult = Rotations.GetData();
			for (; Rotations1 != Rotations1End; ++Rotations1, ++Rotations2, ++RotationsResult)
			{
				//#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
				//				// Check that all bone atoms coming from animation are normalized
				//				check(Rotations1->IsNormalized());
				//				check(Rotations2->IsNormalized());
				//#endif

				*RotationsResult = FQuat::FastLerp(*Rotations1, *Rotations2, BlendWeight).GetNormalized();
			}

			const FAnimScale* RESTRICT Scales3D1 = AtomArray1.Scales3D.GetData();
			const FAnimScale* RESTRICT Scales3D1End = AtomArray1.Scales3D.GetData() + NumTransforms;
			const FAnimScale* RESTRICT Scales3D2 = AtomArray2.Scales3D.GetData();
			FAnimScale* RESTRICT Scales3DResult = Scales3D.GetData();
			for (; Scales3D1 != Scales3D1End; ++Scales3D1, ++Scales3D2, ++Scales3DResult)
			{
				*Scales3DResult = FMath::Lerp(*Scales3D1, *Scales3D2, BlendWeight);
			}
#else
			// This version is slower than AoS and a lot slower than the version using pointers, but has range checks on the TArrayView
			const TArrayView<FAnimTranslation>& Translations1 = AtomArray1.Translations;
			const TArrayView<FAnimTranslation>& Translations2 = AtomArray2.Translations;
			for (int32 i = 0; i < NumTransforms; ++i)
			{
				Translations[i] = FMath::Lerp(Translations1[i], Translations2[i], BlendWeight);
			}
			const TArrayView<FAnimRotation>& Rotations1 = AtomArray1.Rotations;
			const TArrayView<FAnimRotation>& Rotations2 = AtomArray2.Rotations;
			for (int32 i = 0; i < NumTransforms; ++i)
			{
				//#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_EDITORONLY_DATA
				//				// Check that all bone atoms coming from animation are normalized
				//				check(Transforms1[i].IsRotationNormalized());
				//				check(Transforms2[i].IsRotationNormalized());
				//#endif
				Rotations[i] = FQuat::FastLerp(Rotations1[i], Rotations2[i], BlendWeight).GetNormalized();
			}
			const TArrayView<FAnimScale>& Scales3D1 = AtomArray1.Scales3D;
			const TArrayView<FAnimScale>& Scales3D2 = AtomArray2.Scales3D;
			for (int32 i = 0; i < NumTransforms; ++i)
			{
				Scales3D[i] = FMath::Lerp(Scales3D1[i], Scales3D2[i], BlendWeight);
			}

#endif // ANIM_ENABLE_POINTER_ITERATION

			DiagnosticCheckNaN_All();
		}
	}

	FORCEINLINE int32 Num() const
	{
		return Translations.Num();
	}

	FTransformSoAAdapter operator[] (int Index)
	{
		return FTransformSoAAdapter(Rotations[Index], Translations[Index], Scales3D[Index]);
	}

	const FTransformSoAAdapter operator[] (int Index) const
	{
		return FTransformSoAAdapter(Rotations[Index], Translations[Index], Scales3D[Index]);
	}

	bool ContainsNaN() const
	{
		for (const auto& Rotation : Rotations)
		{
			if (Rotation.ContainsNaN())
			{
				return true;
			}
		}
		for (const auto& Translation : Translations)
		{
			if (Translation.ContainsNaN())
			{
				return true;
			}
		}
		for (const auto& Scale3D : Scales3D)
		{
			if (Scale3D.ContainsNaN())
			{
				return true;
			}
		}

		return false;
	}

	bool IsValid() const
	{
		if (ContainsNaN())
		{
			return false;
		}

		for (const auto& Rotation : Rotations)
		{
			if (Rotation.IsNormalized() == false)
			{
				return false;
			}
		}

		return true;
	}

private:
	TArray<uint8, AllocatorType> AllocatedMemory; // TODO : use the allocator directly

	void UpdateViews(uint8* Memory, int32 NumTransforms)
	{
		if (NumTransforms > 0)
		{
			Rotations = TArrayView<FAnimRotation>((FAnimRotation*)(Memory), NumTransforms);

			const int32 TranslationsOffset = sizeof(FAnimRotation) * NumTransforms;
			Translations = TArrayView<FAnimTranslation>((FAnimTranslation*)(Memory + TranslationsOffset), NumTransforms);

			const int32 Scales3DOffset = TranslationsOffset + sizeof(FAnimTranslation) * NumTransforms;
			Scales3D = TArrayView<FAnimScale>((FAnimScale*)(Memory + Scales3DOffset), NumTransforms);
		}
		else
		{
			Rotations = TArrayView<FAnimRotation>();
			Translations = TArrayView<FAnimTranslation>();
			Scales3D = TArrayView<FAnimScale>();
		}
	}

#if ENABLE_NAN_DIAGNOSTIC
	FORCEINLINE static void DiagnosticCheckNaN_Scale3D(const FVector& Scale3D)
	{
		if (Scale3D.ContainsNaN())
		{
			logOrEnsureNanError(TEXT("TAnimTransformArraySoA Scale3D contains NaN"));
		}
	}

	FORCEINLINE static void DiagnosticCheckNaN_Translate(const FVector& Translation)
	{
		if (Translation.ContainsNaN())
		{
			logOrEnsureNanError(TEXT("TAnimTransformArraySoA Translation contains NaN"));
		}
	}

	FORCEINLINE static void DiagnosticCheckNaN_Rotate(const FQuat& Rotation)
	{
		if (Rotation.ContainsNaN())
		{
			logOrEnsureNanError(TEXT("TAnimTransformArraySoA Rotation contains NaN"));
		}
	}

	FORCEINLINE void DiagnosticCheckNaN_All() const
	{
		for (const auto& Rotation : Rotations)
		{
			DiagnosticCheckNaN_Rotate(Rotation);
		}
		for (const auto& Translation : Translations)
		{
			DiagnosticCheckNaN_Translate(Translation);
		}
		for (const auto& Scale3D : Scales3D)
		{
			DiagnosticCheckNaN_Scale3D(Scale3D);
		}
	}

	FORCEINLINE void DiagnosticCheck_IsValid() const
	{
		DiagnosticCheckNaN_All();
		if (!IsValid())
		{
			logOrEnsureNanError(TEXT("TAnimTransformArraySoA is not valid"));
		}
	}

#else
	FORCEINLINE static void DiagnosticCheckNaN_Translate(const FVector& Translation) {}
	FORCEINLINE static void DiagnosticCheckNaN_Rotate(const FQuat& Rotation) {}
	FORCEINLINE static void DiagnosticCheckNaN_Scale3D(const FVector& Scale3D) {}
	FORCEINLINE void DiagnosticCheckNaN_All() const {}
	FORCEINLINE void DiagnosticCheck_IsValid() const {}
#endif

};

/**
* Transform Array using StructsOfArrays model
*/
using FAnimTransformArraySoAHeap = TAnimTransformArraySoA<FDefaultAllocator>;
using FAnimTransformArraySoAStack = TAnimTransformArraySoA<FAnimStackAllocator>;


//****************************************************************************

#define DEFAULT_SOA 1
#if DEFAULT_SOA
template <typename AllocatorType>
using TAnimTransformArray = TAnimTransformArraySoA<AllocatorType>;

using FAnimTransformArrayHeap = FAnimTransformArraySoAHeap;
using FAnimTransformArrayStack = FAnimTransformArraySoAStack;

using FAnimTransformArray = FAnimTransformArraySoAHeap;

#else
template <typename AllocatorType>
using TAnimTransformArray = TAnimTransformArrayAoS<AllocatorType>;

using FAnimTransformArrayHeap = FAnimTransformArrayAoSHeap;
using FAnimTransformArrayStack = FAnimTransformArrayAoSStack;

using FAnimTransformArray = FAnimTransformArrayAoSHeap;

#endif



//****************************************************************************

enum class EReferencePoseGenerationFlags : uint8
{
	None = 0,
	FastPath = 1 << 0
};

ENUM_CLASS_FLAGS(EReferencePoseGenerationFlags);

template <typename AllocatorType>
struct TAnimationReferencePose
{
	TAnimTransformArray<AllocatorType> ReferenceLocalTransforms;
	TArray<TArray<FBoneIndexType, AllocatorType>, AllocatorType> LODBoneIndexes;
	TArray<TArray<FBoneIndexType, AllocatorType>, AllocatorType> SkeletonToLODBoneIndexes;
	TArray<int32, AllocatorType> LODNumBones;

	TWeakObjectPtr<const USkeletalMesh> SkeletalMesh = nullptr;
	TWeakObjectPtr<const USkeleton> Skeleton = nullptr;
	EReferencePoseGenerationFlags GenerationFlags = EReferencePoseGenerationFlags::None;

	TAnimationReferencePose() = default;

	bool IsValid() const
	{
		return ReferenceLocalTransforms.Num() > 0;
	}

	int32 GetNumBonesForLOD(int32 LODLevel) const
	{
		const int32 NumLODS = LODNumBones.Num();

		return (LODLevel < NumLODS) ? LODNumBones[LODLevel] : (NumLODS > 0) ? LODNumBones[0] : 0;
	}

	bool IsFastPath() const
	{
		return (GenerationFlags & EReferencePoseGenerationFlags::FastPath) != EReferencePoseGenerationFlags::None;
	}

	void Initialize(const FReferenceSkeleton& RefSkeleton
		, const TArray<TArray<FBoneIndexType>>& InLODBoneIndexes
		, const TArray<TArray<FBoneIndexType>>& InSkeletonToLODBoneIndexes
		, const TArray<int32, AllocatorType>& InLODNumBones
		, bool bFastPath = false)
	{
		const int32 NumBonesLOD0 = (InLODNumBones.Num()) > 0 ? InLODNumBones[0] : 0;

		ReferenceLocalTransforms.SetNum(NumBonesLOD0);
		LODBoneIndexes = InLODBoneIndexes;
		SkeletonToLODBoneIndexes = InSkeletonToLODBoneIndexes;
		LODNumBones = InLODNumBones;
		
		const TArray<FAnimTransform>& RefBonePose = RefSkeleton.GetRefBonePose();
		const TArray<FBoneIndexType>& InBoneIndexes = InLODBoneIndexes[0]; // Fill the transforms with the LOD0 indexes

		for (int32 i = 0; i < NumBonesLOD0; ++i)
		{
			ReferenceLocalTransforms[i] = RefBonePose[InBoneIndexes[i]]; // TODO : For SoA this is un-optimal, as we are using a TransformAdapter. Evaluate using a specific SoA iterator
		}

		GenerationFlags = bFastPath ? EReferencePoseGenerationFlags::FastPath : EReferencePoseGenerationFlags::None;
	}

	const TArrayView<const FBoneIndexType> GetLODBoneIndexes(int32 LODLevel) const
	{
		TArrayView<const FBoneIndexType> ArrayView;

		if (LODLevel >= 0 && (IsFastPath() || LODLevel < LODBoneIndexes.Num()))
		{
			const int32 NumBonesForLOD = GetNumBonesForLOD(LODLevel);

			if (IsFastPath())
			{
				ArrayView = MakeArrayView(LODBoneIndexes[0].GetData(), NumBonesForLOD);
			}
			else
			{
				ArrayView = MakeArrayView(LODBoneIndexes[LODLevel].GetData(), NumBonesForLOD);
			}
		}

		return ArrayView;
	}

	const TArrayView<const FBoneIndexType> GetSkeletoonToLODBoneIndexes(int32 LODLevel) const
	{
		TArrayView<const FBoneIndexType> ArrayView;

		if (LODLevel >= 0 && (IsFastPath() || LODLevel < SkeletonToLODBoneIndexes.Num()))
		{
			const int32 NumBonesForLOD = GetNumBonesForLOD(LODLevel);

			if (IsFastPath())
			{
				return MakeArrayView(SkeletonToLODBoneIndexes[0].GetData(), NumBonesForLOD);
			}
			else
			{
				return MakeArrayView(SkeletonToLODBoneIndexes[LODLevel].GetData(), NumBonesForLOD);
			}
		}

		return ArrayView;
	}

	int32 GetSkeletonBoneIndexFromLODBoneIndex(int32 LODBoneIndex) const
	{
		const int32 NumBonesLOD0 = LODBoneIndexes[0].Num();
		check(LODBoneIndex < NumBonesLOD0);
		return LODBoneIndexes[0][LODBoneIndex];
	}

	int32 GetLODBoneIndexFromSkeletonBoneIndex(int32 SkeletionBoneIndex) const
	{
		const int32 NumBonesLOD0 = SkeletonToLODBoneIndexes[0].Num();
		check(SkeletionBoneIndex < NumBonesLOD0);
		return SkeletonToLODBoneIndexes[0][SkeletionBoneIndex];
	}

	FTransform GetRefPoseTransform(int32 LODBoneIndex) const
	{
		const int32 NumBonesLOD0 = LODBoneIndexes[0].Num();
		check(LODBoneIndex < NumBonesLOD0);

		return ReferenceLocalTransforms[LODBoneIndex];
	}

	const FQuat& GetRefPoseRotation(int32 LODBoneIndex) const
	{
		const int32 NumBonesLOD0 = LODBoneIndexes[0].Num();
		check(LODBoneIndex < NumBonesLOD0);

		return ReferenceLocalTransforms[LODBoneIndex].GetRotation();
	}

	const FVector& GetRefPoseTranslation(int32 LODBoneIndex) const
	{
		const int32 NumBonesLOD0 = LODBoneIndexes[0].Num();
		check(LODBoneIndex < NumBonesLOD0);

		return ReferenceLocalTransforms[LODBoneIndex].GetTranslation();
	}

	const FVector& GetRefPoseScale3D(int32 LODBoneIndex) const
	{
		const int32 NumBonesLOD0 = LODBoneIndexes[0].Num();
		check(LODBoneIndex < NumBonesLOD0);

		return ReferenceLocalTransforms[LODBoneIndex].GetScale3D();
	}

};

/**
* Hide the allocator usage
*/
using FAnimationReferencePose = TAnimationReferencePose<FDefaultAllocator>;


enum class EAnimationPoseFlags : uint8
{
	None				= 0,
	Additive			= 1 << 0,
	DisableRetargeting	= 1 << 1,
	UseRawData			= 1 << 2,
	UseSourceData		= 1 << 3
};

ENUM_CLASS_FLAGS(EAnimationPoseFlags);

template <typename AllocatorType>
struct TAnimationLODPose
{
	static constexpr int32 INVALID_LOD_LEVEL = -1;

	TAnimTransformArray<AllocatorType> LocalTransforms;
	const FAnimationReferencePose* RefPose = nullptr;
	int32 LODLevel = INVALID_LOD_LEVEL;
	EAnimationPoseFlags Flags = EAnimationPoseFlags::None;

	TAnimationLODPose() = default;

	TAnimationLODPose(const FAnimationReferencePose& InRefPose, int32 InLODLevel, bool bSetRefPose = true, bool bAdditive = false)
	{
		PrepareForLOD(InRefPose, InLODLevel, bSetRefPose, bAdditive);
	}

	void PrepareForLOD(const FAnimationReferencePose& InRefPose, int32 InLODLevel, bool bSetRefPose = true, bool bAdditive = false)
	{
		LODLevel = InLODLevel;
		RefPose = &InRefPose;

		const int32 NumTransforms = InRefPose.GetNumBonesForLOD(InLODLevel);
		LocalTransforms.SetNum(NumTransforms);
		Flags = (EAnimationPoseFlags)(bAdditive ? (Flags | EAnimationPoseFlags::Additive) : Flags & EAnimationPoseFlags::Additive);

		if (bSetRefPose && NumTransforms > 0)
		{
			SetRefPose(bAdditive);
		}
	}

	void SetRefPose(bool bAdditive = false)
	{
		const int32 NumTransforms = LocalTransforms.Num();

		if (NumTransforms > 0)
		{
			if (bAdditive)
			{
				SetIdentity(bAdditive);
			}
			else
			{
				check(RefPose != nullptr);
				LocalTransforms.CopyTransforms(RefPose->ReferenceLocalTransforms, 0, NumTransforms);
			}
		}

		Flags = (EAnimationPoseFlags)(bAdditive ? (Flags | EAnimationPoseFlags::Additive) : Flags & EAnimationPoseFlags::Additive);
	}

	const FAnimationReferencePose& GetRefPose() const
	{
		check(RefPose != nullptr);
		return *RefPose;
	}

	void SetIdentity(bool bAdditive = false)
	{
		LocalTransforms.SetIdentity(bAdditive);
	}

	int32 GetNumBones() const
	{
		return RefPose != nullptr ? RefPose->GetNumBonesForLOD(LODLevel) : 0;
	}

	const TArrayView<const FBoneIndexType> GetLODBoneIndexes() const
	{
		if (LODLevel != INVALID_LOD_LEVEL && RefPose != nullptr)
		{
			return RefPose->GetLODBoneIndexes(LODLevel);
		}
		else
		{
			return TArrayView<const FBoneIndexType>();
		}
	}

	const TArrayView<const FBoneIndexType> GetSkeletoonToLODBoneIndexes() const
	{
		if (LODLevel != INVALID_LOD_LEVEL && RefPose != nullptr)
		{
			return RefPose->GetSkeletoonToLODBoneIndexes(LODLevel);
		}
		else
		{
			return TArrayView<const FBoneIndexType>();
		}
	}

	const USkeleton* GetSkeletonAsset() const
	{
		return RefPose != nullptr ? RefPose->Skeleton.Get() : nullptr;
	}

	/** Disable Retargeting */
	void SetDisableRetargeting(bool bDisableRetargeting)
	{
		Flags = (EAnimationPoseFlags)(bDisableRetargeting ? (Flags | EAnimationPoseFlags::DisableRetargeting) : Flags & EAnimationPoseFlags::DisableRetargeting);
	}

	/** True if retargeting is disabled */
	bool GetDisableRetargeting() const
	{
		return (Flags & EAnimationPoseFlags::DisableRetargeting) != EAnimationPoseFlags::None;
	}

	/** Ignore compressed data and use RAW data instead, for debugging. */
	void SetUseRAWData(bool bUseRAWData)
	{
		Flags = (EAnimationPoseFlags)(bUseRAWData ? (Flags | EAnimationPoseFlags::UseRawData) : Flags & EAnimationPoseFlags::UseRawData);
	}

	/** True if we're requesting RAW data instead of compressed data. For debugging. */
	bool ShouldUseRawData() const
	{
		return (Flags & EAnimationPoseFlags::UseRawData) != EAnimationPoseFlags::None;
	}

	/** Use Source data instead.*/
	void SetUseSourceData(bool bUseSourceData)
	{
		Flags = (EAnimationPoseFlags)(bUseSourceData ? (Flags | EAnimationPoseFlags::UseSourceData) : Flags & EAnimationPoseFlags::UseSourceData);
	}

	/** True if we're requesting Source data instead of RawAnimationData. For debugging. */
	bool ShouldUseSourceData() const
	{
		return (Flags & EAnimationPoseFlags::UseSourceData) != EAnimationPoseFlags::None;
	}

};

using FAnimationLODPoseHeap = TAnimationLODPose<FDefaultAllocator>;
using FAnimationLODPoseStack = TAnimationLODPose<FAnimStackAllocator>;

using FAnimationLODPose = FAnimationLODPoseHeap;

} // namespace UE::AnimNext

// USTRUCT wrapper for reference pose
USTRUCT()
struct FAnimNextReferencePose
#if CPP
	: UE::AnimNext::FAnimationReferencePose
#endif
{
	GENERATED_BODY()
};

// USTRUCT wrapper for LOD pose
USTRUCT()
struct FAnimNextLODPose
#if CPP
	: UE::AnimNext::FAnimationLODPose
#endif
{
	GENERATED_BODY()
};