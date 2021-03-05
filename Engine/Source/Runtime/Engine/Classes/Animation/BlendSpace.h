// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Blend Space. Contains functionality shared across all blend space objects
 *
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BoneContainer.h"
#include "Animation/AnimationAsset.h"
#include "AnimationRuntime.h"
#include "AnimNodeBase.h"
#include "Containers/ArrayView.h"
#include "BlendSpace.generated.h"

/** Interpolation data types. */
UENUM()
enum EBlendSpaceAxis
{
	BSA_None UMETA(DisplayName = "None"),
	BSA_X UMETA(DisplayName = "Horizontal (X) Axis"),
	BSA_Y UMETA(DisplayName = "Vertical (Y) Axis")
};

UENUM()
enum class EPreferredTriangulationDirection : uint8
{
	None UMETA(DisplayName = "None"),
	Tangential UMETA(DisplayName = "Tangential"),
	Radial UMETA(DisplayName = "Radial")
};

USTRUCT()
struct FInterpolationParameter
{
	GENERATED_BODY()

	/** Interpolation Time for input, when it gets input, it will use this time to interpolate to target, used for smoother interpolation. */
	UPROPERTY(EditAnywhere, Category=Parameter)
	float InterpolationTime = 0.f;

	/** Type of interpolation used for filtering the input value to decide how to get to target. */
	UPROPERTY(EditAnywhere, Category=Parameter)
	TEnumAsByte<EFilterInterpolationType> InterpolationType = EFilterInterpolationType::BSIT_Average;
};

USTRUCT()
struct FBlendParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, DisplayName = "Name", Category=BlendParameter)
	FString DisplayName;

	/** Min value for this parameter. */
	UPROPERTY(EditAnywhere, DisplayName = "Minimum Axis Value", Category=BlendParameter)
	float Min;

	/** Max value for this parameter. */
	UPROPERTY(EditAnywhere, DisplayName = "Maximum Axis Value", Category=BlendParameter)
	float Max;

	/** The number of grid divisions for this parameter (axis). */
	UPROPERTY(EditAnywhere, DisplayName = "Number of Grid Divisions", Category=BlendParameter, meta=(UIMin="1", ClampMin="1"))
	int32 GridNum;

	/** If true then samples will always be snapped to the grid on this axis when added, moved, or the axes are changed. */
	UPROPERTY(EditAnywhere, DisplayName = "Snap to Grid", Category = BlendParameter)
	bool bSnapToGrid;

	/** If false then input parameters are clamped to the min/max values on this axis. If true then the input can go outside the min/max range and the blend space is treated as being cyclic on this axis. */
	UPROPERTY(EditAnywhere, DisplayName = "Wrap Input", Category = BlendParameter)
	bool bWrapInput;

	FBlendParameter()
		: DisplayName(TEXT("None"))
		, Min(0.f)
		, Max(100.f)
		, GridNum(4) // TODO when changing GridNum's default value, it breaks all grid samples ATM - provide way to rebuild grid samples during loading
		, bSnapToGrid(false)
		, bWrapInput(false)
	{
	}

	float GetRange() const
	{
		return Max-Min;
	}
	/** Return size of each grid. */
	float GetGridSize() const
	{
		return GetRange()/(float)GridNum;
	}
	
};

/** Sample data */
USTRUCT()
struct FBlendSample
{
	GENERATED_BODY()

	// For linked animations
	UPROPERTY(EditAnywhere, Category=BlendSample)
	TObjectPtr<class UAnimSequence> Animation;

	//blend 0->x, blend 1->y, blend 2->z

	UPROPERTY(EditAnywhere, Category=BlendSample)
	FVector SampleValue;
	
	UPROPERTY(EditAnywhere, Category = BlendSample, meta=(UIMin="0.01", UIMax="2.0", ClampMin="0.01", ClampMax="64.0"))
	float RateScale;

#if WITH_EDITORONLY_DATA
	UPROPERTY(transient)
	uint8 bIsValid : 1;

	// Cache the samples marker data counter so that we can track if it changes and revalidate the blendspace
	int32 CachedMarkerDataUpdateCounter;

#endif // WITH_EDITORONLY_DATA

