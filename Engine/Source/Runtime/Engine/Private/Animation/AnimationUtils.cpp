// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AnimationUtils.cpp: Skeletal mesh animation utilities.
=============================================================================*/ 

#include "AnimationUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/Package.h"
#include "AnimationRuntime.h"
#include "Animation/AnimCompress.h"
#include "Animation/AnimCompress_BitwiseCompressOnly.h"
#include "Animation/AnimCompress_RemoveLinearKeys.h"
#include "Animation/AnimCompress_PerTrackCompression.h"
#include "Animation/AnimCompress_LeastDestructive.h"
#include "Animation/AnimCompress_RemoveEverySecondKey.h"
#include "Animation/AnimCompress_Automatic.h"
#include "Animation/AnimSet.h"
#include "Animation/AnimationSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "AnimationCompression.h"
#include "Engine/SkeletalMeshSocket.h"
#include "AnimEncoding.h"
#include "UObject/LinkerLoad.h"

/** Array to keep track of SkeletalMeshes we have built metadata for, and log out the results just once. */
//static TArray<USkeleton*> UniqueSkeletonsMetadataArray;

void FAnimationUtils::BuildSkeletonMetaData(USkeleton* Skeleton, TArray<FBoneData>& OutBoneData)
{
	// Disable logging by default. Except if we deal with a new Skeleton. Then we log out its details. (just once).
	bool bEnableLogging = false;
// Uncomment to enable.
// 	if( UniqueSkeletonsMetadataArray.FindItemIndex(Skeleton) == INDEX_NONE )
// 	{
// 		bEnableLogging = true;
// 		UniqueSkeletonsMetadataArray.AddItem(Skeleton);
// 	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const TArray<FTransform> & SkeletonRefPose = Skeleton->GetRefLocalPoses();
	const int32 NumBones = RefSkeleton.GetNum();

	// Assemble bone data.
	OutBoneData.Empty();
	OutBoneData.AddZeroed( NumBones );

	const TArray<FString>& KeyEndEffectorsMatchNameArray = UAnimationSettings::Get()->KeyEndEffectorsMatchNameArray;

	for (int32 BoneIndex = 0 ; BoneIndex<NumBones; ++BoneIndex )
	{
		FBoneData& BoneData = OutBoneData[BoneIndex];

		// Copy over data from the skeleton.
		const FTransform& SrcTransform = SkeletonRefPose[BoneIndex];

		ensure(!SrcTransform.ContainsNaN());
		ensure(SrcTransform.IsRotationNormalized());

		BoneData.Orientation = SrcTransform.GetRotation();
		BoneData.Position = SrcTransform.GetTranslation();
		BoneData.Name = RefSkeleton.GetBoneName(BoneIndex);

		if ( BoneIndex > 0 )
		{
			// Compute ancestry.
			int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
			BoneData.BonesToRoot.Add( ParentIndex );
			while ( ParentIndex > 0 )
			{
				ParentIndex = RefSkeleton.GetParentIndex(ParentIndex);
				BoneData.BonesToRoot.Add( ParentIndex );
			}
		}

		// See if a Socket is attached to that bone
		BoneData.bHasSocket = false;
		// @todo anim: socket isn't moved to Skeleton yet, but this code needs better testing
		for(int32 SocketIndex=0; SocketIndex<Skeleton->Sockets.Num(); SocketIndex++)
		{
			USkeletalMeshSocket* Socket = Skeleton->Sockets[SocketIndex];
			if( Socket && Socket->BoneName == RefSkeleton.GetBoneName(BoneIndex) )
			{
				BoneData.bHasSocket = true;
				break;
			}
		}
	}

	// Enumerate children (bones that refer to this bone as parent).
	for ( int32 BoneIndex = 0 ; BoneIndex < OutBoneData.Num() ; ++BoneIndex )
	{
		FBoneData& BoneData = OutBoneData[BoneIndex];
		// Exclude the root bone as it is the child of nothing.
		for ( int32 BoneIndex2 = 1 ; BoneIndex2 < OutBoneData.Num() ; ++BoneIndex2 )
		{
			if ( OutBoneData[BoneIndex2].GetParent() == BoneIndex )
			{
				BoneData.Children.Add(BoneIndex2);
			}
		}
	}

	// Enumerate end effectors.  For each end effector, propagate its index up to all ancestors.
	if( bEnableLogging )
	{
		UE_LOG(LogAnimationCompression, Log, TEXT("Enumerate End Effectors for %s"), *Skeleton->GetFName().ToString());
	}
	for ( int32 BoneIndex = 0 ; BoneIndex < OutBoneData.Num() ; ++BoneIndex )
	{
		FBoneData& BoneData = OutBoneData[BoneIndex];
		if ( BoneData.IsEndEffector() )
		{
			// End effectors have themselves as an ancestor.
			BoneData.EndEffectors.Add( BoneIndex );
			// Add the end effector to the list of end effectors of all ancestors.
			for ( int32 i = 0 ; i < BoneData.BonesToRoot.Num() ; ++i )
			{
				const int32 AncestorIndex = BoneData.BonesToRoot[i];
				OutBoneData[AncestorIndex].EndEffectors.Add( BoneIndex );
			}

			for(int32 MatchIndex=0; MatchIndex<KeyEndEffectorsMatchNameArray.Num(); MatchIndex++)
			{
				// See if this bone has been defined as a 'key' end effector
				FString BoneString(BoneData.Name.ToString());
				if( BoneString.Contains(KeyEndEffectorsMatchNameArray[MatchIndex]) )
				{
					BoneData.bKeyEndEffector = true;
					break;
				}
			}
			if( bEnableLogging )
			{
				UE_LOG(LogAnimationCompression, Log, TEXT("\t %s bKeyEndEffector: %d"), *BoneData.Name.ToString(), BoneData.bKeyEndEffector);
			}
		}
	}
#if 0
	UE_LOG(LogAnimationCompression, Log,  TEXT("====END EFFECTORS:") );
	int32 NumEndEffectors = 0;
	for ( int32 BoneIndex = 0 ; BoneIndex < OutBoneData.Num() ; ++BoneIndex )
	{
		const FBoneData& BoneData = OutBoneData(BoneIndex);
		if ( BoneData.IsEndEffector() )
		{
			FString Message( FString::Printf(TEXT("%s(%i): "), *BoneData.Name, BoneData.GetDepth()) );
			for ( int32 i = 0 ; i < BoneData.BonesToRoot.Num() ; ++i )
			{
				const int32 AncestorIndex = BoneData.BonesToRoot(i);
				Message += FString::Printf( TEXT("%s "), *OutBoneData(AncestorIndex).Name );
			}
			UE_LOG(LogAnimation, Log,  *Message );
			++NumEndEffectors;
		}
	}
	UE_LOG(LogAnimationCompression, Log,  TEXT("====NUM EFFECTORS %i(%i)"), NumEndEffectors, OutBoneData(0).Children.Num() );
	UE_LOG(LogAnimationCompression, Log,  TEXT("====NON END EFFECTORS:") );
	for ( int32 BoneIndex = 0 ; BoneIndex < OutBoneData.Num() ; ++BoneIndex )
	{
		const FBoneData& BoneData = OutBoneData(BoneIndex);
		if ( !BoneData.IsEndEffector() )
		{
			FString Message( FString::Printf(TEXT("%s(%i): "), *BoneData.Name, BoneData.GetDepth()) );
			Message += TEXT("Children: ");
			for ( int32 i = 0 ; i < BoneData.Children.Num() ; ++i )
			{
				const int32 ChildIndex = BoneData.Children(i);
				Message += FString::Printf( TEXT("%s "), *OutBoneData(ChildIndex).Name );
			}
			Message += TEXT("  EndEffectors: ");
			for ( int32 i = 0 ; i < BoneData.EndEffectors.Num() ; ++i )
			{
				const int32 EndEffectorIndex = BoneData.EndEffectors(i);
				Message += FString::Printf( TEXT("%s "), *OutBoneData(EndEffectorIndex).Name );
				check( OutBoneData(EndEffectorIndex).IsEndEffector() );
			}
			UE_LOG(LogAnimationCompression, Log,  *Message );
		}
	}
	UE_LOG(LogAnimationCompression, Log,  TEXT("===================") );
#endif
}

/**
* Builds the local-to-component matrix for the specified bone.
*/
void FAnimationUtils::BuildComponentSpaceTransform(FTransform& OutTransform,
											   int32 BoneIndex,
											   const TArray<FTransform>& BoneSpaceTransforms,
											   const TArray<FBoneData>& BoneData)
{
	// Put root-to-component in OutTransform.
	OutTransform = BoneSpaceTransforms[0];

	if (BoneIndex > 0)
	{
		const FBoneData& Bone = BoneData[BoneIndex];

		checkSlow((Bone.BonesToRoot.Num() - 1) == 0);

		// Compose BoneData.BonesToRoot down.
		for (int32 i = Bone.BonesToRoot.Num()-2; i >=0; --i)
		{
			const int32 AncestorIndex = Bone.BonesToRoot[i];
			ensure(AncestorIndex != INDEX_NONE);
			OutTransform = BoneSpaceTransforms[AncestorIndex] * OutTransform;
			OutTransform.NormalizeRotation();
		}

		// Finally, include the bone's local-to-parent.
		OutTransform = BoneSpaceTransforms[BoneIndex] * OutTransform;
		OutTransform.NormalizeRotation();
	}
}

int32 FAnimationUtils::GetAnimTrackIndexForSkeletonBone(const int32 InSkeletonBoneIndex, const TArray<FTrackToSkeletonMap>& TrackToSkelMap)
{
	return TrackToSkelMap.IndexOfByPredicate([&](const FTrackToSkeletonMap& TrackToSkel)
	{ 
		return TrackToSkel.BoneTreeIndex == InSkeletonBoneIndex;
	});
}

#if WITH_EDITOR
/**
 * Utility function to measure the accuracy of a compressed animation. Each end-effector is checked for 
 * world-space movement as a result of compression.
 *
 * @param	AnimSet		The animset to calculate compression error for.
 * @param	BoneData	BoneData array describing the hierarchy of the animated skeleton
 * @param	ErrorStats	Output structure containing the final compression error values
 * @return				None.
 */
void FAnimationUtils::ComputeCompressionError(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& CompressedData, AnimationErrorStats& ErrorStats)
{
	ErrorStats.AverageError = 0.0f;
	ErrorStats.MaxError = 0.0f;
	ErrorStats.MaxErrorBone = 0;
	ErrorStats.MaxErrorTime = 0.0f;
	int32 MaxErrorTrack = -1;

	if (CompressedData.CompressedNumberOfFrames > 0)
	{
		const bool bCanUseCompressedData = (CompressedData.CompressedByteStream.Num() > 0);
		if (!bCanUseCompressedData)
		{
			// If we can't use CompressedData, there's not much point in being here.
			return;
		}

		const int32 NumBones = CompressibleAnimData.BoneData.Num();
		
		float ErrorCount = 0.0f;
		float ErrorTotal = 0.0f;

		USkeleton* Skeleton = CompressibleAnimData.Skeleton;
		check ( Skeleton );

		const TArray<FTransform>& RefPose = Skeleton->GetRefLocalPoses();

		TArray<FTransform> RawTransforms;
		TArray<FTransform> NewTransforms;
		RawTransforms.AddZeroed(NumBones);
		NewTransforms.AddZeroed(NumBones);

		// Cache these to speed up animations with a lot of frames.
		// We do this only once, instead of every frame.
		struct FCachedBoneIndexData
		{
			int32 TrackIndex;
			int32 ParentIndex;
		};
		TArray<FCachedBoneIndexData> CachedBoneIndexData;
		CachedBoneIndexData.AddZeroed(NumBones);
		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			FCachedBoneIndexData& BoneIndexData = CachedBoneIndexData[BoneIndex];
			BoneIndexData.TrackIndex = GetAnimTrackIndexForSkeletonBone(BoneIndex, CompressibleAnimData.TrackToSkeletonMapTable);
			BoneIndexData.ParentIndex = Skeleton->GetReferenceSkeleton().GetParentIndex(BoneIndex);
		}

		// Check the precondition that parents occur before children in the RequiredBones array.
		for (int32 BoneIndex = 1; BoneIndex < NumBones; ++BoneIndex)
		{
			const FCachedBoneIndexData& BoneIndexData = CachedBoneIndexData[BoneIndex];
			check(BoneIndexData.ParentIndex != INDEX_NONE);
			check(BoneIndexData.ParentIndex < BoneIndex);
		}

		const FTransform EndEffectorDummyBoneSocket(FQuat::Identity, FVector(END_EFFECTOR_DUMMY_BONE_LENGTH_SOCKET));
		const FTransform EndEffectorDummyBone(FQuat::Identity, FVector(END_EFFECTOR_DUMMY_BONE_LENGTH));
		const FAnimKeyHelper Helper(CompressibleAnimData.SequenceLength, CompressedData.CompressedNumberOfFrames);
		const float KeyLength = Helper.TimePerKey() + SMALL_NUMBER;

		const FUECompressedAnimData CompressedDataWrapper(CompressedData);
		FAnimSequenceDecompressionContext DecompContext(CompressibleAnimData, CompressedDataWrapper);

		const TArray<FBoneData>& BoneData = CompressibleAnimData.BoneData;

		for (int32 FrameIndex = 0; FrameIndex< CompressedData.CompressedNumberOfFrames; FrameIndex++)
		{
			const float Time = (float)FrameIndex * KeyLength;
			DecompContext.Seek(Time);

			// get the raw and compressed atom for each bone
			for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
			{
				const FCachedBoneIndexData& BoneIndexData = CachedBoneIndexData[BoneIndex];
				if (BoneIndexData.TrackIndex == INDEX_NONE)
				{
					// No track for the bone was found, use default transform
					const FTransform& RefPoseTransform = RefPose[BoneIndex];
					RawTransforms[BoneIndex] = RefPoseTransform;
					NewTransforms[BoneIndex] = RefPoseTransform;
				}
				else
				{
					// If we have transforms, but they're additive, apply them to the ref pose.
					// This is because additive animations are mostly rotation.
					// And for the error metric we measure distance between end effectors.
					// So that means additive animations by default will all be balled up at the origin and not show any error.
					if (CompressibleAnimData.bIsValidAdditive)
					{
						const FTransform& RefPoseTransform = RefPose[BoneIndex];
						RawTransforms[BoneIndex] = RefPoseTransform;
						NewTransforms[BoneIndex] = RefPoseTransform;

						FTransform AdditiveRawTransform;
						FTransform AdditiveNewTransform;
						FAnimationUtils::ExtractTransformFromTrack(Time, CompressibleAnimData.NumFrames, CompressibleAnimData.SequenceLength, CompressibleAnimData.RawAnimationData[BoneIndexData.TrackIndex], CompressibleAnimData.Interpolation, AdditiveRawTransform);

						AnimationFormat_GetBoneAtom(AdditiveNewTransform, DecompContext, BoneIndexData.TrackIndex);

						const ScalarRegister VBlendWeight(1.f);
						RawTransforms[BoneIndex].AccumulateWithAdditiveScale(AdditiveRawTransform, VBlendWeight);
						NewTransforms[BoneIndex].AccumulateWithAdditiveScale(AdditiveNewTransform, VBlendWeight);
					}
					else
					{
						FAnimationUtils::ExtractTransformFromTrack(Time, CompressibleAnimData.NumFrames, CompressibleAnimData.SequenceLength, CompressibleAnimData.RawAnimationData[BoneIndexData.TrackIndex], CompressibleAnimData.Interpolation, RawTransforms[BoneIndex]);
						AnimationFormat_GetBoneAtom(NewTransforms[BoneIndex], DecompContext, BoneIndexData.TrackIndex);
					}
				}

				ensure(!RawTransforms[BoneIndex].ContainsNaN());
				ensure(!NewTransforms[BoneIndex].ContainsNaN());

				// For all bones below the root, final component-space transform is relative transform * component-space transform of parent.
				if (BoneIndex > 0)
				{
					RawTransforms[BoneIndex] *= RawTransforms[BoneIndexData.ParentIndex];
					NewTransforms[BoneIndex] *= NewTransforms[BoneIndexData.ParentIndex];
				}
				
				// If this is an EndEffector, add a dummy bone to measure the effect of compressing the rotation.
				if (BoneData[BoneIndex].IsEndEffector())
				{
					// Sockets and Key EndEffectors have a longer dummy bone to maintain higher precision.
					if (BoneData[BoneIndex].bHasSocket || BoneData[BoneIndex].bKeyEndEffector)
					{
						RawTransforms[BoneIndex] = EndEffectorDummyBoneSocket * RawTransforms[BoneIndex];
						NewTransforms[BoneIndex] = EndEffectorDummyBoneSocket * NewTransforms[BoneIndex];
					}
					else
					{
						RawTransforms[BoneIndex] = EndEffectorDummyBone * RawTransforms[BoneIndex];
						NewTransforms[BoneIndex] = EndEffectorDummyBone * NewTransforms[BoneIndex];
					}
				}

				// Normalize rotations
				RawTransforms[BoneIndex].NormalizeRotation();
				NewTransforms[BoneIndex].NormalizeRotation();

				if (BoneData[BoneIndex].IsEndEffector())
				{
					float Error = (RawTransforms[BoneIndex].GetLocation() - NewTransforms[BoneIndex].GetLocation()).Size();

					ErrorTotal += Error;
					ErrorCount += 1.0f;

					if (Error > ErrorStats.MaxError)
					{
						ErrorStats.MaxError	= Error;
						ErrorStats.MaxErrorBone = BoneIndex;
						MaxErrorTrack = BoneIndexData.TrackIndex;
						ErrorStats.MaxErrorTime = Time;
					}
				}
			}
		}

		if (ErrorCount > 0.0f)
		{
			ErrorStats.AverageError = ErrorTotal / ErrorCount;
		}
	}
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Default animation compression algorithm.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

