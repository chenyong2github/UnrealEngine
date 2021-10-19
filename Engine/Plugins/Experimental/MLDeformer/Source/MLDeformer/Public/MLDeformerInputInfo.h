// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformer.h"
#include "MLDeformerInputInfo.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UAnimInstance;
class UGeometryCache;

struct MLDEFORMER_API FMLDeformerInputInfoInitSettings
{
	/** The skeletal mesh to initialize for. Cannot be nullptr. */
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	/** The target mesh. Cannot be a nullptr. */
	TObjectPtr<UGeometryCache> TargetMesh = nullptr;

	/** The list of bone names to include in training. When empty, all bones are included. These are ignored when bIncludeBones is set to false. */
	TArray<FString> BoneNamesToInclude;

	/* The list of curve names to include in training. When empty, all curves are included. These are ignored when bIncludeCurves is set to false. */
	TArray<FString> CurveNamesToInclude;

	/** Do we want to include bone rotations as inputs to the training? */
	bool bIncludeBones = true;

	/** Do we want to include curve values as inputs to the training? */
	bool bIncludeCurves = true;
};

/**
 * The neural network input information.
 * This contains arrays of names for things such as bones and morph targets.
 * Knowing what bones etc are used as inputs, and in what order, helps us feeding the data during inference.
 * It can also help us detect issues, for example when the character we apply the deformer to is missing any of those bones.
 */
USTRUCT()
struct MLDEFORMER_API FMLDeformerInputInfo
{
	GENERATED_BODY()

public:
	/**
	 * Get the number of bones that we trained on.
	 * @return The number of bones.
	 */
	int32 GetNumBones() const { return BoneNames.Num(); }

	/**
	 * Get the bone name as a string, for a given bone we included during training.
	 * @param Index The bone index, which is a number in range of [0..GetNumBones()-1].
	 * @result The name of the bone.
	 */
	const FString& GetBoneNameString(int32 Index) const { return BoneNameStrings[Index]; }

	/**
	 * Get the bone name as an FName, for a given bone we included during training.
	 * @param Index The bone index, which is a number in range of [0..GetNumBones()-1].
	 * @result The name of the bone.
	 */
	const FName GetBoneName(int32 Index) const { return BoneNames[Index]; }

	/**
	 * Get the number of curves that we trained on.
	 * @return The number of curves.
	 */
	int32 GetNumCurves() const { return CurveNames.Num(); }

	/**
	 * Get the curve name as a string, for a given bone we included during training.
	 * @param Index The curve index, which is a number in range of [0..GetNumCurves()-1].
	 * @result The name of the curve.
	 */
	const FString& GetCurveNameString(int32 Index) const { return CurveNameStrings[Index]; }

	/**
	 * Get the curve name as an FName, for a given curve we included during training.
	 * @param Index The curve index, which is a number in range of [0..GetNumCurves()-1].
	 * @result The name of the curve.
	 */
	const FName GetCurveName(int32 Index) const { return CurveNames[Index]; }

	/**
	 * Initialize the inputs based on a skeletal mesh.
	 * @param InitSettings The init settings.
	 */
	void Init(const FMLDeformerInputInfoInitSettings& InitSettings);

	/**
	 * Check whether the current inputs are compatible with a given skeletal mesh.
	 * @param SkeletalMesh The skeletal mesh to check compatibility with. This may not be a nullptr.
	 * @return Returns true when we can safely apply the ML Deformer to a character using this skeletal mesh, otherwise false is returned.
	 * @note Use GenerateCompatibilityErrorString to get the error report.
	 */
	bool IsCompatible(USkeletalMesh* SkeletalMesh) const;

	/**
	 * Get the compatibility error report.
	 * @param SkeletalMesh The skeletal mesh to check compatibility with.
	 * @return Returns an empty string in case there are no compatibility issues, otherwise it contains a string that describes the issue(s).
	 *         In case a nullptr is passed as SkeletalMesh parameter, an empty string is returned.
	 */
	FString GenerateCompatibilityErrorString(USkeletalMesh* SkeletalMesh) const;

	/** 
	 * Update the FName arrays based on the name string arrays.
	 * This is automatically called on PostLoad of the UMLDeformerAsset.
	 */
	void UpdateFNames();

	/** 
	 * Extract the curve values for all curves we're interested in.
	 * @param SkelMeshComponent The skeletal mesh component to sample from.
	 * @param OutValues The array to write the values to. This array will be reset/resized by this method.
	 */
	void ExtractCurveValues(USkeletalMeshComponent* SkelMeshComponent, TArray<float>& OutValues) const;

	/**
	 * Extract bone space rotations, as a float array.
	 * The number of output rotations are NumBones * 4, where the array contains values like: xyzw,xyzw,xyzw,... where each xyzw are the 
	 * components of the bone's bone space (local space) rotation quaternion.
	 * @param SkelMeshComponent The skeletal mesh component to sample from.
	 * @param OutRotations The output rotation values. This array will be resized internally.
	 */
	void ExtractBoneRotations(USkeletalMeshComponent* SkelMeshComponent, TArray<float>& OutRotations) const;

	/**
	 * Get the number of imported vertices in the base mesh.
	 * @return The number of imported vertices in the base mesh.
	 */
	int32 GetNumBaseMeshVertices() const { return NumBaseMeshVertices; }

	/**
	 * Get the number of imported vertices in the target mesh.
	 * @return The number of imported vertices in the target mesh.
	 */
	int32 GetNumTargetMeshVertices() const { return NumTargetMeshVertices; }

	/**
	 * Check whether we have any training inputs or not.
	 * This happens when there are no bones or curves to use as inputs.
	 * @return Returns true when there are no bones or curves specified as inputs.
	 **/
	bool IsEmpty() const { return (BoneNameStrings.IsEmpty() && CurveNameStrings.IsEmpty()); }

	/**
	 * Calculate how many inputs this input info generates for the neural network.
	 * A single bone would take 4 inputs, while a curve takes one input.
	 * @return THe number of input float values to the neural network.
	 */
	int32 CalcNumNeuralNetInputs() const;

private:
	/** 
	 * The name of each bone. The inputs to the network are in the order of this array.
	 * So if the array contains ["Root", "Child1", "Child2"] then the first bone transforms that we 
	 * input to the neural network is the transform for "Root", followed by "Child1", followed by "Child2".
	 */
	UPROPERTY()
	TArray<FString> BoneNameStrings;

	/** The same as the BoneNames member, but stored as pre-created FName objects. These are not serialized. */
	UPROPERTY(Transient)
	TArray<FName> BoneNames;

	/**
	 * The name of each curve. The inputs to the network are in the order of this array.
	 * So if the array contains ["Smile", "LeftEyeClosed", "RightEyeClosed"] then the first curve that we
	 * input to the neural network is the one for "Smile", followed by "LeftEyeClosed", followed by "RightEyeClosed".
	 */
	UPROPERTY()
	TArray<FString> CurveNameStrings;

	/** The same as the CurveNames member, but stored as pre-created FName objects. These are not serialized. */
	UPROPERTY(Transient)
	TArray<FName> CurveNames;

	/** Number of imported base mesh vertices, so not render vertices. */
	UPROPERTY()
	int32 NumBaseMeshVertices = 0;

	/** Number of imported target mesh vertices, so not render vertices. */
	UPROPERTY()
	int32 NumTargetMeshVertices = 0;
};