	FBlendSample()
		: Animation(nullptr)
		, SampleValue(0.f)
		, RateScale(1.0f)
#if WITH_EDITORONLY_DATA
		, bIsValid(false)
		, CachedMarkerDataUpdateCounter(INDEX_NONE)
#endif // WITH_EDITORONLY_DATA
	{		
	}
	
	FBlendSample(class UAnimSequence* InAnim, FVector InValue, bool bInIsSnapped, bool bInIsValid) 
		: Animation(InAnim)
		, SampleValue(InValue)
		, RateScale(1.0f)
#if WITH_EDITORONLY_DATA
		, bIsValid(bInIsValid)
		, CachedMarkerDataUpdateCounter(INDEX_NONE)
#endif // WITH_EDITORONLY_DATA
	{		
	}
	
	bool operator==( const FBlendSample& Other ) const 
	{
		return (Other.Animation == Animation && Other.SampleValue == SampleValue && FMath::IsNearlyEqual(Other.RateScale, RateScale));
	}
};

/**
 * This is the runtime representation of a segment which stores its vertices (start and end) in normalized space.
 */
USTRUCT()
struct FBlendSpaceSegment
{
	GENERATED_BODY();

public:
	// Triangles have three vertices
	static const int32 NUM_VERTICES = 2;

	/** Indices into the samples */
	UPROPERTY()
	int32 SampleIndices[NUM_VERTICES];

	/** The vertices are in the normalized space - i.e. in the range 0-1. */
	UPROPERTY()
	float Vertices[NUM_VERTICES];
};

USTRUCT()
struct FBlendSpaceTriangleEdgeInfo
{
	GENERATED_BODY();

public:
	/** Edge normal faces out */
	UPROPERTY()
	FVector2D Normal;

	UPROPERTY()
	int32 NeighbourTriangleIndex;

	/**
	* IF there is no neighbor, then (a) we're on the perimeter and (b) these will be the indices of
	* triangles along the perimeter (next to the start and end of this edge, respectively) 
	*/
	UPROPERTY()
	int32 AdjacentPerimeterTriangleIndices[2];

	/**
	 * The vertex index of the associated AdjacentPerimeterTriangle such that the perimeter edge is
	 * from this vertex to the next.
	 */
	UPROPERTY()
	int32 AdjacentPerimeterVertexIndices[2];
};

/**
* This is the runtime representation of a triangle. Each triangle stores its vertices etc in normalized space,
* with an index to the original samples.
 */
USTRUCT()
struct FBlendSpaceTriangle
{
	GENERATED_BODY();

public:
	// Triangles have three vertices
	static const int32 NUM_VERTICES = 3;

public:

	/** Indices into the samples */
	UPROPERTY(EditAnywhere, Category = EditorElement)
	int32 SampleIndices[NUM_VERTICES];

	/** The vertices are in the normalized space - i.e. in the range 0-1. */
	UPROPERTY(EditAnywhere, Category = EditorElement)
	FVector2D Vertices[NUM_VERTICES];

	/** Info for the edge starting at the vertex index and going (anti-clockwise) to the next vertex */
	UPROPERTY(EditAnywhere, Category = EditorElement)
	FBlendSpaceTriangleEdgeInfo EdgeInfo[NUM_VERTICES];
};

USTRUCT()
struct FWeightedBlendSample
{
	GENERATED_BODY();

public:
	FWeightedBlendSample(int32 Index = INDEX_NONE, float Weight = 0) : SampleIndex(Index), SampleWeight(Weight) {}

public:
	UPROPERTY()
	int32 SampleIndex;

	UPROPERTY()
	float SampleWeight;
};

/**
* The runtime data used for interpolating. Note that only one of Segments/Triangles will be in use,
* depending on the dimensionality of the data.
*/
USTRUCT()
struct FBlendSpaceData
{
	GENERATED_BODY();
public:
	void GetSamples(
		TArray<FWeightedBlendSample>& OutWeightedSamples,
		const TArray<int32>&          InDimensionIndices,
		const FVector&                InSamplePosition,
		int32&                        InOutTriangulationIndex) const;

	void Empty()
	{
		Segments.Empty();
		Triangles.Empty();
	}

	bool IsEmpty() const 
	{
		return Segments.Num() == 0 && Triangles.Num() == 0;
	}
public:
	UPROPERTY()
	TArray<FBlendSpaceSegment> Segments;