/**
 * @return		A new instance of the default animation compression algorithm singleton, attached to the root set.
 */
static inline UAnimCompress* ConstructDefaultCompressionAlgorithm()
{
	// Algorithm.
	const UAnimationSettings* AnimSettings = UAnimationSettings::Get();

	// Rotation compression format.
	AnimationCompressionFormat RotationCompressionFormat = AnimSettings->RotationCompressionFormat;
	// Translation compression format.
	AnimationCompressionFormat TranslationCompressionFormat = AnimSettings->TranslationCompressionFormat;

	UClass* CompressionAlgorithmClass = AnimSettings->DefaultCompressionAlgorithm;
	if ( !CompressionAlgorithmClass )
	{
		// if can't find back out to bitwise
		CompressionAlgorithmClass = UAnimCompress_BitwiseCompressOnly::StaticClass();
		UE_LOG(LogAnimationCompression, Warning, TEXT("Couldn't find animation compression, default to AnimCompress_BitwiseCompressOnly") );
	}

	UAnimCompress* NewAlgorithm = NewObject<UAnimCompress>(GetTransientPackage(), CompressionAlgorithmClass);
	NewAlgorithm->RotationCompressionFormat = RotationCompressionFormat;
	NewAlgorithm->TranslationCompressionFormat = TranslationCompressionFormat;
	NewAlgorithm->AddToRoot();
	return NewAlgorithm;
}

} // namespace

/**
 * @return		The default animation compression algorithm singleton, instantiating it if necessary.
 */
UAnimCompress* FAnimationUtils::GetDefaultAnimationCompressionAlgorithm()
{
	static UAnimCompress* SAlgorithm = ConstructDefaultCompressionAlgorithm();
	return SAlgorithm;
}

/**
 * Determines the current setting for world-space error tolerance in the animation compressor.
 * When requested, animation being compressed will also consider an alternative compression
 * method if the end result of that method produces less error than the AlternativeCompressionThreshold.
 * The default tolerance value is 0.0f (no alternatives allowed) but may be overridden using a field in the base engine INI file.
 *
 * @return				World-space error tolerance for considering an alternative compression method
 */
float FAnimationUtils::GetAlternativeCompressionThreshold()
{
	// Allow the Engine INI file to provide a new override
	return UAnimationSettings::Get()->AlternativeCompressionThreshold;
}

/**
 * Determines the current setting for recompressing all animations upon load. The default value 
 * is False, but may be overridden by an optional field in the base engine INI file. 
 *
 * @return				true if the engine settings request that all animations be recompiled
 */
bool FAnimationUtils::GetForcedRecompressionSetting()
{
	// Allow the Engine INI file to provide a new override
	bool ForcedRecompressionSetting = false;
	GConfig->GetBool( TEXT("AnimationCompression"), TEXT("ForceRecompression"), (bool&)ForcedRecompressionSetting, GEngineIni );

	return ForcedRecompressionSetting;
}

struct FAnimCompressionJobContext
{
	// Inputs
	bool bForceBelowThreshold;
	FAnimCompressContext* CompressContext;
	UAnimCompress* CompressionAlgorithm;
	
	const FCompressibleAnimData* CompressibleAnimData;
	FCompressibleAnimDataResult CompressionResult;

	const TCHAR* CompressionName;
	int32* WinningCompressor_Count;
	float* WinningCompressor_Error;
	int64* WinningCompressor_Margin;

	// Outputs
	AnimationErrorStats ErrorStats;
	float PctSaving;

	FAnimCompressionJobContext()
	{
		memset(this, 0, sizeof(FAnimCompressionJobContext));
	}

	void UpdatePctSaving(int64 OriginalSize)
	{
		PctSaving = (OriginalSize > 0) ? (100.f - (100.f * float(CompressionResult.GetApproxBoneCompressedSize()) / float(OriginalSize))) : 0.f;
	}
};

static void TryCompressionInner(FAnimCompressionJobContext& JobContext, bool is_async);

class FAsyncAnimCompressionTask
{
public:
	FAsyncAnimCompressionTask(FAnimCompressionJobContext* JobContext_)
		: JobContext(JobContext_)
	{}

	/** return the name of the task **/
	static const TCHAR* GetTaskName() { return TEXT("FAsyncAnimCompressionTask"); }
	FORCEINLINE static TStatId GetStatId() { RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncAnimCompressionTask, STATGROUP_TaskGraphTasks); }

	static ENamedThreads::Type GetDesiredThread() { return ENamedThreads::AnyThread; }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		TryCompressionInner(*JobContext, true);
	}

	FAnimCompressionJobContext* JobContext;
};

bool ShouldKeepNewCompressionMethod(const FAnimCompressionJobContext& JobContext, SIZE_T OriginalSize, SIZE_T CurrentSize, float WinningCompressorError, float MasterTolerance)
{
	const SIZE_T NewSize = JobContext.CompressionResult.GetApproxBoneCompressedSize();

	/* compute the savings and compression error*/
	const int64 MemorySavingsFromOriginal = OriginalSize - NewSize;
	const int64 MemorySavingsFromPrevious = CurrentSize - NewSize;

	/* figure out our new compression error*/
	const AnimationErrorStats& ErrorStats = JobContext.ErrorStats;

	const bool bLowersError = ErrorStats.MaxError < WinningCompressorError;
	const bool bErrorUnderThreshold = ErrorStats.MaxError <= MasterTolerance;

	/* keep it if it we want to force the error below the threshold and it reduces error */
	bool bKeepNewCompressionMethod = false;
	const bool bReducesErrorBelowThreshold = (bLowersError && (WinningCompressorError > MasterTolerance) && JobContext.bForceBelowThreshold);
	bKeepNewCompressionMethod |= bReducesErrorBelowThreshold;
	/* or if has an acceptable error and saves space  */
	const bool bHasAcceptableErrorAndSavesSpace = bErrorUnderThreshold && (MemorySavingsFromPrevious > 0);
	bKeepNewCompressionMethod |= bHasAcceptableErrorAndSavesSpace;
	/* or if saves the same amount and an acceptable error that is lower than the previous best */
	const bool bLowersErrorAndSavesSameOrBetter = bErrorUnderThreshold && bLowersError && (MemorySavingsFromPrevious >= 0);
	bKeepNewCompressionMethod |= bLowersErrorAndSavesSameOrBetter;

	UE_LOG(LogAnimationCompression, Verbose, TEXT("- %s - bytes saved(%i) (%.1f%%) from previous(%i) MaxError(%.2f) bLowersError(%d) %s"),
		JobContext.CompressionName, MemorySavingsFromOriginal, JobContext.PctSaving, MemorySavingsFromPrevious, ErrorStats.MaxError, bLowersError, bKeepNewCompressionMethod ? TEXT("(**Best so far**)") : TEXT(""));

	UE_LOG(LogAnimationCompression, Verbose, TEXT("    bReducesErrorBelowThreshold(%d) bHasAcceptableErrorAndSavesSpace(%d) bLowersErrorAndSavesSameOrBetter(%d)"),
		bReducesErrorBelowThreshold, bHasAcceptableErrorAndSavesSpace, bLowersErrorAndSavesSameOrBetter);

	UE_LOG(LogAnimationCompression, Verbose, TEXT("    WinningCompressorError(%f) MasterTolerance(%f) bForceBelowThreshold(%d) bErrorUnderThreshold(%d)"),
		WinningCompressorError, MasterTolerance, JobContext.bForceBelowThreshold, bErrorUnderThreshold);
	return bKeepNewCompressionMethod;
}

static void TryCompressionInner(FAnimCompressionJobContext& JobContext, bool bIsAsync)
{
#if WITH_EDITOR
	/* try the alternative compressor	*/
	JobContext.CompressionAlgorithm->Reduce(*JobContext.CompressibleAnimData, JobContext.CompressionResult);
	FAnimationUtils::ComputeCompressionError(*JobContext.CompressibleAnimData, JobContext.CompressionResult, JobContext.ErrorStats);
#endif	// WITH_EDITOR
}

#if WITH_EDITORONLY_DATA
struct WinningCompressorStats
{
	SIZE_T CurrentSize;

	int32* WinningCompressorCounter;
	float* WinningCompressorErrorSum;
	int64* WinningCompressorMarginalSavingsSum;
	int64 WinningCompressorMarginalSavings;
	FString WinningCompressorName;
	int32 WinningCompressorSavings;
	float PctSaving;
	float WinningCompressorError;

	WinningCompressorStats()
	{
		memset(this, 0, sizeof(WinningCompressorStats));
	}
};

void HandlePostTryCompression(FAnimCompressionJobContext& JobContext, int64 OriginalSize, float MasterTolerance, WinningCompressorStats& OutCompressorStats, FCompressibleAnimDataResult& OutCompressedData, AnimationErrorStats& OutNewErrorStats)
{
	JobContext.UpdatePctSaving(OriginalSize);

	bool bKeepNewCompressionMethod = ShouldKeepNewCompressionMethod(JobContext, OriginalSize, OutCompressorStats.CurrentSize, OutCompressorStats.WinningCompressorError, MasterTolerance);
	if (bKeepNewCompressionMethod)
	{
		OutCompressedData = JobContext.CompressionResult;

		OutNewErrorStats = JobContext.ErrorStats;

		int64 NewSize = JobContext.CompressionResult.GetApproxBoneCompressedSize();
		const int64 MemorySavingsFromOriginal = OriginalSize - NewSize;
		const int64 MemorySavingsFromPrevious = OutCompressorStats.CurrentSize - NewSize;

		OutCompressorStats.CurrentSize = NewSize;
		OutCompressorStats.WinningCompressorMarginalSavings = MemorySavingsFromPrevious;
		OutCompressorStats.WinningCompressorCounter = JobContext.WinningCompressor_Count; 
		OutCompressorStats.WinningCompressorErrorSum = JobContext.WinningCompressor_Error; 
		OutCompressorStats.WinningCompressorMarginalSavingsSum = JobContext.WinningCompressor_Margin; 
		OutCompressorStats.WinningCompressorName = JobContext.CompressionName; 
		OutCompressorStats.WinningCompressorSavings = MemorySavingsFromOriginal;
		OutCompressorStats.WinningCompressorError = JobContext.ErrorStats.MaxError;
		OutCompressorStats.PctSaving = JobContext.PctSaving;
	}
}
#endif