	UPROPERTY()
	TArray<FBlendSpaceTriangle> Triangles;

private:
	void GetSamples1D(
		TArray<FWeightedBlendSample>& OutWeightedSamples,
		const TArray<int32>&          InDimensionIndices,
		const FVector&                InSamplePosition,
		int32&                        InOutSegmentIndex) const;

	void GetSamples2D(
		TArray<FWeightedBlendSample>& OutWeightedSamples,
		const TArray<int32>&          InDimensionIndices,
		const FVector&                InSamplePosition,
		int32&                        InOutTriangleIndex) const;
};

/**
 * Each elements in the grid
 */
USTRUCT()
struct FEditorElement
{
	GENERATED_BODY()

	// for now we only support triangles
	static const int32 MAX_VERTICES = 3;

	UPROPERTY(EditAnywhere, Category=EditorElement)
	int32 Indices[MAX_VERTICES];

	UPROPERTY(EditAnywhere, Category=EditorElement)
	float Weights[MAX_VERTICES];

	FEditorElement()
	{
		for (int32 ElementIndex = 0; ElementIndex < MAX_VERTICES; ElementIndex++)
		{
			Indices[ElementIndex] = INDEX_NONE;
			Weights[ElementIndex] = 0;
		}
	}
	
};

/** result of how much weight of the grid element **/
USTRUCT()
struct FGridBlendSample
{
	GENERATED_BODY()

	UPROPERTY()
	struct FEditorElement GridElement;

	UPROPERTY()
	float BlendWeight;

	FGridBlendSample()
		: BlendWeight(0)
	{
	}

};

USTRUCT()
struct FPerBoneInterpolation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category=FPerBoneInterpolation)
	FBoneReference BoneReference;

	/**
	* If greater than zero, this is the speed at which the sample weights are allowed to change for this specific bone.
	*
	* A speed of 1 means a sample weight can change from zero to one (or one to zero) in one second.
	* A speed of 2 means that this would take half a second.
	*
	* Smaller values mean slower adjustments of the sample weights, and thus more smoothing. However, a
	* value of zero disables this smoothing entirely.
	* 
	* If set, the value overrides the overall Sample Weight Speed which will no longer affect this bone.
	*/
	UPROPERTY(EditAnywhere, Category=FPerBoneInterpolation, meta=(DisplayName="Sample Weight Speed"))
	float InterpolationSpeedPerSec;

	FPerBoneInterpolation()
		: InterpolationSpeedPerSec(6.f)
	{}

	void Initialize(const USkeleton* Skeleton)
	{
		BoneReference.Initialize(Skeleton);
	}
};

UENUM()
namespace ENotifyTriggerMode
{
	enum Type
	{
		AllAnimations UMETA(DisplayName="All Animations"),
		HighestWeightedAnimation UMETA(DisplayName="Highest Weighted Animation"),
		None,
	};
}

/**
 * Allows multiple animations to be blended between based on input parameters
 */
UCLASS(config=Engine, hidecategories=Object, MinimalAPI, BlueprintType)
class UBlendSpace : public UAnimationAsset, public IInterpolationIndexProvider
{
	GENERATED_UCLASS_BODY()
public:

	/** Required for accessing protected variable names */
	friend class FBlendSpaceDetails;
	friend class UAnimGraphNode_BlendSpaceGraphBase;

	//~ Begin UObject Interface
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	//~ Begin UAnimationAsset Interface
	virtual void TickAssetPlayer(FAnimTickRecord& Instance, struct FAnimNotifyQueue& NotifyQueue, FAnimAssetTickContext& Context) const override;
	// this is used in editor only when used for transition getter
	// this doesn't mean max time. In Sequence, this is SequenceLength,
	// but for BlendSpace CurrentTime is normalized [0,1], so this is 1
	virtual float GetPlayLength() const override { return 1.f; }
	virtual TArray<FName>* GetUniqueMarkerNames() override;
	virtual bool IsValidAdditive() const override;
#if WITH_EDITOR
	virtual bool GetAllAnimationSequencesReferred(TArray<UAnimationAsset*>& AnimationAssets, bool bRecursive = true) override;
	virtual void ReplaceReferredAnimations(const TMap<UAnimationAsset*, UAnimationAsset*>& ReplacementMap) override;
	virtual int32 GetMarkerUpdateCounter() const;
	void    RuntimeValidateMarkerData();
#endif
	//~ End UAnimationAsset Interface
	