#define POPULATE_ANIM_COMPRESSION_JOB_CONTEXT(Name_, CompressionAlgorithm_, CompressContext_, JobContext_) \
	(JobContext_).bForceBelowThreshold = bForceBelowThreshold; \
	(JobContext_).CompressContext = &CompressContext_; \
	(JobContext_).CompressibleAnimData = &CompressibleAnimData; \
	(JobContext_).CompressionAlgorithm = DuplicateObject<UAnimCompress>((CompressionAlgorithm_), GetTransientPackage()); \
	(JobContext_).CompressionAlgorithm->bEnableSegmenting = bEnableSegmenting; \
	(JobContext_).CompressionAlgorithm->IdealNumFramesPerSegment = IdealNumFramesPerSegment; \
	(JobContext_).CompressionAlgorithm->MaxNumFramesPerSegment = MaxNumFramesPerSegment; \
	(JobContext_).CompressionName = TEXT(#Name_); \
	(JobContext_).WinningCompressor_Count = &(Name_ ## CompressorWins); \
	(JobContext_).WinningCompressor_Error = &(Name_ ## CompressorSumError); \
	(JobContext_).WinningCompressor_Margin = &(Name_ ## CompressorWinMargin);

#define TRYCOMPRESSION(Name, CompressionAlgorithm_) \
	do \
	{ \
		FAnimCompressionJobContext JobContext; \
		POPULATE_ANIM_COMPRESSION_JOB_CONTEXT(Name, CompressionAlgorithm_, CompressContext, JobContext); \
		\
		TryCompressionInner(JobContext, false); \
		\
		HandlePostTryCompression(JobContext, OriginalSize, MasterTolerance, CompressorStats, OutCompressedData, NewErrorStats); \
	} \
	while (false)

#define TRYCOMPRESSION_SYNC(Name, CompressionAlgorithm_) TRYCOMPRESSION(Name, CompressionAlgorithm_)

// TODO: Async compression is DISABLED for additive sequences because UAnimCompress_RemoveLinearKeys::ConvertFromRelativeSpace()
// modifies the RAW data! This is bad... Even though we duplicate the anim sequence, some additive information isn't
// copied over and it doesn't seem safe to generate it by calling UAnimSequence::BakeOutAdditiveIntoRawData()
#define TRYCOMPRESSION_ASYNC(Name, CompressionAlgorithm_) \
	do \
	{ \
		if (!CompressibleAnimData.bIsValidAdditive) \
		{ \
			FAnimCompressionJobContext* JobContext = new FAnimCompressionJobContext(); \
			FAnimCompressContext CompressContextCopy(CompressContext); \
			CompressContextCopy.CompressionSummary = FCompressionMemorySummary(false); \
			POPULATE_ANIM_COMPRESSION_JOB_CONTEXT(Name, CompressionAlgorithm_, CompressContextCopy, *JobContext); \
			AnimCompressionJobContexes.Add(JobContext); \
			\
			AnimCompressionTask_CompletionEvents.Add(TGraphTask<FAsyncAnimCompressionTask>::CreateTask(NULL, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(JobContext)); \
		} \
		else \
		{ \
			TRYCOMPRESSION_SYNC(Name, CompressionAlgorithm_); \
		} \
	} \
	while (false)

#define LOG_COMPRESSION_STATUS(Name) \
	UE_LOG(LogAnimationCompression, Log, TEXT("\t\tWins for '%32s': %4i\t\t%f\t%i bytes"), TEXT(#Name), Name ## CompressorWins, (Name ## CompressorWins > 0) ? Name ## CompressorSumError / Name ## CompressorWins : 0.0f, Name ## CompressorWinMargin)

#define DECLARE_ANIM_COMP_ALGORITHM(Algorithm) \
	static int32 Algorithm ## CompressorWins = 0; \
	static float Algorithm ## CompressorSumError = 0.0f; \
	static int64 Algorithm ## CompressorWinMargin = 0

#if WITH_EDITORONLY_DATA

static void WaitForAnimCompressionJobs(const FGraphEventArray& AnimCompressionTask_CompletionEvents)
{
	FTaskGraphInterface::Get().WaitUntilTasksComplete(AnimCompressionTask_CompletionEvents, ENamedThreads::GameThread);
}

static void ClearAnimCompressionJobs(FGraphEventArray& AnimCompressionTask_CompletionEvents, TArray<FAnimCompressionJobContext*>& AnimCompressionJobContexes)
{
	for (FAnimCompressionJobContext* Context : AnimCompressionJobContexes)
	{
		delete Context;
	}
	AnimCompressionJobContexes.Reset();
	AnimCompressionTask_CompletionEvents.Reset();
}

static FAnimCompressionJobContext* FindBestAnimCompression(TArray<FAnimCompressionJobContext*>& AnimCompressionJobContexes, SIZE_T OriginalSize, SIZE_T CurrentSize, float WinningCompressorError, float MasterTolerance)
{
	FAnimCompressionJobContext* BestJobContext = nullptr;

	for (FAnimCompressionJobContext* context : AnimCompressionJobContexes)
	{
		FAnimCompressionJobContext& JobContext = *context;
		JobContext.UpdatePctSaving(OriginalSize);

		bool bKeepNewCompressionMethod = ShouldKeepNewCompressionMethod(JobContext, OriginalSize, CurrentSize, WinningCompressorError, MasterTolerance);

		if (bKeepNewCompressionMethod)
		{
			BestJobContext = context;
			WinningCompressorError = JobContext.ErrorStats.MaxError;
			CurrentSize = JobContext.CompressionResult.GetApproxBoneCompressedSize();
		}
	}

	return BestJobContext;
}

static void UpdateAnimCompressionFromAsyncJobs(FCompressibleAnimDataResult& OutCompressedData, FGraphEventArray& AnimCompressionTask_CompletionEvents, TArray<FAnimCompressionJobContext*>& AnimCompressionJobContexes, SIZE_T OriginalSize, WinningCompressorStats& CompressorStats, float MasterTolerance)
{
	// Pick the best
	const FAnimCompressionJobContext* BestJobContext = FindBestAnimCompression(AnimCompressionJobContexes, OriginalSize, CompressorStats.CurrentSize, CompressorStats.WinningCompressorError, MasterTolerance);

	if (BestJobContext != nullptr)
	{
		const FAnimCompressionJobContext& JobContext = *BestJobContext;

		/* Copy our data */
		OutCompressedData = JobContext.CompressionResult;

		const SIZE_T NewSize = OutCompressedData.GetApproxBoneCompressedSize();

		const int64 MemorySavingsFromOriginal = OriginalSize - NewSize;
		const SIZE_T MemorySavingsFromPrevious = CompressorStats.CurrentSize - NewSize;

		CompressorStats.WinningCompressorMarginalSavings = MemorySavingsFromPrevious;
		CompressorStats.WinningCompressorCounter = JobContext.WinningCompressor_Count;
		CompressorStats.WinningCompressorErrorSum = JobContext.WinningCompressor_Error;
		CompressorStats.WinningCompressorMarginalSavingsSum = JobContext.WinningCompressor_Margin;
		CompressorStats.WinningCompressorName = JobContext.CompressionName;
		CompressorStats.CurrentSize = NewSize;
		CompressorStats.WinningCompressorSavings = MemorySavingsFromOriginal;
		CompressorStats.WinningCompressorError = JobContext.ErrorStats.MaxError;
	}

	ClearAnimCompressionJobs(AnimCompressionTask_CompletionEvents, AnimCompressionJobContexes);
}
#endif	// WITH_EDITORONLY_DATA

// Simple struct to calc duration of of scope
struct FCompressionTimeElapsed
{
private:

	// Time we started tracking
	double StartTime;

	// Where to store the elapsed time
	double& Result;

public:
	FCompressionTimeElapsed(double& InResult)
		: StartTime(FPlatformTime::Seconds())
		, Result(InResult)
	{}

	~FCompressionTimeElapsed()
	{
		Result = FPlatformTime::Seconds() - StartTime;
	}
};

/**
 * Utility function to compress an animation. If the animation is currently associated with a codec, it will be used to 
 * compress the animation. Otherwise, the default codec will be used. If AllowAlternateCompressor is true, an
 * alternative compression codec will also be tested. If the alternative codec produces better compression and 
 * the accuracy of the compressed animation remains within tolerances, the alternative codec will be used. 
 * See GetAlternativeCompressionThreshold for information on the tolerance value used.
 *
 * @param	AnimSet		The animset to compress.
 * @param	AllowAlternateCompressor	true if an alternative compressor is permitted.
 * @param	bOutput		If false don't generate output or compute memory savings.
 * @return				None.
 */
void FAnimationUtils::CompressAnimSequence(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutCompressedData, FAnimCompressContext& CompressContext)
{
	if (FPlatformProperties::HasEditorOnlyData())
	{
		bool bOnlyCheckForMissingSkeletalMeshes = UAnimationSettings::Get()->bOnlyCheckForMissingSkeletalMeshes;
		if (!bOnlyCheckForMissingSkeletalMeshes)
		{
			UAnimationSettings* AnimSetting = UAnimationSettings::Get();
			bool bForceBelowThreshold = AnimSetting->bForceBelowThreshold;
			bool bFirstRecompressUsingCurrentOrDefault = AnimSetting->bFirstRecompressUsingCurrentOrDefault;
			bool bRaiseMaxErrorToExisting = AnimSetting->bRaiseMaxErrorToExisting;
			// If we don't allow alternate compressors, and just want to recompress with default/existing, then make sure we do so.
			if( !CompressContext.bAllowAlternateCompressor )
			{
				bFirstRecompressUsingCurrentOrDefault = true;
			}

			bool bTryExhaustiveSearch = AnimSetting->bTryExhaustiveSearch;
			bool bEnableSegmenting = AnimSetting->bEnableSegmenting;
			int32 IdealNumFramesPerSegment = 64;
			int32 MaxNumFramesPerSegment = (IdealNumFramesPerSegment * 2) - 1;

#if WITH_EDITORONLY_DATA
			UAnimCompress_Automatic* AutoCompressionScheme = Cast<UAnimCompress_Automatic>(CompressibleAnimData.RequestedCompressionScheme);

			if (AutoCompressionScheme != nullptr)
			{
				bTryExhaustiveSearch = AutoCompressionScheme->bTryExhaustiveSearch;
				bEnableSegmenting = AutoCompressionScheme->bEnableSegmenting;
				IdealNumFramesPerSegment = AutoCompressionScheme->IdealNumFramesPerSegment;
				MaxNumFramesPerSegment = AutoCompressionScheme->MaxNumFramesPerSegment;
			}
			else if (CompressibleAnimData.RequestedCompressionScheme != nullptr)
			{
				bTryExhaustiveSearch = AnimSetting->bTryExhaustiveSearch;
			}
#endif

			double CompressionTime = 0.0;
			{
				//Scoped timing of compression, make sure nothing else is added to this scope
				FCompressionTimeElapsed TimeTracker(CompressionTime);
				CompressAnimSequenceExplicit(
					CompressibleAnimData,
					OutCompressedData,
					CompressContext,
					CompressibleAnimData.AltCompressionErrorThreshold,
					bFirstRecompressUsingCurrentOrDefault,
					bForceBelowThreshold,
					bRaiseMaxErrorToExisting,
					bTryExhaustiveSearch,
					bEnableSegmenting,
					IdealNumFramesPerSegment,
					MaxNumFramesPerSegment);
			}

			CompressContext.GatherPostCompressionStats(CompressibleAnimData, OutCompressedData, CompressionTime);
		}
	}
}

/**
 * Utility function to compress an animation. If the animation is currently associated with a codec, it will be used to 
 * compress the animation. Otherwise, the default codec will be used. If AllowAlternateCompressor is true, an
 * alternative compression codec will also be tested. If the alternative codec produces better compression and 
 * the accuracy of the compressed animation remains within tolerances, the alternative codec will be used. 
 * See GetAlternativeCompressionThreshold for information on the tolerance value used.
 *
 * @param	AnimSet		The animset to compress.
 * @param	MasterTolerance	The alternate error threshold (0.0 means don't try anything other than the current / default scheme)
 * @param	bOutput		If false don't generate output or compute memory savings.
 * @return				None.
 */
void FAnimationUtils::CompressAnimSequenceExplicit(
	const FCompressibleAnimData& CompressibleAnimData,
	FCompressibleAnimDataResult& OutCompressedData,
	FAnimCompressContext& CompressContext,
	float MasterTolerance,
	bool bFirstRecompressUsingCurrentOrDefault,
	bool bForceBelowThreshold,
	bool bRaiseMaxErrorToExisting,
	bool bTryExhaustiveSearch,
	bool bEnableSegmenting,
	int32 IdealNumFramesPerSegment,
	int32 MaxNumFramesPerSegment)
{
#if WITH_EDITORONLY_DATA
	DECLARE_ANIM_COMP_ALGORITHM(BitwiseACF_Float96);
	DECLARE_ANIM_COMP_ALGORITHM(BitwiseACF_Fixed48);
	DECLARE_ANIM_COMP_ALGORITHM(BitwiseACF_IntervalFixed32);
	DECLARE_ANIM_COMP_ALGORITHM(BitwiseACF_Fixed32);

	DECLARE_ANIM_COMP_ALGORITHM(HalfOddACF_Float96);
	DECLARE_ANIM_COMP_ALGORITHM(HalfOddACF_Fixed48);
	DECLARE_ANIM_COMP_ALGORITHM(HalfOddACF_IntervalFixed32);
	DECLARE_ANIM_COMP_ALGORITHM(HalfOddACF_Fixed32);

	DECLARE_ANIM_COMP_ALGORITHM(HalfEvenACF_Float96);
	DECLARE_ANIM_COMP_ALGORITHM(HalfEvenACF_Fixed48);
	DECLARE_ANIM_COMP_ALGORITHM(HalfEvenACF_IntervalFixed32);
	DECLARE_ANIM_COMP_ALGORITHM(HalfEvenACF_Fixed32);

	DECLARE_ANIM_COMP_ALGORITHM(LinearACF_Float96);
	DECLARE_ANIM_COMP_ALGORITHM(LinearACF_Fixed48);
	DECLARE_ANIM_COMP_ALGORITHM(LinearACF_IntervalFixed32);
	DECLARE_ANIM_COMP_ALGORITHM(LinearACF_Fixed32);

	DECLARE_ANIM_COMP_ALGORITHM(Progressive_PerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Bitwise_PerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Linear_PerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Adaptive1_LinPerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Adaptive1_LinPerTrackNoRT);

	DECLARE_ANIM_COMP_ALGORITHM(Adaptive2_PerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Adaptive2_LinPerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Adaptive2_LinPerTrackNoRT);

	DECLARE_ANIM_COMP_ALGORITHM(Downsample20Hz_PerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Downsample15Hz_PerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Downsample10Hz_PerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Downsample5Hz_PerTrack);

	DECLARE_ANIM_COMP_ALGORITHM(Adaptive1_15Hz_LinPerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Adaptive1_10Hz_LinPerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Adaptive1_5Hz_LinPerTrack);

	DECLARE_ANIM_COMP_ALGORITHM(Adaptive2_15Hz_LinPerTrack);
	DECLARE_ANIM_COMP_ALGORITHM(Adaptive2_10Hz_LinPerTrack);

	DECLARE_ANIM_COMP_ALGORITHM(Linear_PerTrackExp1);
	DECLARE_ANIM_COMP_ALGORITHM(Linear_PerTrackExp2);

	USkeleton* Skeleton = CompressibleAnimData.Skeleton;
	check (Skeleton);
	if (Skeleton->HasAnyFlags(RF_NeedLoad))
	{
		Skeleton->GetLinker()->Preload(Skeleton);
	}

	static int32 TotalRecompressions = 0;

	static int32 TotalNoWinnerRounds = 0;

	static int32 AlternativeCompressorLossesFromSize = 0;
	static int32 AlternativeCompressorLossesFromError = 0;
	static int32 AlternativeCompressorSavings = 0;
	static int64 TotalSizeBefore = 0;
	static int64 TotalSizeNow = 0;
	static int64 TotalUncompressed = 0;

	const int32 NumRawDataTracks = CompressibleAnimData.RawAnimationData.Num();

	// we must have raw data to continue
	if(NumRawDataTracks > 0 )
	{
		// See if we're trying alternate compressors
		// If compression Scheme is automatic, then we definitely want to 'TryAlternateCompressor'.
		const bool bIsAutomatic = CompressibleAnimData.RequestedCompressionScheme && CompressibleAnimData.RequestedCompressionScheme->IsA(UAnimCompress_Automatic::StaticClass());
		const bool bTryAlternateCompressor = bIsAutomatic || CompressContext.bAllowAlternateCompressor;

		// We shouldn't override as this value can come from automatic compression sequences
		// but that was broken by CL 3489273
		// Preserving override behaviour till issue can be properly addressed
		MasterTolerance = CompressibleAnimData.AltCompressionErrorThreshold;

		AnimationErrorStats TrueOriginalErrorStats;
		FAnimationUtils::ComputeCompressionError(CompressibleAnimData, OutCompressedData, TrueOriginalErrorStats);

		AnimationErrorStats OriginalErrorStats;

		int32 AfterOriginalRecompression = 0;
		if( (bFirstRecompressUsingCurrentOrDefault && !bTryAlternateCompressor) || (OutCompressedData.CompressedByteStream.Num() == 0))
		{
			UAnimCompress* OriginalCompressionAlgorithm = CompressibleAnimData.RequestedCompressionScheme ? CompressibleAnimData.RequestedCompressionScheme : FAnimationUtils::GetDefaultAnimationCompressionAlgorithm();

			// Automatic compression brings us back here, so don't create an infinite loop and pick bitwise compress instead.
			if (!OriginalCompressionAlgorithm || OriginalCompressionAlgorithm->IsA(UAnimCompress_Automatic::StaticClass()))
			{
				UAnimCompress* CompressionAlgorithm = NewObject<UAnimCompress_BitwiseCompressOnly>();
				if (OriginalCompressionAlgorithm != nullptr)
				{
					// Keep the same segmenting settings
					CompressionAlgorithm->bEnableSegmenting = OriginalCompressionAlgorithm->bEnableSegmenting;
					CompressionAlgorithm->IdealNumFramesPerSegment = OriginalCompressionAlgorithm->IdealNumFramesPerSegment;
					CompressionAlgorithm->MaxNumFramesPerSegment = OriginalCompressionAlgorithm->MaxNumFramesPerSegment;
				}

				OriginalCompressionAlgorithm = CompressionAlgorithm;
			}

			UE_LOG(LogAnimationCompression, Log, TEXT("Recompressing (%s) using current/default (%s) bFirstRecompressUsingCurrentOrDefault(%d) bTryAlternateCompressor(%d) IsCompressedDataValid(%d)"),
				*CompressibleAnimData.FullName,
				*OriginalCompressionAlgorithm->GetName(),
				bFirstRecompressUsingCurrentOrDefault,
				bTryAlternateCompressor,
				OutCompressedData.IsCompressedDataValid());

			OriginalCompressionAlgorithm->Reduce(CompressibleAnimData, OutCompressedData);
			
			AfterOriginalRecompression = OutCompressedData.GetApproxBoneCompressedSize();

			// figure out our current compression error
			FAnimationUtils::ComputeCompressionError(CompressibleAnimData, OutCompressedData, OriginalErrorStats);
		}
		else
		{
			AfterOriginalRecompression = OutCompressedData.GetApproxBoneCompressedSize();
			OriginalErrorStats = TrueOriginalErrorStats;
		}

		//For logging
		FString OriginalKeyEncodingFormat = GetAnimationKeyFormatString(static_cast<AnimationKeyFormat>(OutCompressedData.KeyEncodingFormat));
		FString OriginalRotationFormat = FAnimationUtils::GetAnimationCompressionFormatString(static_cast<AnimationCompressionFormat>(OutCompressedData.TranslationCompressionFormat));
		FString OriginalTranslationFormat = FAnimationUtils::GetAnimationCompressionFormatString(static_cast<AnimationCompressionFormat>(OutCompressedData.RotationCompressionFormat));

		// Get the current size
		SIZE_T OriginalSize = AfterOriginalRecompression;
		TotalSizeBefore += OriginalSize;

		// Estimate total uncompressed
		TotalUncompressed += ((sizeof(FVector) + sizeof(FQuat) + sizeof(FVector)) * NumRawDataTracks * CompressibleAnimData.NumFrames);

		// Check for global permission to try an alternative compressor
		// And it's valid to manually recompress animations
		if( bTryAlternateCompressor )
		{
			ensure(OutCompressedData.CompressedByteStream.Num() > 0);

			AnimationErrorStats NewErrorStats = OriginalErrorStats;
			if (bRaiseMaxErrorToExisting)
			{
				if (NewErrorStats.MaxError > MasterTolerance)
				{
					UE_LOG(LogAnimationCompression, Log, TEXT("  Boosting MasterTolerance to %f, as existing MaxDiff was higher than %f and bRaiseMaxErrorToExisting=true"), NewErrorStats.MaxError, MasterTolerance);
					MasterTolerance = NewErrorStats.MaxError;
				}
			}

			// count all attempts for debugging
			++TotalRecompressions;

			WinningCompressorStats CompressorStats;
			CompressorStats.CurrentSize = OriginalSize;
			CompressorStats.WinningCompressorError = OriginalErrorStats.MaxError;

			FGraphEventArray AnimCompressionTask_CompletionEvents;
			TArray<FAnimCompressionJobContext*> AnimCompressionJobContexes;

			if (!bTryExhaustiveSearch)
			{
				// Dispatch our async compression
				{
					{
						// Adaptive error through probing the effect of perturbations at each track
						UAnimCompress_PerTrackCompression* NewPerTrackCompressor = NewObject<UAnimCompress_PerTrackCompression>();
						NewPerTrackCompressor->bUseAdaptiveError2 = true;
						NewPerTrackCompressor->MaxPosDiffBitwise = 0.05f;
						NewPerTrackCompressor->MaxAngleDiffBitwise = 0.02f;
						NewPerTrackCompressor->MaxScaleDiffBitwise = 0.00005f;

						TRYCOMPRESSION_ASYNC(Adaptive2_PerTrack, NewPerTrackCompressor);

						NewPerTrackCompressor->bActuallyFilterLinearKeys = true;
						NewPerTrackCompressor->bRetarget = true;
						TRYCOMPRESSION_ASYNC(Adaptive2_LinPerTrack, NewPerTrackCompressor);

						NewPerTrackCompressor->bActuallyFilterLinearKeys = true;
						NewPerTrackCompressor->bRetarget = false;
						TRYCOMPRESSION_ASYNC(Adaptive2_LinPerTrackNoRT, NewPerTrackCompressor);
					}

					{
						UAnimCompress_PerTrackCompression* PerTrackCompressor = NewObject<UAnimCompress_PerTrackCompression>();
						PerTrackCompressor->bUseAdaptiveError = true;

						if (CompressibleAnimData.NumFrames > 1)
						{
							PerTrackCompressor->bActuallyFilterLinearKeys = true;
							PerTrackCompressor->bRetarget = true;

							PerTrackCompressor->MaxPosDiff = 0.1f;
							// 						PerTrackCompressor->MaxAngleDiff = 0.1;
							PerTrackCompressor->MaxScaleDiff = 0.00001f;
							PerTrackCompressor->ParentingDivisor = 2.0f;
							PerTrackCompressor->ParentingDivisorExponent = 1.0f;
							TRYCOMPRESSION_ASYNC(Linear_PerTrackExp1, PerTrackCompressor);
						}
					}

					{
						UAnimCompress_PerTrackCompression* PerTrackCompressor = NewObject<UAnimCompress_PerTrackCompression>();

						// Straight PerTrackCompression, no key decimation and no linear key removal
						TRYCOMPRESSION_ASYNC(Bitwise_PerTrack, PerTrackCompressor);
						PerTrackCompressor->bUseAdaptiveError = true;

						// Full blown linear
						PerTrackCompressor->bActuallyFilterLinearKeys = true;
						PerTrackCompressor->bRetarget = true;
						TRYCOMPRESSION_ASYNC(Linear_PerTrack, PerTrackCompressor);

						// Adaptive retargetting based on height within the skeleton
						PerTrackCompressor->bActuallyFilterLinearKeys = true;
						PerTrackCompressor->bRetarget = false;
						PerTrackCompressor->ParentingDivisor = 2.0f;
						PerTrackCompressor->ParentingDivisorExponent = 1.6f;
						TRYCOMPRESSION_ASYNC(Adaptive1_LinPerTrackNoRT, PerTrackCompressor);
					}

					{
						UAnimCompress_PerTrackCompression* PerTrackCompressor = NewObject<UAnimCompress_PerTrackCompression>();
						PerTrackCompressor->bUseAdaptiveError = true;

						// Try the decimation algorithms
						if (CompressibleAnimData.NumFrames >= PerTrackCompressor->MinKeysForResampling)
						{
							PerTrackCompressor->bActuallyFilterLinearKeys = false;
							PerTrackCompressor->bRetarget = false;
							PerTrackCompressor->bUseAdaptiveError = false;
							PerTrackCompressor->bResampleAnimation = true;

							// Try PerTrackCompression, downsample to 5 Hz
							PerTrackCompressor->ResampledFramerate = 5.0f;
							TRYCOMPRESSION_ASYNC(Downsample5Hz_PerTrack, PerTrackCompressor);
						}
					}

					if (CompressibleAnimData.NumFrames > 1)
					{
						UAnimCompress_RemoveLinearKeys* LinearKeyRemover = NewObject<UAnimCompress_RemoveLinearKeys>();
						{
							// Try ACF_Float96NoW
							LinearKeyRemover->RotationCompressionFormat = ACF_Float96NoW;
							LinearKeyRemover->TranslationCompressionFormat = ACF_None;
							TRYCOMPRESSION_ASYNC(LinearACF_Float96, LinearKeyRemover);
						}
					}

					{
						UAnimCompress_BitwiseCompressOnly* BitwiseCompressor = NewObject<UAnimCompress_BitwiseCompressOnly>();

						// Try ACF_Float96NoW
						BitwiseCompressor->RotationCompressionFormat = ACF_Float96NoW;
						BitwiseCompressor->TranslationCompressionFormat = ACF_None;
						TRYCOMPRESSION_ASYNC(BitwiseACF_Float96, BitwiseCompressor);

						// Try ACF_Fixed48NoW
						BitwiseCompressor->RotationCompressionFormat = ACF_Fixed48NoW;
						BitwiseCompressor->TranslationCompressionFormat = ACF_None;
						TRYCOMPRESSION_ASYNC(BitwiseACF_Fixed48, BitwiseCompressor);
					}
				}

				WaitForAnimCompressionJobs(AnimCompressionTask_CompletionEvents);
				UpdateAnimCompressionFromAsyncJobs(OutCompressedData, AnimCompressionTask_CompletionEvents, AnimCompressionJobContexes, OriginalSize, CompressorStats, MasterTolerance);
			}
			else
			{
				// Prepare to compress
				UE_LOG(LogAnimationCompression, Log, TEXT("Compressing %s (%s)\n\tSkeleton: %s\n\tOriginal Size: %i   MaxDiff: %f"),
					*CompressibleAnimData.Name,
					*CompressibleAnimData.FullName,
					Skeleton ? *Skeleton->GetFName().ToString() : TEXT("NULL - Not all compression techniques can be used!"),
					OriginalSize,
					TrueOriginalErrorStats.MaxError);

				UE_LOG(LogAnimationCompression, Log, TEXT("Original Key Encoding: %s\n\tOriginal Rotation Format: %s\n\tOriginal Translation Format: %s\n\tNumFrames: %i\n\tSequenceLength: %f (%2.1f fps)"),
					*OriginalKeyEncodingFormat,
					*OriginalRotationFormat,
					*OriginalTranslationFormat,
					OutCompressedData.CompressedNumberOfFrames,
					CompressibleAnimData.SequenceLength,
					(OutCompressedData.CompressedNumberOfFrames > 1) ? (OutCompressedData.CompressedNumberOfFrames -1) / CompressibleAnimData.SequenceLength : DEFAULT_SAMPLERATE);

				if (bFirstRecompressUsingCurrentOrDefault)
				{
					UE_LOG(LogAnimationCompression, Log, TEXT("Recompressed using current/default\n\tRecompress Size: %i   MaxDiff: %f\n\tRecompress Scheme: %s"),
						AfterOriginalRecompression,
						OriginalErrorStats.MaxError,
						OutCompressedData.CompressionScheme ? *OutCompressedData.CompressionScheme->GetClass()->GetName() : TEXT("NULL"));
				}

				// Progressive Algorithm
				{
					UAnimCompress_PerTrackCompression* PerTrackCompressor = NewObject<UAnimCompress_PerTrackCompression>();

					// Start not too aggressive
// 					PerTrackCompressor->MaxPosDiffBitwise /= 10.f;
// 					PerTrackCompressor->MaxAngleDiffBitwise /= 10.f;
// 					PerTrackCompressor->MaxScaleDiffBitwise /= 10.f;
					PerTrackCompressor->bUseAdaptiveError2 = true;

					// Try default compressor first
					TRYCOMPRESSION(Progressive_PerTrack, PerTrackCompressor);

					if( NewErrorStats.MaxError >= MasterTolerance )
					{
						UE_LOG(LogAnimationCompression, Log, TEXT("\tStandard bitwise compressor too aggressive, lower default settings."));
					}
					else
					{
						// First, start by finding most downsampling factor.
						if (CompressibleAnimData.NumFrames >= PerTrackCompressor->MinKeysForResampling)
						{
							PerTrackCompressor->bResampleAnimation = true;
				
							// Try PerTrackCompression, down sample to 5 Hz
							PerTrackCompressor->ResampledFramerate = 5.0f;
							UE_LOG(LogAnimationCompression, Log, TEXT("\tResampledFramerate: %f"), PerTrackCompressor->ResampledFramerate);
							TRYCOMPRESSION(Progressive_PerTrack, PerTrackCompressor);

							// If too much error, try 6Hz
							if( NewErrorStats.MaxError >= MasterTolerance )
							{
								PerTrackCompressor->ResampledFramerate = 6.0f;
								UE_LOG(LogAnimationCompression, Log, TEXT("\tResampledFramerate: %f"), PerTrackCompressor->ResampledFramerate);
								TRYCOMPRESSION(Progressive_PerTrack, PerTrackCompressor);

								// if too much error go 10Hz, 15Hz, 20Hz.
								if( NewErrorStats.MaxError >= MasterTolerance )
								{
									PerTrackCompressor->ResampledFramerate = 5.0f;
									// Keep trying until we find something that works (or we just don't downsample)
									while( PerTrackCompressor->ResampledFramerate < 20.f && NewErrorStats.MaxError >= MasterTolerance )
									{
										PerTrackCompressor->ResampledFramerate += 5.f;
										UE_LOG(LogAnimationCompression, Log, TEXT("\tResampledFramerate: %f"), PerTrackCompressor->ResampledFramerate);
										TRYCOMPRESSION(Progressive_PerTrack, PerTrackCompressor);
									}
								}
							}
							
							// Give up downsampling if it didn't work.
							if( NewErrorStats.MaxError >= MasterTolerance )
							{
								UE_LOG(LogAnimationCompression, Log, TEXT("\tDownsampling didn't work."));
								PerTrackCompressor->bResampleAnimation = false;
							}
						}
						
						// Now do Linear Key Removal
						if (CompressibleAnimData.NumFrames > 1)
						{
							PerTrackCompressor->bActuallyFilterLinearKeys = true;
							PerTrackCompressor->bRetarget = true;
							
							int32 const TestSteps = 16;
							float const MaxScale = 2^(TestSteps);

							// Start with the least aggressive first. if that one doesn't succeed, don't bother going through all the steps.
							PerTrackCompressor->MaxPosDiff /= MaxScale;
							PerTrackCompressor->MaxAngleDiff /= MaxScale;
							PerTrackCompressor->MaxScaleDiff /= MaxScale;
							PerTrackCompressor->MaxEffectorDiff /= MaxScale;
							PerTrackCompressor->MinEffectorDiff /= MaxScale;
							PerTrackCompressor->EffectorDiffSocket /= MaxScale;
							UE_LOG(LogAnimationCompression, Log, TEXT("\tLinearKeys. MaxPosDiff: %f, MaxAngleDiff: %f, MaxScaleDiff : %f"), PerTrackCompressor->MaxPosDiff, PerTrackCompressor->MaxAngleDiff, PerTrackCompressor->MaxScaleDiff);
							TRYCOMPRESSION(Progressive_PerTrack, PerTrackCompressor);							
							PerTrackCompressor->MaxPosDiff *= MaxScale;
							PerTrackCompressor->MaxAngleDiff *= MaxScale;
							PerTrackCompressor->MaxScaleDiff *= MaxScale;
							PerTrackCompressor->MaxEffectorDiff *= MaxScale;
							PerTrackCompressor->MinEffectorDiff *= MaxScale;
							PerTrackCompressor->EffectorDiffSocket *= MaxScale;

							if( NewErrorStats.MaxError < MasterTolerance )
							{
								// Start super aggressive, and go down until we find something that works.
								UE_LOG(LogAnimationCompression, Log, TEXT("\tLinearKeys. MaxPosDiff: %f, MaxAngleDiff: %f, MaxScaleDiff : %f"), PerTrackCompressor->MaxPosDiff, PerTrackCompressor->MaxAngleDiff, PerTrackCompressor->MaxScaleDiff);
								TRYCOMPRESSION(Progressive_PerTrack, PerTrackCompressor);

								for(int32 Step=0; Step<TestSteps && (NewErrorStats.MaxError >= MasterTolerance); Step++)
								{
									PerTrackCompressor->MaxPosDiff /= 2.f;
									PerTrackCompressor->MaxAngleDiff /= 2.f;
									PerTrackCompressor->MaxScaleDiff /= 2.f;
									PerTrackCompressor->MaxEffectorDiff /= 2.f;
									PerTrackCompressor->MinEffectorDiff /= 2.f;
									PerTrackCompressor->EffectorDiffSocket /= 2.f;
									UE_LOG(LogAnimationCompression, Log, TEXT("\tLinearKeys. MaxPosDiff: %f, MaxAngleDiff: %f, MaxScaleDiff : %f"), PerTrackCompressor->MaxPosDiff, PerTrackCompressor->MaxAngleDiff, PerTrackCompressor->MaxScaleDiff);
									TRYCOMPRESSION(Progressive_PerTrack, PerTrackCompressor);
								}
							}

							// Give up Linear Key Compression if it didn't work
							if( NewErrorStats.MaxError >= MasterTolerance )
							{
								PerTrackCompressor->bActuallyFilterLinearKeys = false;
								PerTrackCompressor->bRetarget = false;
							}
						}

						// Finally tighten up bitwise compression
						PerTrackCompressor->MaxPosDiffBitwise *= 10.f;
						PerTrackCompressor->MaxAngleDiffBitwise *= 10.f;
						PerTrackCompressor->MaxScaleDiffBitwise *= 10.f;
						{
							int32 const TestSteps = 16;
							float const MaxScale = 2^(TestSteps/2);

							PerTrackCompressor->MaxPosDiffBitwise *= MaxScale;
							PerTrackCompressor->MaxAngleDiffBitwise *= MaxScale;
							PerTrackCompressor->MaxScaleDiffBitwise *= MaxScale;
							UE_LOG(LogAnimationCompression, Log, TEXT("\tBitwise. MaxPosDiffBitwise: %f, MaxAngleDiffBitwise: %f, MaxScaleDiffBitwise: %f"), PerTrackCompressor->MaxPosDiffBitwise, PerTrackCompressor->MaxAngleDiffBitwise, PerTrackCompressor->MaxScaleDiffBitwise);
							TRYCOMPRESSION(Progressive_PerTrack, PerTrackCompressor);
							PerTrackCompressor->MaxPosDiffBitwise /= 2.f;
							PerTrackCompressor->MaxAngleDiffBitwise /= 2.f;
							PerTrackCompressor->MaxScaleDiffBitwise /= 2.f;
							for(int32 Step=0; Step<TestSteps && (NewErrorStats.MaxError >= MasterTolerance) && (PerTrackCompressor->MaxPosDiffBitwise >= PerTrackCompressor->MaxZeroingThreshold); Step++)
							{
								UE_LOG(LogAnimationCompression, Log, TEXT("\tBitwise. MaxPosDiffBitwise: %f, MaxAngleDiffBitwise: %f, MaxScaleDiffBitwise: %f"), PerTrackCompressor->MaxPosDiffBitwise, PerTrackCompressor->MaxAngleDiffBitwise, PerTrackCompressor->MaxScaleDiffBitwise);
								TRYCOMPRESSION(Progressive_PerTrack, PerTrackCompressor);
								PerTrackCompressor->MaxPosDiffBitwise /= 2.f;
								PerTrackCompressor->MaxAngleDiffBitwise /= 2.f;
								PerTrackCompressor->MaxScaleDiffBitwise /= 2.f;
							}
						}
					}
				}

				// Start with Bitwise Compress only
				{
					UAnimCompress_BitwiseCompressOnly* BitwiseCompressor = NewObject<UAnimCompress_BitwiseCompressOnly>();

					// Try ACF_Float96NoW
					BitwiseCompressor->RotationCompressionFormat = ACF_Float96NoW;
					BitwiseCompressor->TranslationCompressionFormat = ACF_None;
					TRYCOMPRESSION_ASYNC(BitwiseACF_Float96, BitwiseCompressor);

					// Try ACF_Fixed48NoW
					BitwiseCompressor->RotationCompressionFormat = ACF_Fixed48NoW;
					BitwiseCompressor->TranslationCompressionFormat = ACF_None;
					TRYCOMPRESSION_ASYNC(BitwiseACF_Fixed48,BitwiseCompressor);

// 32bits currently unusable due to creating too much error
// 					// Try ACF_IntervalFixed32NoW
// 					BitwiseCompressor->RotationCompressionFormat = ACF_IntervalFixed32NoW;
// 					BitwiseCompressor->TranslationCompressionFormat = ACF_None;
// 					TRYCOMPRESSION(BitwiseACF_IntervalFixed32,BitwiseCompressor);
// 
// 					// Try ACF_Fixed32NoW
// 					BitwiseCompressor->RotationCompressionFormat = ACF_Fixed32NoW;
// 					BitwiseCompressor->TranslationCompressionFormat = ACF_None;
// 					TRYCOMPRESSION(BitwiseACF_Fixed32,BitwiseCompressor);
				}

				// Start with Bitwise Compress only
				// this compressor has a minimum number of frames requirement. So no need to go there if we don't meet that...
				{
					UAnimCompress_RemoveEverySecondKey* RemoveEveryOtherKeyCompressor = NewObject<UAnimCompress_RemoveEverySecondKey>();
					if(CompressibleAnimData.NumFrames > RemoveEveryOtherKeyCompressor->MinKeys )
					{
						RemoveEveryOtherKeyCompressor->bStartAtSecondKey = false;
						{
							// Try ACF_Float96NoW
							RemoveEveryOtherKeyCompressor->RotationCompressionFormat = ACF_Float96NoW;	
							RemoveEveryOtherKeyCompressor->TranslationCompressionFormat = ACF_None;		
							TRYCOMPRESSION_ASYNC(HalfOddACF_Float96, RemoveEveryOtherKeyCompressor);

							// Try ACF_Fixed48NoW
							RemoveEveryOtherKeyCompressor->RotationCompressionFormat = ACF_Fixed48NoW;	
							RemoveEveryOtherKeyCompressor->TranslationCompressionFormat = ACF_None;		
							TRYCOMPRESSION_ASYNC(HalfOddACF_Fixed48, RemoveEveryOtherKeyCompressor);

// 32bits currently unusable due to creating too much error
// 							// Try ACF_IntervalFixed32NoW
// 							RemoveEveryOtherKeyCompressor->RotationCompressionFormat = ACF_IntervalFixed32NoW;	
// 							RemoveEveryOtherKeyCompressor->TranslationCompressionFormat = ACF_None;		
// 							TRYCOMPRESSION(HalfOddACF_IntervalFixed32, RemoveEveryOtherKeyCompressor);
// 
// 							// Try ACF_Fixed32NoW
// 							RemoveEveryOtherKeyCompressor->RotationCompressionFormat = ACF_Fixed32NoW;	
// 							RemoveEveryOtherKeyCompressor->TranslationCompressionFormat = ACF_None;		
// 							TRYCOMPRESSION(HalfOddACF_Fixed32, RemoveEveryOtherKeyCompressor);
						}
						RemoveEveryOtherKeyCompressor->bStartAtSecondKey = true;
						{
							// Try ACF_Float96NoW
							RemoveEveryOtherKeyCompressor->RotationCompressionFormat = ACF_Float96NoW;	
							RemoveEveryOtherKeyCompressor->TranslationCompressionFormat = ACF_None;		
							TRYCOMPRESSION_ASYNC(HalfEvenACF_Float96,RemoveEveryOtherKeyCompressor);

							// Try ACF_Fixed48NoW
							RemoveEveryOtherKeyCompressor->RotationCompressionFormat = ACF_Fixed48NoW;	
							RemoveEveryOtherKeyCompressor->TranslationCompressionFormat = ACF_None;		
							TRYCOMPRESSION_ASYNC(HalfEvenACF_Fixed48,RemoveEveryOtherKeyCompressor);

// 32bits currently unusable due to creating too much error
// 							// Try ACF_IntervalFixed32NoW
// 							RemoveEveryOtherKeyCompressor->RotationCompressionFormat = ACF_IntervalFixed32NoW;	
// 							RemoveEveryOtherKeyCompressor->TranslationCompressionFormat = ACF_None;		
// 							TRYCOMPRESSION(HalfEvenACF_IntervalFixed32,RemoveEveryOtherKeyCompressor);
// 
// 							// Try ACF_Fixed32NoW
// 							RemoveEveryOtherKeyCompressor->RotationCompressionFormat = ACF_Fixed32NoW;	
// 							RemoveEveryOtherKeyCompressor->TranslationCompressionFormat = ACF_None;		
// 							TRYCOMPRESSION(HalfEvenACF_Fixed32,RemoveEveryOtherKeyCompressor);
						}
					}
				}

				// construct the proposed compressor		
				if(CompressibleAnimData.NumFrames > 1 )
				{
					UAnimCompress_RemoveLinearKeys* LinearKeyRemover = NewObject<UAnimCompress_RemoveLinearKeys>();
					{
						// Try ACF_Float96NoW
						LinearKeyRemover->RotationCompressionFormat = ACF_Float96NoW;
						LinearKeyRemover->TranslationCompressionFormat = ACF_None;	
						TRYCOMPRESSION_ASYNC(LinearACF_Float96,LinearKeyRemover);

						// Try ACF_Fixed48NoW
						LinearKeyRemover->RotationCompressionFormat = ACF_Fixed48NoW;
						LinearKeyRemover->TranslationCompressionFormat = ACF_None;	
						TRYCOMPRESSION_ASYNC(LinearACF_Fixed48,LinearKeyRemover);

// Error is too bad w/ 32bits
// 						// Try ACF_IntervalFixed32NoW
// 						LinearKeyRemover->RotationCompressionFormat = ACF_IntervalFixed32NoW;
// 						LinearKeyRemover->TranslationCompressionFormat = ACF_None;
// 						TRYCOMPRESSION(LinearACF_IntervalFixed32,LinearKeyRemover);
// 
// 						// Try ACF_Fixed32NoW
// 						LinearKeyRemover->RotationCompressionFormat = ACF_Fixed32NoW;
// 						LinearKeyRemover->TranslationCompressionFormat = ACF_None;
// 						TRYCOMPRESSION(LinearACF_Fixed32,LinearKeyRemover);
					}
				}

				{
					UAnimCompress_PerTrackCompression* PerTrackCompressor = NewObject<UAnimCompress_PerTrackCompression>();

					// Straight PerTrackCompression, no key decimation and no linear key removal
					TRYCOMPRESSION_ASYNC(Bitwise_PerTrack, PerTrackCompressor);
					PerTrackCompressor->bUseAdaptiveError = true;

					// Full blown linear
					PerTrackCompressor->bActuallyFilterLinearKeys = true;
					PerTrackCompressor->bRetarget = true;
					TRYCOMPRESSION_ASYNC(Linear_PerTrack, PerTrackCompressor);

					// Adaptive retargetting based on height within the skeleton
					PerTrackCompressor->bActuallyFilterLinearKeys = true;
					PerTrackCompressor->bRetarget = false;
					PerTrackCompressor->ParentingDivisor = 2.0f;
					PerTrackCompressor->ParentingDivisorExponent = 1.6f;
					TRYCOMPRESSION_ASYNC(Adaptive1_LinPerTrackNoRT, PerTrackCompressor);
					PerTrackCompressor->ParentingDivisor = 1.0f;
					PerTrackCompressor->ParentingDivisorExponent = 1.0f;

					PerTrackCompressor->bActuallyFilterLinearKeys = true;
					PerTrackCompressor->bRetarget = true;
					PerTrackCompressor->ParentingDivisor = 2.0f;
					PerTrackCompressor->ParentingDivisorExponent = 1.6f;
					TRYCOMPRESSION_ASYNC(Adaptive1_LinPerTrack, PerTrackCompressor);
					PerTrackCompressor->ParentingDivisor = 1.0f;
					PerTrackCompressor->ParentingDivisorExponent = 1.0f;
				}


				{
					UAnimCompress_PerTrackCompression* PerTrackCompressor = NewObject<UAnimCompress_PerTrackCompression>();
					PerTrackCompressor->bUseAdaptiveError = true;

					if (CompressibleAnimData.NumFrames > 1 )
					{
						PerTrackCompressor->bActuallyFilterLinearKeys = true;
						PerTrackCompressor->bRetarget = true;

						PerTrackCompressor->MaxPosDiff = 0.1f;
// 						PerTrackCompressor->MaxAngleDiff = 0.1f;
						PerTrackCompressor->MaxScaleDiff = 0.00001f;
						PerTrackCompressor->ParentingDivisor = 2.0f;
						PerTrackCompressor->ParentingDivisorExponent = 1.0f;
						TRYCOMPRESSION_ASYNC(Linear_PerTrackExp1, PerTrackCompressor);

						PerTrackCompressor->MaxPosDiff = 0.01f;
// 						PerTrackCompressor->MaxAngleDiff = 0.025f;
						PerTrackCompressor->MaxScaleDiff = 0.000001f;
						PerTrackCompressor->ParentingDivisor = 2.0f;
						PerTrackCompressor->ParentingDivisorExponent = 1.0f;
						TRYCOMPRESSION_ASYNC(Linear_PerTrackExp2, PerTrackCompressor);

						PerTrackCompressor->bRetarget = false;
						PerTrackCompressor->MaxPosDiff = 0.1f;
// 						PerTrackCompressor->MaxAngleDiff = 0.025f;
						PerTrackCompressor->MaxScaleDiff = 0.00001f;
						PerTrackCompressor->ParentingDivisor = 1.0f;
						PerTrackCompressor->ParentingDivisorExponent = 1.0f;
					}
				}

				{
					UAnimCompress_PerTrackCompression* PerTrackCompressor = NewObject<UAnimCompress_PerTrackCompression>();
					PerTrackCompressor->bUseAdaptiveError = true;

					// Try the decimation algorithms
					if (CompressibleAnimData.NumFrames >= PerTrackCompressor->MinKeysForResampling)
					{
						PerTrackCompressor->bActuallyFilterLinearKeys = false;
						PerTrackCompressor->bRetarget = false;
						PerTrackCompressor->bUseAdaptiveError = false;
						PerTrackCompressor->bResampleAnimation = true;

						// Try PerTrackCompression, downsample to 20 Hz
						PerTrackCompressor->ResampledFramerate = 20.0f;
						TRYCOMPRESSION_ASYNC(Downsample20Hz_PerTrack, PerTrackCompressor);

						// Try PerTrackCompression, downsample to 15 Hz
						PerTrackCompressor->ResampledFramerate = 15.0f;
						TRYCOMPRESSION_ASYNC(Downsample15Hz_PerTrack, PerTrackCompressor);

						// Try PerTrackCompression, downsample to 10 Hz
						PerTrackCompressor->ResampledFramerate = 10.0f;
						TRYCOMPRESSION_ASYNC(Downsample10Hz_PerTrack, PerTrackCompressor);

						// Try PerTrackCompression, downsample to 5 Hz
						PerTrackCompressor->ResampledFramerate = 5.0f;
						TRYCOMPRESSION_ASYNC(Downsample5Hz_PerTrack, PerTrackCompressor);


						// Downsampling with linear key removal and adaptive error metrics
						PerTrackCompressor->bActuallyFilterLinearKeys = true;
						PerTrackCompressor->bRetarget = false;
						PerTrackCompressor->bUseAdaptiveError = true;
						PerTrackCompressor->ParentingDivisor = 2.0f;
						PerTrackCompressor->ParentingDivisorExponent = 1.6f;

						PerTrackCompressor->ResampledFramerate = 15.0f;
						TRYCOMPRESSION_ASYNC(Adaptive1_15Hz_LinPerTrack, PerTrackCompressor);

						PerTrackCompressor->ResampledFramerate = 10.0f;
						TRYCOMPRESSION_ASYNC(Adaptive1_10Hz_LinPerTrack, PerTrackCompressor);

						PerTrackCompressor->ResampledFramerate = 5.0f;
						TRYCOMPRESSION_ASYNC(Adaptive1_5Hz_LinPerTrack, PerTrackCompressor);
					}
				}


				{
					// Try the decimation algorithms
					if (CompressibleAnimData.NumFrames >= 3)
					{
						UAnimCompress_PerTrackCompression* NewPerTrackCompressor = NewObject<UAnimCompress_PerTrackCompression>();

						// Downsampling with linear key removal and adaptive error metrics v2
						NewPerTrackCompressor->MinKeysForResampling = 3;
						NewPerTrackCompressor->bUseAdaptiveError2 = true;
						NewPerTrackCompressor->MaxPosDiffBitwise = 0.05f;
						NewPerTrackCompressor->MaxAngleDiffBitwise = 0.02f;
						NewPerTrackCompressor->MaxScaleDiffBitwise = 0.00005f;
						NewPerTrackCompressor->bActuallyFilterLinearKeys = true;
						NewPerTrackCompressor->bRetarget = true;

						NewPerTrackCompressor->ResampledFramerate = 15.0f;
						TRYCOMPRESSION_ASYNC(Adaptive2_15Hz_LinPerTrack, NewPerTrackCompressor);

						NewPerTrackCompressor->ResampledFramerate = 10.0f;
						TRYCOMPRESSION_ASYNC(Adaptive2_10Hz_LinPerTrack, NewPerTrackCompressor);
					}
				}


				{
					// Adaptive error through probing the effect of perturbations at each track
					UAnimCompress_PerTrackCompression* NewPerTrackCompressor = NewObject<UAnimCompress_PerTrackCompression>();
					NewPerTrackCompressor->bUseAdaptiveError2 = true;
					NewPerTrackCompressor->MaxPosDiffBitwise = 0.05f;
					NewPerTrackCompressor->MaxAngleDiffBitwise = 0.02f;
					NewPerTrackCompressor->MaxScaleDiffBitwise = 0.00005f;

					TRYCOMPRESSION_ASYNC(Adaptive2_PerTrack, NewPerTrackCompressor);

					NewPerTrackCompressor->bActuallyFilterLinearKeys = true;
					NewPerTrackCompressor->bRetarget = true;
					TRYCOMPRESSION_ASYNC(Adaptive2_LinPerTrack, NewPerTrackCompressor);

					NewPerTrackCompressor->bActuallyFilterLinearKeys = true;
					NewPerTrackCompressor->bRetarget = false;
					TRYCOMPRESSION_ASYNC(Adaptive2_LinPerTrackNoRT, NewPerTrackCompressor);
				}

				WaitForAnimCompressionJobs(AnimCompressionTask_CompletionEvents);
				UpdateAnimCompressionFromAsyncJobs(OutCompressedData, AnimCompressionTask_CompletionEvents, AnimCompressionJobContexes, OriginalSize, CompressorStats, MasterTolerance);
			}

			// Increase winning compressor.
			{
				int32 SizeDecrease = OriginalSize - CompressorStats.CurrentSize;
				if (CompressorStats.WinningCompressorCounter)
				{
					++(*CompressorStats.WinningCompressorCounter);
					(*CompressorStats.WinningCompressorErrorSum) += CompressorStats.WinningCompressorError;
					AlternativeCompressorSavings += CompressorStats.WinningCompressorSavings;
					*CompressorStats.WinningCompressorMarginalSavingsSum += CompressorStats.WinningCompressorMarginalSavings;
					check(CompressorStats.WinningCompressorSavings == SizeDecrease);

					UE_LOG(LogAnimationCompression, Log, TEXT("  Recompressing(%s) with compressor('%s') saved %i bytes (%i -> %i -> %i) (max diff=%f)\n"),
						*CompressibleAnimData.Name,
						*CompressorStats.WinningCompressorName,
						SizeDecrease,
						OriginalSize, AfterOriginalRecompression,CompressorStats. CurrentSize,
						CompressorStats.WinningCompressorError);
				}
				else
				{
					UE_LOG(LogAnimationCompression, Log, TEXT("  No compressor suitable! Recompressing(%s) with original/default compressor(%s) saved %i bytes (%i -> %i -> %i) (max diff=%f)\n"), 
						*CompressibleAnimData.Name,
						*OutCompressedData.CompressionScheme->GetName(),
						SizeDecrease,
						OriginalSize, AfterOriginalRecompression, CompressorStats.CurrentSize,
						CompressorStats.WinningCompressorError);

					UE_LOG(LogAnimationCompression, Log, TEXT("  CompressedTrackOffsets(%d) CompressedByteStream(%d) CompressedScaleOffsets(%d) CompressedSegments(%d)"),
						OutCompressedData.CompressedTrackOffsets.Num(),
						OutCompressedData.CompressedByteStream.Num(),
						OutCompressedData.CompressedScaleOffsets.GetMemorySize(),
						0);

					TotalNoWinnerRounds++;
				}
			}

			// Make sure we got that right.
			check(CompressorStats.CurrentSize == OutCompressedData.GetApproxBoneCompressedSize());
			TotalSizeNow += CompressorStats.CurrentSize;

			CompressorStats.PctSaving = TotalSizeBefore > 0 ? 100.f - (100.f * float(TotalSizeNow) / float(TotalSizeBefore)) : 0.f;
			UE_LOG(LogAnimationCompression, Log, TEXT("Compression Stats Summary [Recompressions(%i) Bytes saved(%i) before(%i) now(%i) savings(%3.1f%%) Uncompressed(%i) TotalRatio(%i:1)]"), 
				TotalRecompressions,
				AlternativeCompressorSavings,
				TotalSizeBefore, 
				TotalSizeNow,
				CompressorStats.PctSaving,
				TotalUncompressed,
				(TotalUncompressed / TotalSizeNow));

			UE_LOG(LogAnimationCompression, Log, TEXT("\t\tDefault compressor wins:                      %i"), TotalNoWinnerRounds);

			{
				LOG_COMPRESSION_STATUS(BitwiseACF_Float96);
				LOG_COMPRESSION_STATUS(BitwiseACF_Fixed48);
// 				LOG_COMPRESSION_STATUS(BitwiseACF_IntervalFixed32);
// 				LOG_COMPRESSION_STATUS(BitwiseACF_Fixed32);
			}

			{
				LOG_COMPRESSION_STATUS(HalfOddACF_Float96);
				LOG_COMPRESSION_STATUS(HalfOddACF_Fixed48);
// 				LOG_COMPRESSION_STATUS(HalfOddACF_IntervalFixed32);
// 				LOG_COMPRESSION_STATUS(HalfOddACF_Fixed32);

				LOG_COMPRESSION_STATUS(HalfEvenACF_Float96);
				LOG_COMPRESSION_STATUS(HalfEvenACF_Fixed48);
// 				LOG_COMPRESSION_STATUS(HalfEvenACF_IntervalFixed32);
// 				LOG_COMPRESSION_STATUS(HalfEvenACF_Fixed32);
			}

			{
				LOG_COMPRESSION_STATUS(LinearACF_Float96);
				LOG_COMPRESSION_STATUS(LinearACF_Fixed48);
// 				LOG_COMPRESSION_STATUS(LinearACF_IntervalFixed32);
// 				LOG_COMPRESSION_STATUS(LinearACF_Fixed32);
			}

			{
				LOG_COMPRESSION_STATUS(Progressive_PerTrack);
				LOG_COMPRESSION_STATUS(Bitwise_PerTrack);
				LOG_COMPRESSION_STATUS(Linear_PerTrack);
				LOG_COMPRESSION_STATUS(Adaptive1_LinPerTrackNoRT);
				LOG_COMPRESSION_STATUS(Adaptive1_LinPerTrack);
					
				LOG_COMPRESSION_STATUS(Linear_PerTrackExp1);
				LOG_COMPRESSION_STATUS(Linear_PerTrackExp2);
			}

			{
				LOG_COMPRESSION_STATUS(Downsample20Hz_PerTrack);
				LOG_COMPRESSION_STATUS(Downsample15Hz_PerTrack);
				LOG_COMPRESSION_STATUS(Downsample10Hz_PerTrack);
				LOG_COMPRESSION_STATUS(Downsample5Hz_PerTrack);

				LOG_COMPRESSION_STATUS(Adaptive1_15Hz_LinPerTrack);
				LOG_COMPRESSION_STATUS(Adaptive1_10Hz_LinPerTrack);
				LOG_COMPRESSION_STATUS(Adaptive1_5Hz_LinPerTrack);

				LOG_COMPRESSION_STATUS(Adaptive2_15Hz_LinPerTrack);
				LOG_COMPRESSION_STATUS(Adaptive2_10Hz_LinPerTrack);
			}

			{
				LOG_COMPRESSION_STATUS(Adaptive2_PerTrack);
				LOG_COMPRESSION_STATUS(Adaptive2_LinPerTrack);
				LOG_COMPRESSION_STATUS(Adaptive2_LinPerTrackNoRT);
			}
		}
		// Do not recompress - Still take into account size for stats.
		else
		{
			TotalSizeNow += OutCompressedData.GetApproxBoneCompressedSize();
		}
	}
	else
	{
		// this can happen if the animation only contains curve - i.e. blendshape curves
		UE_LOG(LogAnimationCompression, Log, TEXT("Compression Requested for Empty Animation %s"), *CompressibleAnimData.Name );
	}
#endif // WITH_EDITORONLY_DATA
}

static void GetBindPoseAtom(FTransform &OutBoneAtom, int32 BoneIndex, USkeleton *Skeleton)
{
	OutBoneAtom = Skeleton->GetRefLocalPoses()[BoneIndex];
// #if DEBUG_ADDITIVE_CREATION
// 	UE_LOG(LogAnimation, Log, TEXT("GetBindPoseAtom BoneIndex: %d, OutBoneAtom: %s"), BoneIndex, *OutBoneAtom.ToString());
// #endif
}

/** Get default Outer for AnimSequences contained in this AnimSet.
	*  The intent is to use that when Constructing new AnimSequences to put into that set.
	*  The Outer will be Package.<AnimSetName>_Group.
	*  @param bCreateIfNotFound if true, Group will be created. This is only in the editor.
	*/
UObject* FAnimationUtils::GetDefaultAnimSequenceOuter(UAnimSet* InAnimSet, bool bCreateIfNotFound)
{
	check( InAnimSet );

#if WITH_EDITORONLY_DATA
	for(int32 i=0; i<InAnimSet->Sequences.Num(); i++)
	{
		UAnimSequence* TestAnimSeq = InAnimSet->Sequences[i];
		// Make sure outer is not current AnimSet, but they should be in the same package.
		if( TestAnimSeq && TestAnimSeq->GetOuter() != InAnimSet && TestAnimSeq->GetOutermost() == InAnimSet->GetOutermost() )
		{
			return TestAnimSeq->GetOuter();
		}
	}
#endif	//#if WITH_EDITORONLY_DATA

	// Otherwise go ahead and create a new one if we should.
	if( bCreateIfNotFound )
	{
		// We can only create Group if we are within the editor.
		check(GIsEditor);

		UPackage* AnimSetPackage = InAnimSet->GetOutermost();
		// Make sure package is fully loaded.
		AnimSetPackage->FullyLoad();

		// Try to create a new package with Group named <AnimSetName>_Group.
		FString NewPackageString = FString::Printf(TEXT("%s.%s_Group"), *AnimSetPackage->GetFName().ToString(), *InAnimSet->GetFName().ToString());
		UPackage* NewPackage = CreatePackage( NULL, *NewPackageString );

		// New Outer to use
		return NewPackage;
	}

	return NULL;
}


/**
 * Converts an animation compression type into a human readable string
 *
 * @param	InFormat	The compression format to convert into a string
 * @return				The format as a string
 */
FString FAnimationUtils::GetAnimationCompressionFormatString(AnimationCompressionFormat InFormat)
{
	switch(InFormat)
	{
	case ACF_None:
		return FString(TEXT("ACF_None"));
	case ACF_Float96NoW:
		return FString(TEXT("ACF_Float96NoW"));
	case ACF_Fixed48NoW:
		return FString(TEXT("ACF_Fixed48NoW"));
	case ACF_IntervalFixed32NoW:
		return FString(TEXT("ACF_IntervalFixed32NoW"));
	case ACF_Fixed32NoW:
		return FString(TEXT("ACF_Fixed32NoW"));
	case ACF_Float32NoW:
		return FString(TEXT("ACF_Float32NoW"));
	case ACF_Identity:
		return FString(TEXT("ACF_Identity"));
	default:
		UE_LOG(LogAnimationCompression, Warning, TEXT("AnimationCompressionFormat was not found:  %i"), static_cast<int32>(InFormat) );
	}

	return FString(TEXT("Unknown"));
}

/**
 * Converts an animation codec format into a human readable string
 *
 * @param	InFormat	The format to convert into a string
 * @return				The format as a string
 */
FString FAnimationUtils::GetAnimationKeyFormatString(AnimationKeyFormat InFormat)
{
	switch(InFormat)
	{
	case AKF_ConstantKeyLerp:
		return FString(TEXT("AKF_ConstantKeyLerp"));
	case AKF_VariableKeyLerp:
		return FString(TEXT("AKF_VariableKeyLerp"));
	case AKF_PerTrackCompression:
		return FString(TEXT("AKF_PerTrackCompression"));
	default:
		UE_LOG(LogAnimationCompression, Warning, TEXT("AnimationKeyFormat was not found:  %i"), static_cast<int32>(InFormat) );
	}

	return FString(TEXT("Unknown"));
}


/**
 * Computes the 'height' of each track, relative to a given animation linkup.
 *
 * The track height is defined as the minimal number of bones away from an end effector (end effectors are 0, their parents are 1, etc...)
 *
  * @param BoneData				The bone data to check
 * @param NumTracks				The number of tracks
 * @param TrackHeights [OUT]	The computed track heights
 *
 */
void FAnimationUtils::CalculateTrackHeights(const FCompressibleAnimData& CompressibleAnimData, int32 NumTracks, TArray<int32>& TrackHeights)
{
	TrackHeights.Empty();
	TrackHeights.AddUninitialized(NumTracks);
	for (int32 TrackIndex = 0; TrackIndex < NumTracks; ++TrackIndex)
	{
		TrackHeights[TrackIndex] = 0;
	}

	const TArray<FBoneData>& BoneData = CompressibleAnimData.BoneData;

	// Populate the bone 'height' table (distance from closest end effector, with 0 indicating an end effector)
	// setup the raw bone transformation and find all end effectors
	for (int32 BoneIndex = 0; BoneIndex < BoneData.Num(); ++BoneIndex)
	{
		// also record all end-effectors we find
		const FBoneData& Bone = BoneData[BoneIndex];
		if (Bone.IsEndEffector())
		{
			const FBoneData& EffectorBoneData = BoneData[BoneIndex];

			for (int32 FamilyIndex = 0; FamilyIndex < EffectorBoneData.BonesToRoot.Num(); ++FamilyIndex)
			{
				const int32 NextParentBoneIndex = EffectorBoneData.BonesToRoot[FamilyIndex];
				const int32 NextParentTrackIndex = GetAnimTrackIndexForSkeletonBone(NextParentBoneIndex, CompressibleAnimData.TrackToSkeletonMapTable);
				if (NextParentTrackIndex != INDEX_NONE)
				{
					const int32 CurHeight = TrackHeights[NextParentTrackIndex];
					TrackHeights[NextParentTrackIndex] = (CurHeight > 0) ? FMath::Min<int32>(CurHeight, (FamilyIndex+1)) : (FamilyIndex+1);
				}
			}
		}
	}
}

/**
 * Checks a set of key times to see if the spacing is uniform or non-uniform.
 * Note: If there are as many times as frames, they are automatically assumed to be uniformly spaced.
 * Note: If there are two or fewer times, they are automatically assumed to be uniformly spaced.
 *
 * @param AnimSeq		The animation sequence the Times array is associated with
 * @param Times			The array of key times
 *
 * @return				true if the keys are uniformly spaced (or one of the trivial conditions is detected).  false if any key spacing is greater than 1e-4 off.
 */
bool FAnimationUtils::HasUniformKeySpacing(int32 NumFrames, const TArray<float>& Times)
{
	if ((Times.Num() <= 2) || (Times.Num() == NumFrames))
	{
		return true;
	}

	float FirstDelta = Times[1] - Times[0];
	for (int32 i = 2; i < Times.Num(); ++i)
	{
		float DeltaTime = Times[i] - Times[i-1];

		if (fabs(DeltaTime - FirstDelta) > KINDA_SMALL_NUMBER)
		{
			return false;
		}
	}

	return false;
}

/**
 * Perturbs the bone(s) associated with each track in turn, measuring the maximum error introduced in end effectors as a result
 */
void FAnimationUtils::TallyErrorsFromPerturbation(
	const FCompressibleAnimData& CompressibleAnimData,
	int32 NumTracks,
	const FVector& PositionNudge,
	const FQuat& RotationNudge,
	const FVector& ScaleNudge,
	TArray<FAnimPerturbationError>& InducedErrors)
{
	const float TimeStep = (float)CompressibleAnimData.SequenceLength / CompressibleAnimData.NumFrames;
	const int32 NumBones = CompressibleAnimData.BoneData.Num();


	USkeleton* Skeleton = CompressibleAnimData.Skeleton;
	check ( Skeleton );

	const TArray<FTransform>& RefPose = Skeleton->GetRefLocalPoses();

	TArray<FTransform> RawAtoms;
	TArray<FTransform> NewAtomsT;
	TArray<FTransform> NewAtomsR;
	TArray<FTransform> NewAtomsS;
	TArray<FTransform> RawTransforms;
	TArray<FTransform> NewTransformsT;
	TArray<FTransform> NewTransformsR;
	TArray<FTransform> NewTransformsS;

	RawAtoms.AddZeroed(NumBones);
	NewAtomsT.AddZeroed(NumBones);
	NewAtomsR.AddZeroed(NumBones);
	NewAtomsS.AddZeroed(NumBones);
	RawTransforms.AddZeroed(NumBones);
	NewTransformsT.AddZeroed(NumBones);
	NewTransformsR.AddZeroed(NumBones);
	NewTransformsS.AddZeroed(NumBones);

	InducedErrors.AddUninitialized(NumTracks);

	FTransform Perturbation(RotationNudge, PositionNudge, ScaleNudge);

	for (int32 TrackUnderTest = 0; TrackUnderTest < NumTracks; ++TrackUnderTest)
	{
		float MaxErrorT_DueToT = 0.0f;
		float MaxErrorR_DueToT = 0.0f;
		float MaxErrorS_DueToT = 0.0f;
		float MaxErrorT_DueToR = 0.0f;
		float MaxErrorR_DueToR = 0.0f;
		float MaxErrorS_DueToR = 0.0f;
		float MaxErrorT_DueToS = 0.0f;
		float MaxErrorR_DueToS = 0.0f;
		float MaxErrorS_DueToS = 0.0f;
		
		// for each whole increment of time (frame stepping)
		for (float Time = 0.0f; Time < CompressibleAnimData.SequenceLength; Time += TimeStep)
		{
			// get the raw and compressed atom for each bone
			for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
			{
				const int32 TrackIndex = GetAnimTrackIndexForSkeletonBone(BoneIndex, CompressibleAnimData.TrackToSkeletonMapTable);

				if (TrackIndex == INDEX_NONE)
				{
					// No track for the bone was found, so use the reference pose.
					RawAtoms[BoneIndex]	= RefPose[BoneIndex];
					NewAtomsT[BoneIndex] = RawAtoms[BoneIndex];
					NewAtomsR[BoneIndex] = RawAtoms[BoneIndex];
					NewAtomsS[BoneIndex] = RawAtoms[BoneIndex];
				}
				else
				{
					ExtractTransformFromTrack(Time, CompressibleAnimData.NumFrames, CompressibleAnimData.SequenceLength, CompressibleAnimData.RawAnimationData[TrackIndex], CompressibleAnimData.Interpolation, RawAtoms[BoneIndex]);

					NewAtomsT[BoneIndex] = RawAtoms[BoneIndex];
					NewAtomsR[BoneIndex] = RawAtoms[BoneIndex];
					NewAtomsS[BoneIndex] = RawAtoms[BoneIndex];

					// Perturb the bone under test
					if (TrackIndex == TrackUnderTest)
					{
						NewAtomsT[BoneIndex].AddToTranslation(PositionNudge);

						FQuat NewR = NewAtomsR[BoneIndex].GetRotation();
						NewR += RotationNudge;
						NewR.Normalize();
						NewAtomsR[BoneIndex].SetRotation(NewR);

						FVector Scale3D = NewAtomsS[BoneIndex].GetScale3D();
						NewAtomsS[BoneIndex].SetScale3D( Scale3D + ScaleNudge);
					}
				}

				RawTransforms[BoneIndex] = RawAtoms[BoneIndex];
				NewTransformsT[BoneIndex] = NewAtomsT[BoneIndex];
				NewTransformsR[BoneIndex] = NewAtomsR[BoneIndex];
				NewTransformsS[BoneIndex] = NewAtomsS[BoneIndex];

				// For all bones below the root, final component-space transform is relative transform * component-space transform of parent.
				if ( BoneIndex > 0 )
				{
					const int32 ParentIndex = Skeleton->GetReferenceSkeleton().GetParentIndex(BoneIndex);

					// Check the precondition that parents occur before children in the RequiredBones array.
					check( ParentIndex != INDEX_NONE );
					check( ParentIndex < BoneIndex );

					RawTransforms[BoneIndex] *= RawTransforms[ParentIndex];
					NewTransformsT[BoneIndex] *= NewTransformsT[ParentIndex];
					NewTransformsR[BoneIndex] *= NewTransformsR[ParentIndex];
					NewTransformsS[BoneIndex] *= NewTransformsS[ParentIndex];
				}

				// Only look at the error that occurs in end effectors
				if (CompressibleAnimData.BoneData[BoneIndex].IsEndEffector())
				{
					MaxErrorT_DueToT = FMath::Max(MaxErrorT_DueToT, (RawTransforms[BoneIndex].GetLocation() - NewTransformsT[BoneIndex].GetLocation()).Size());
					MaxErrorT_DueToR = FMath::Max(MaxErrorT_DueToR, (RawTransforms[BoneIndex].GetLocation() - NewTransformsR[BoneIndex].GetLocation()).Size());
					MaxErrorT_DueToS = FMath::Max(MaxErrorT_DueToS, (RawTransforms[BoneIndex].GetLocation() - NewTransformsS[BoneIndex].GetLocation()).Size());
					MaxErrorR_DueToT = FMath::Max(MaxErrorR_DueToT, FQuat::ErrorAutoNormalize(RawTransforms[BoneIndex].GetRotation(), NewTransformsT[BoneIndex].GetRotation()));
					MaxErrorR_DueToR = FMath::Max(MaxErrorR_DueToR, FQuat::ErrorAutoNormalize(RawTransforms[BoneIndex].GetRotation(), NewTransformsR[BoneIndex].GetRotation()));
					MaxErrorR_DueToS = FMath::Max(MaxErrorR_DueToS, FQuat::ErrorAutoNormalize(RawTransforms[BoneIndex].GetRotation(), NewTransformsS[BoneIndex].GetRotation()));
					MaxErrorS_DueToT = FMath::Max(MaxErrorS_DueToT, (RawTransforms[BoneIndex].GetScale3D() - NewTransformsT[BoneIndex].GetScale3D()).Size());
					MaxErrorS_DueToR = FMath::Max(MaxErrorS_DueToR, (RawTransforms[BoneIndex].GetScale3D() - NewTransformsR[BoneIndex].GetScale3D()).Size());
					MaxErrorS_DueToS = FMath::Max(MaxErrorS_DueToS, (RawTransforms[BoneIndex].GetScale3D() - NewTransformsS[BoneIndex].GetScale3D()).Size());
				}
			} // for each bone
		} // for each time

		// Save the worst errors
		FAnimPerturbationError& TrackError = InducedErrors[TrackUnderTest];
		TrackError.MaxErrorInTransDueToTrans = MaxErrorT_DueToT;
		TrackError.MaxErrorInRotDueToTrans = MaxErrorR_DueToT;
		TrackError.MaxErrorInScaleDueToTrans = MaxErrorS_DueToT;
		TrackError.MaxErrorInTransDueToRot = MaxErrorT_DueToR;
		TrackError.MaxErrorInRotDueToRot = MaxErrorR_DueToR;
		TrackError.MaxErrorInScaleDueToRot = MaxErrorS_DueToR;
		TrackError.MaxErrorInTransDueToScale = MaxErrorT_DueToR;
		TrackError.MaxErrorInRotDueToScale = MaxErrorR_DueToR;
		TrackError.MaxErrorInScaleDueToScale = MaxErrorS_DueToR;
	}
}

static UAnimCurveCompressionSettings* DefaultCurveCompressionSettings = nullptr;

UAnimCurveCompressionSettings* FAnimationUtils::GetDefaultAnimationCurveCompressionSettings()
{
	if (DefaultCurveCompressionSettings == nullptr)
	{
		FConfigSection* AnimDefaultObjectSettingsSection = GConfig->GetSectionPrivate(TEXT("Animation.DefaultObjectSettings"), false, true, GEngineIni);
		const FConfigValue* Value = AnimDefaultObjectSettingsSection != nullptr ? AnimDefaultObjectSettingsSection->Find(TEXT("CurveCompressionSettings")) : nullptr;

		if (Value != nullptr)
		{
			const FString& CurveCompressionSettingsName = Value->GetValue();

			DefaultCurveCompressionSettings = LoadObject<UAnimCurveCompressionSettings>(nullptr, *CurveCompressionSettingsName);

			if (DefaultCurveCompressionSettings == nullptr)
			{
				UE_LOG(LogAnimationCompression, Fatal, TEXT("Couldn't load default curve compression settings with path '%s'"), *CurveCompressionSettingsName);
			}
		}
		else
		{
			UE_LOG(LogAnimationCompression, Fatal, TEXT("Couldn't find default curve compression setting under '[Animation.DefaultObjectSettings]'"));
		}

		// Force load the default settings and all its dependencies just in case it hasn't happened yet
		bool bLoadDependencies = false;
		if (DefaultCurveCompressionSettings->HasAnyFlags(RF_NeedLoad))
		{
			DefaultCurveCompressionSettings->GetLinker()->Preload(DefaultCurveCompressionSettings);
			bLoadDependencies = true;
		}

		if (DefaultCurveCompressionSettings->HasAnyFlags(RF_NeedPostLoad))
		{
			DefaultCurveCompressionSettings->ConditionalPostLoad();
			bLoadDependencies = true;
		}

		if (bLoadDependencies)
		{
			TArray<UObject*> ObjectReferences;
			FReferenceFinder(ObjectReferences, nullptr, false, true, false, true).FindReferences(DefaultCurveCompressionSettings);

			for (UObject* Dependency : ObjectReferences)
			{
				if (Dependency->HasAnyFlags(RF_NeedLoad))
				{
					Dependency->GetLinker()->Preload(Dependency);
				}

				if (Dependency->HasAnyFlags(RF_NeedPostLoad))
				{
					Dependency->ConditionalPostLoad();
				}
			}
		}

		DefaultCurveCompressionSettings->AddToRoot();
	}

	return DefaultCurveCompressionSettings;
}

void FAnimationUtils::ExtractTransformFromTrack(float Time, int32 NumFrames, float SequenceLength, const struct FRawAnimSequenceTrack& RawTrack, EAnimInterpolationType Interpolation, FTransform &OutAtom)
{
	// Bail out (with rather wacky data) if data is empty for some reason.
	if (RawTrack.PosKeys.Num() == 0 || RawTrack.RotKeys.Num() == 0)
	{
		OutAtom.SetIdentity();
		return;
	}

	int32 KeyIndex1, KeyIndex2;
	float Alpha;
	FAnimationRuntime::GetKeyIndicesFromTime(KeyIndex1, KeyIndex2, Alpha, Time, NumFrames, SequenceLength);
	// @Todo fix me: this change is not good, it has lots of branches. But we'd like to save memory for not saving scale if no scale change exists
	const bool bHasScaleKey = (RawTrack.ScaleKeys.Num() > 0);
	static const FVector DefaultScale3D = FVector(1.f);

	if (Interpolation == EAnimInterpolationType::Step)
	{
		Alpha = 0.f;
	}

	if (Alpha <= 0.f)
	{
		const int32 PosKeyIndex1 = FMath::Min(KeyIndex1, RawTrack.PosKeys.Num() - 1);
		const int32 RotKeyIndex1 = FMath::Min(KeyIndex1, RawTrack.RotKeys.Num() - 1);
		if (bHasScaleKey)
		{
			const int32 ScaleKeyIndex1 = FMath::Min(KeyIndex1, RawTrack.ScaleKeys.Num() - 1);
			OutAtom = FTransform(RawTrack.RotKeys[RotKeyIndex1], RawTrack.PosKeys[PosKeyIndex1], RawTrack.ScaleKeys[ScaleKeyIndex1]);
		}
		else
		{
			OutAtom = FTransform(RawTrack.RotKeys[RotKeyIndex1], RawTrack.PosKeys[PosKeyIndex1], DefaultScale3D);
		}
		return;
	}
	else if (Alpha >= 1.f)
	{
		const int32 PosKeyIndex2 = FMath::Min(KeyIndex2, RawTrack.PosKeys.Num() - 1);
		const int32 RotKeyIndex2 = FMath::Min(KeyIndex2, RawTrack.RotKeys.Num() - 1);
		if (bHasScaleKey)
		{
			const int32 ScaleKeyIndex2 = FMath::Min(KeyIndex2, RawTrack.ScaleKeys.Num() - 1);
			OutAtom = FTransform(RawTrack.RotKeys[RotKeyIndex2], RawTrack.PosKeys[PosKeyIndex2], RawTrack.ScaleKeys[ScaleKeyIndex2]);
		}
		else
		{
			OutAtom = FTransform(RawTrack.RotKeys[RotKeyIndex2], RawTrack.PosKeys[PosKeyIndex2], DefaultScale3D);
		}
		return;
	}

	const int32 PosKeyIndex1 = FMath::Min(KeyIndex1, RawTrack.PosKeys.Num() - 1);
	const int32 RotKeyIndex1 = FMath::Min(KeyIndex1, RawTrack.RotKeys.Num() - 1);

	const int32 PosKeyIndex2 = FMath::Min(KeyIndex2, RawTrack.PosKeys.Num() - 1);
	const int32 RotKeyIndex2 = FMath::Min(KeyIndex2, RawTrack.RotKeys.Num() - 1);

	FTransform KeyAtom1, KeyAtom2;

	if (bHasScaleKey)
	{
		const int32 ScaleKeyIndex1 = FMath::Min(KeyIndex1, RawTrack.ScaleKeys.Num() - 1);
		const int32 ScaleKeyIndex2 = FMath::Min(KeyIndex2, RawTrack.ScaleKeys.Num() - 1);

		KeyAtom1 = FTransform(RawTrack.RotKeys[RotKeyIndex1], RawTrack.PosKeys[PosKeyIndex1], RawTrack.ScaleKeys[ScaleKeyIndex1]);
		KeyAtom2 = FTransform(RawTrack.RotKeys[RotKeyIndex2], RawTrack.PosKeys[PosKeyIndex2], RawTrack.ScaleKeys[ScaleKeyIndex2]);
	}
	else
	{
		KeyAtom1 = FTransform(RawTrack.RotKeys[RotKeyIndex1], RawTrack.PosKeys[PosKeyIndex1], DefaultScale3D);
		KeyAtom2 = FTransform(RawTrack.RotKeys[RotKeyIndex2], RawTrack.PosKeys[PosKeyIndex2], DefaultScale3D);
	}

	// 	UE_LOG(LogAnimation, Log, TEXT(" *  *  *  Position. PosKeyIndex1: %3d, PosKeyIndex2: %3d, Alpha: %f"), PosKeyIndex1, PosKeyIndex2, Alpha);
	// 	UE_LOG(LogAnimation, Log, TEXT(" *  *  *  Rotation. RotKeyIndex1: %3d, RotKeyIndex2: %3d, Alpha: %f"), RotKeyIndex1, RotKeyIndex2, Alpha);

	// Ensure rotations are normalized (Added for Jira UE-53971)
	KeyAtom1.NormalizeRotation();
	KeyAtom2.NormalizeRotation();

	OutAtom.Blend(KeyAtom1, KeyAtom2, Alpha);
	OutAtom.NormalizeRotation();
}

#if WITH_EDITOR

void FAnimationUtils::ExtractTransformFromCompressionData(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& CompressedAnimData, float Time, int32 TrackIndex, bool bUseRawData, FTransform& OutBoneTransform)
{
	// If the caller didn't request that raw animation data be used . . .
	if (!bUseRawData && CompressedAnimData.IsCompressedDataValid())
	{
		const FUECompressedAnimData CompressedDataWrapper(CompressedAnimData);
		FAnimSequenceDecompressionContext DecompContext(CompressibleAnimData, CompressedDataWrapper);
		DecompContext.Seek(Time);
		AnimationFormat_GetBoneAtom(OutBoneTransform, DecompContext, TrackIndex);
		return;
	}

	FAnimationUtils::ExtractTransformFromTrack(Time, CompressibleAnimData.NumFrames, CompressibleAnimData.SequenceLength, CompressibleAnimData.RawAnimationData[TrackIndex], CompressibleAnimData.Interpolation, OutBoneTransform);
}

bool FAnimationUtils::CompressAnimCurves(FCompressibleAnimData& AnimSeq, FCompressedAnimSequence& Target)
{
	// Clear any previous data we might have even if we end up failing to compress
	Target.CompressedCurveByteStream.Empty();
	Target.CurveCompressionCodec = nullptr;

	if (AnimSeq.CurveCompressionSettings == nullptr || !AnimSeq.CurveCompressionSettings->AreSettingsValid())
	{
		return false;
	}

	check(AnimSeq.CurveCompressionSettings->AreSettingsValid());
	return AnimSeq.CurveCompressionSettings->Compress(AnimSeq, Target);
}
#endif