	// Begin IInterpolationIndexProvider Overrides
	/**
	* Get PerBoneInterpolationIndex for the input BoneIndex
	* If nothing found, return INDEX_NONE
	*/
	virtual int32 GetPerBoneInterpolationIndex(int32 BoneIndex, const FBoneContainer& RequiredBones) const override;	
	// End UBlendSpace Overrides

	/** Returns whether or not the given additive animation type is compatible with the blendspace type */
	ENGINE_API virtual bool IsValidAdditiveType(EAdditiveAnimationType AdditiveType) const;

	/**
	 * BlendSpace Get Animation Pose function
	 */
	UE_DEPRECATED(4.26, "Use GetAnimationPose with other signature")
	ENGINE_API void GetAnimationPose(TArray<FBlendSampleData>& BlendSampleDataCache, /*out*/ FCompactPose& OutPose, /*out*/ FBlendedCurve& OutCurve) const;
	
	ENGINE_API void GetAnimationPose(TArray<FBlendSampleData>& BlendSampleDataCache, /*out*/ FAnimationPoseData& OutAnimationPoseData) const;

	ENGINE_API void GetAnimationPose(TArray<FBlendSampleData>& BlendSampleDataCache, TArrayView<FPoseLink> InPoseLinks, /*out*/ FPoseContext& Output) const;

	/** Accessor for blend parameter **/
	ENGINE_API const FBlendParameter& GetBlendParameter(const int32 Index) const;

	/** Get this blend spaces sample data */
	const TArray<struct FBlendSample>& GetBlendSamples() const { return SampleData; }

	/** Returns the Blend Sample at the given index, will assert on invalid indices */
	ENGINE_API const struct FBlendSample& GetBlendSample(const int32 SampleIndex) const;

	/**
	* Get Grid Samples from BlendInput
	* It will return all samples that has weight > KINDA_SMALL_NUMBER
	*
	* @param	BlendInput	BlendInput X, Y, Z corresponds to BlendParameters[0], [1], [2]
	* @param	InOutCachedTriangulationIndex	The previous index into triangulation/segmentation to warm-start the search. 
	* @param	bCombineAnimations	Will combine samples that point to the same animation. Useful when processing, but confusing when viewing
	*
	* @return	true if it has valid OutSampleDataList, false otherwise
	*/
	ENGINE_API bool GetSamplesFromBlendInput(const FVector &BlendInput, TArray<FBlendSampleData> & OutSampleDataList, int32& InOutCachedTriangulationIndex, bool bCombineAnimations) const;

	/** Initialize BlendSpace for runtime. It needs certain data to be reinitialized per instsance **/
	ENGINE_API void InitializeFilter(FBlendFilter* Filter) const;

	/** Returns the blend input after clamping and/or wrapping */
	ENGINE_API FVector GetClampedAndWrappedBlendInput(const FVector& BlendInput) const;

	/** 
	 * Updates a cached set of blend samples according to internal parameters, blendspace position and a delta time. Used internally to GetAnimationPose().
	 * Note that this function does not perform any filtering internally.
 	 * @param	InBlendSpacePosition	The current position parameter of the blendspace
	 * @param	InOutSampleDataCache	The sample data cache. Previous frames samples are re-used in the case of target weight interpolation
	 * @param	InDeltaTime				The tick time for this update
	 */
	ENGINE_API bool UpdateBlendSamples(const FVector& InBlendSpacePosition, float InDeltaTime, TArray<FBlendSampleData>& InOutSampleDataCache, int32& InOutCachedTriangulationIndex) const;

	/** Interpolate BlendInput based on Filter data **/
	ENGINE_API FVector FilterInput(FBlendFilter* Filter, const FVector& BlendInput, float DeltaTime) const;

#if WITH_EDITOR	
	/** Validates sample data for blendspaces using the given animation sequence */
	ENGINE_API static void UpdateBlendSpacesUsingAnimSequence(UAnimSequenceBase* Sequence);

	/** Validates the contained data */
	ENGINE_API void ValidateSampleData();

	/** Add samples */
	ENGINE_API bool AddSample(const FVector& SampleValue);
	ENGINE_API bool	AddSample(UAnimSequence* AnimationSequence, const FVector& SampleValue);

	/** edit samples */
	ENGINE_API bool	EditSampleValue(const int32 BlendSampleIndex, const FVector& NewValue);

	UE_DEPRECATED(5.0, "Please use ReplaceSampleAnimation instead")
	ENGINE_API bool	UpdateSampleAnimation(UAnimSequence* AnimationSequence, const FVector& SampleValue);

	/** update animation on grid sample */
	ENGINE_API bool	ReplaceSampleAnimation(const int32 BlendSampleIndex, UAnimSequence* AnimationSequence);

	/** delete samples */
	ENGINE_API bool	DeleteSample(const int32 BlendSampleIndex);
	
	/** Get the number of sample points for this blend space */
	ENGINE_API int32 GetNumberOfBlendSamples()  const { return SampleData.Num(); }

	/** Check whether or not the sample index is valid in combination with the stored sample data */
	ENGINE_API bool IsValidBlendSampleIndex(const int32 SampleIndex) const;

	/**
	* return GridSamples from this BlendSpace
	*
	* @param	OutGridElements
	*
	* @return	Number of OutGridElements
	*/
	ENGINE_API const TArray<FEditorElement>& GetGridSamples() const;

	/**
	 * Returns the runtime triangulation etc data
	 */
	ENGINE_API const FBlendSpaceData& GetBlendSpaceData() const;

	/**
	 * Runs triangulation/segmentation to update our grid and BlendSpaceData structures
	 */
	ENGINE_API void ResampleData();

	/**
	 * Sets up BlendSpaceData based on Line elements
	 */
	ENGINE_API void SetBlendSpaceData(const TArray<FBlendSpaceSegment>& Segments);

	/** Validate that the given animation sequence and contained blendspace data */
	ENGINE_API bool ValidateAnimationSequence(const UAnimSequence* AnimationSequence) const;

	/** Check if the blend spaces contains samples whos additive type match that of the animation sequence */
	ENGINE_API bool DoesAnimationMatchExistingSamples(const UAnimSequence* AnimationSequence) const;
	
	/** Check if the the blendspace contains additive samples only */	
	ENGINE_API bool ShouldAnimationBeAdditive() const;

	/** Check if the animation sequence's skeleton is compatible with this blendspace */
	ENGINE_API bool IsAnimationCompatibleWithSkeleton(const UAnimSequence* AnimationSequence) const;

	/** Check if the animation sequence additive type is compatible with this blend space */
	ENGINE_API bool IsAnimationCompatible(const UAnimSequence* AnimationSequence) const;

	/** Validates supplied blend sample against current contents of blendspace */
	ENGINE_API bool ValidateSampleValue(const FVector& SampleValue, int32 OriginalIndex = INDEX_NONE) const;

	ENGINE_API bool IsSampleWithinBounds(const FVector &SampleValue) const;

	/** Check if given sample value isn't too close to existing sample point **/
	ENGINE_API bool IsTooCloseToExistingSamplePoint(const FVector& SampleValue, int32 OriginalIndex) const;
#endif

protected:
	/**
	* Get Grid Samples from BlendInput, From Input, it will populate OutGridSamples with the closest grid points.
	*
	* @param	BlendInput			BlendInput X, Y, Z corresponds to BlendParameters[0], [1], [2]
	* @param	OutBlendSamples		Populated with the samples nearest the BlendInput
	*
	*/
	void GetRawSamplesFromBlendInput(const FVector& BlendInput, TArray<FGridBlendSample, TInlineAllocator<4> >& OutBlendSamples) const;

	/** Returns the axis which can be used to scale animation speed. **/
	virtual EBlendSpaceAxis GetAxisToScale() const { return AxisToScaleAnimation; }

	/** Initialize Per Bone Blend **/
	void InitializePerBoneBlend();

	void TickFollowerSamples(TArray<FBlendSampleData> &SampleDataList, const int32 HighestWeightIndex, FAnimAssetTickContext &Context, bool bResetMarkerDataOnFollowers) const;

	/** Utility function to calculate animation length from sample data list **/
	float GetAnimationLengthFromSampleData(const TArray<FBlendSampleData> & SampleDataList) const;

	/** Returns the blend input clamped to the valid range, unless that axis has been set to wrap in which case no clamping is done **/
	FVector GetClampedBlendInput(const FVector& BlendInput) const;
	
	/** Translates BlendInput to grid space */
	FVector ConvertBlendInputToGridSpace(const FVector& BlendInput) const;

	/** Translates BlendInput to grid space */
	FVector GetNormalizedBlendInput(const FVector& BlendInput) const;

	/** Returns the grid element at Index or NULL if Index is not valid */
	const FEditorElement* GetGridSampleInternal(int32 Index) const;
	
	/** Utility function to interpolate weight of samples from OldSampleDataList to NewSampleDataList and copy back the interpolated result to FinalSampleDataList **/
	bool InterpolateWeightOfSampleData(float DeltaTime, const TArray<FBlendSampleData> & OldSampleDataList, const TArray<FBlendSampleData> & NewSampleDataList, TArray<FBlendSampleData> & FinalSampleDataList) const;

	/** Returns whether or not all animation set on the blend space samples match the given additive type */
	bool ContainsMatchingSamples(EAdditiveAnimationType AdditiveType) const;

	/** Checks if the given samples points overlap */
	bool IsSameSamplePoint(const FVector& SamplePointA, const FVector& SamplePointB) const;	

#if WITH_EDITOR
	bool ContainsNonAdditiveSamples() const;
	void UpdatePreviewBasePose();
	/** If around border, snap to the border to avoid empty hole of data that is not valid **/
	virtual void SnapSamplesToClosestGridPoint();
#endif // WITH_EDITOR
	
private:
	// Internal helper function for GetAnimationPose variants
	void GetAnimationPose_Internal(TArray<FBlendSampleData>& BlendSampleDataCache, TArrayView<FPoseLink> InPoseLinks, FAnimInstanceProxy* InProxy, bool bInExpectsAdditivePose, /*out*/ FAnimationPoseData& OutAnimationPoseData) const;

	// Internal helper function for UpdateBlendSamples and TickAssetPlayer
	bool UpdateBlendSamples_Internal(const FVector& InBlendSpacePosition, float InDeltaTime, TArray<FBlendSampleData>& InOutOldSampleDataList, TArray<FBlendSampleData>& InOutSampleDataCache, int32& InOutCachedTriangulationIndex) const;

public:
	/**
	* When you use blend per bone, allows rotation to blend in mesh space. This only works if this does not contain additive animation samples
	* This is more performance intensive
	*/
	UPROPERTY()
	bool bRotationBlendInMeshSpace;

	/** Input interpolation parameter for all 3 axis, for each axis input, decide how you'd like to interpolate input to*/
	UPROPERTY(EditAnywhere, Category = InputInterpolation)
	FInterpolationParameter	InterpolationParam[3];

#if WITH_EDITORONLY_DATA
	/** Preview Base pose for additive BlendSpace **/
	UPROPERTY(EditAnywhere, Category = AdditiveSettings)
	TObjectPtr<UAnimSequence> PreviewBasePose;
#endif // WITH_EDITORONLY_DATA

	/** This animation length changes based on current input (resulting in different blend time)**/
	UPROPERTY(transient)
	float AnimLength;

	/**
	* If greater than zero, this is the speed at which the sample weights are allowed to change.
	* 
	* A speed of 1 means a sample weight can change from zero to one (or one to zero) in one second.
	* A speed of 2 means that this would take half a second.
	* 
	* This allows the Blend Space to switch to new parameters without going through intermediate states, 
	* effectively blending between where it was and where the new target is. For example, imagine we have 
	* a blend space for locomotion, moving left, forward and right. Now if you interpolate the inputs of 
	* the blend space itself, from one extreme to the other, you will go from left, to forward, to right. 
	* As an alternative, by setting this Sample Weight Speed to a value higher than zero, it will go 
	* directly from left to right, without going through moving forward first.
	* 
	* Smaller values mean slower adjustments of the sample weights, and thus more smoothing. However, a 
	* value of zero disables this smoothing entirely.
	*/
	UPROPERTY(EditAnywhere, Category = SampleInterpolation, meta = (DisplayName = "Sample Weight Speed"))
	float TargetWeightInterpolationSpeedPerSec;

	/**
	 * If set then this eases in/out the sample weight adjustments, using the speed to determine how much smoothing to apply.
	 */
	UPROPERTY(EditAnywhere, Category = SampleInterpolation, meta = (DisplayName = "Sample Weight Speed Smoothing "))
	bool bTargetWeightInterpolationEaseInOut;

	/** The current mode used by the BlendSpace to decide which animation notifies to fire. Valid options are:
	- AllAnimations - All notify events will fire
	- HighestWeightedAnimation - Notify events will only fire from the highest weighted animation
	- None - No notify events will fire from any animations
	*/
	UPROPERTY(EditAnywhere, Category = AnimationNotifies)
	TEnumAsByte<ENotifyTriggerMode::Type> NotifyTriggerMode;

	/** If true then interpolation is done via a grid at runtime. If false the interpolation uses the triangulation. */
	UPROPERTY(EditAnywhere, Category = InputInterpolation)
	bool bInterpolateUsingGrid = false;

	/** Preferred edge direction when the triangulation has to make an arbitrary choice */
	UPROPERTY(EditAnywhere, Category = InputInterpolation)
	EPreferredTriangulationDirection PreferredTriangulationDirection = EPreferredTriangulationDirection::Tangential;

protected:

	/**
	* Per bone interpolation speed settings. 
	* These act as overrides to the global interpolation speed. 
	* This means the global interpolation speed does not impact these bones.
	*/
	UPROPERTY(EditAnywhere, Category = SampleInterpolation, meta = (DisplayName="Per Bone Overrides"))
	TArray<FPerBoneInterpolation> PerBoneBlend;

	/** Track index to get marker data from. Samples are tested for the suitability of marker based sync
	    during load and if we can use marker based sync we cache an index to a representative sample here */
	UPROPERTY()
	int32 SampleIndexWithMarkers;

	/** Sample animation data */
	UPROPERTY(EditAnywhere, Category=BlendSamples)
	TArray<struct FBlendSample> SampleData;

	/** Grid samples, indexing scheme imposed by subclass */
	UPROPERTY()
	TArray<struct FEditorElement> GridSamples;

	/** COntainer for the runtime data, which could be line segments, triangulation or tetrahedrons */
	UPROPERTY()
	FBlendSpaceData BlendSpaceData;
	
	/** Blend Parameters for each axis. **/
	UPROPERTY(EditAnywhere, Category = BlendParametersTest)
	struct FBlendParameter BlendParameters[3];

	/** If you have input interpolation, which axis to drive animation speed (scale) - i.e. for locomotion animation, speed axis will drive animation speed (thus scale) */
	UPROPERTY(EditAnywhere, Category = InputInterpolation)
	TEnumAsByte<EBlendSpaceAxis> AxisToScaleAnimation;

	/** Reset to reference pose. It does apply different refpose based on additive or not */
	void ResetToRefPose(FCompactPose& OutPose) const;

	/** The order in which to use the dimensions in the data - e.g. [1, 2] means a 2D blend using Y and Z */
	UPROPERTY()
	TArray<int32> DimensionIndices;

#if WITH_EDITOR
private:
	// Track whether we have updated markers so cached data can be updated
	int32 MarkerDataUpdateCounter;
protected:
	FVector PreviousAxisMinMaxValues[3];
	float   PreviousGridSpacings[3];
#endif	

private:

	void GetRawSamplesFromBlendInput1D(const FVector& BlendInput, TArray<FGridBlendSample, TInlineAllocator<4> >& OutBlendSamples) const;

	void GetRawSamplesFromBlendInput2D(const FVector& BlendInput, TArray<FGridBlendSample, TInlineAllocator<4> >& OutBlendSamples) const;

	/** Fill up local GridElements from the grid elements that are created using the sorted points
	*	This will map back to original index for result
	*
	*  @param	SortedPointList		This is the pointlist that are used to create the given GridElements
	*								This list contains subsets of the points it originally requested for visualization and sorted
	*
	*/
	void FillupGridElements(const TArray<FEditorElement>& GridElements, const TArray<int32>& InDimensionIndices);

	void EmptyGridElements();

	void ClearBlendSpaceData();

	void SetBlendSpaceData(const TArray<FBlendSpaceTriangle>& Triangles);

	void ResampleData1D();
	void ResampleData2D();


	/** Get the Editor Element from Index
	*
	* @param	XIndex	Index of X
	* @param	YIndex	Index of Y
	*
	* @return	FEditorElement * return the grid data
	*/
	const FEditorElement* GetEditorElement(int32 XIndex, int32 YIndex) const;
};
