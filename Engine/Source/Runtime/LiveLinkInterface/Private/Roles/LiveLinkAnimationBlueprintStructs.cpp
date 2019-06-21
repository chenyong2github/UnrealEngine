// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Roles/LiveLinkAnimationBlueprintStructs.h"

#include "Misc/QualifiedFrameTime.h"
#include "Roles/LiveLinkAnimationTypes.h"

// FCachedSubjectFrame

FCachedSubjectFrame::FCachedSubjectFrame() 
	: bHaveCachedCurves(false)
{

};

FCachedSubjectFrame::FCachedSubjectFrame(const FLiveLinkSkeletonStaticData* InStaticData, const FLiveLinkAnimationFrameData* InAnimData)
	: bHaveCachedCurves(false)
{
	SourceSkeletonData = *InStaticData;
	SourceAnimationFrameData = *InAnimData;

	const int32 NumTransforms = InAnimData->Transforms.Num();
	check(InStaticData->BoneNames.Num() == NumTransforms);
	check(InStaticData->BoneParents.Num() == NumTransforms);
	check(InStaticData->PropertyNames.Num() == InAnimData->PropertyValues.Num());
	RootSpaceTransforms.SetNum(NumTransforms);
	ChildTransformIndices.SetNum(NumTransforms);
	for (int32 i = 0; i < NumTransforms; ++i)
	{
		RootSpaceTransforms[i].Key = false;
		ChildTransformIndices[i].Key = false;
	}
}

void FCachedSubjectFrame::SetCurvesFromCache(TMap<FName, float>& OutCurves)
{
	if (!bHaveCachedCurves)
	{
		CacheCurves();
	}
	OutCurves = CachedCurves;
};

void FCachedSubjectFrame::GetSubjectMetadata(FSubjectMetadata& OutSubjectMetadata)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	OutSubjectMetadata.StringMetadata = SourceAnimationFrameData.MetaData.StringMetaData;
	FQualifiedFrameTime QualifiedFrameTime = SourceAnimationFrameData.MetaData.SceneTime;
	OutSubjectMetadata.SceneTimecode = FTimecode::FromFrameNumber(QualifiedFrameTime.Time.FrameNumber, QualifiedFrameTime.Rate, false);
	OutSubjectMetadata.SceneFramerate = QualifiedFrameTime.Rate;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
};

int32 FCachedSubjectFrame::GetNumberOfTransforms()
{
	return SourceAnimationFrameData.Transforms.Num();
};

void FCachedSubjectFrame::GetTransformNames(TArray<FName>& OutTransformNames)
{
	OutTransformNames = SourceSkeletonData.GetBoneNames();
};

void FCachedSubjectFrame::GetTransformName(const int32 InTransformIndex, FName& OutName)
{
	if (IsValidTransformIndex(InTransformIndex))
	{
		OutName = SourceSkeletonData.GetBoneNames()[InTransformIndex];
	}
	else
	{
		OutName = TEXT("None");
	}
};

int32 FCachedSubjectFrame::GetTransformIndexFromName(FName InTransformName)
{
	return SourceSkeletonData.GetBoneNames().IndexOfByKey(InTransformName);
};

int32 FCachedSubjectFrame::GetParentTransformIndex(const int32 InTransformIndex)
{
	if (IsValidTransformIndex(InTransformIndex))
	{
		return SourceSkeletonData.GetBoneParents()[InTransformIndex];
	}
	else
	{
		return -1;
	}
};

void FCachedSubjectFrame::GetChildTransformIndices(const int32 InTransformIndex, TArray<int32>& OutChildIndices)
{
	OutChildIndices.Reset();
	if (IsValidTransformIndex(InTransformIndex))
	{
		TPair<bool, TArray<int32>>& ChildIndicesCache = ChildTransformIndices[InTransformIndex];
		bool bHasValidCache = ChildIndicesCache.Key;
		TArray<int32>& CachedChildIndices = ChildIndicesCache.Value;
		if (!bHasValidCache)
		{
			// Build Cache
			const TArray<int32>& BoneParents = SourceSkeletonData.GetBoneParents();
			for (int32 ChildIndex = 0; ChildIndex < BoneParents.Num(); ++ChildIndex)
			{
				if (BoneParents[ChildIndex] == InTransformIndex)
				{
					CachedChildIndices.Emplace(ChildIndex);
				}
			}
			ChildIndicesCache.Key = true;
		}
		OutChildIndices = CachedChildIndices;
	}
}

void FCachedSubjectFrame::GetTransformParentSpace(const int32 InTransformIndex, FTransform& OutTransform)
{
	// Case: Root joint or invalid
	OutTransform = FTransform::Identity;
	// Case: Joint in SourceFrame
	if (IsValidTransformIndex(InTransformIndex))
	{
		OutTransform = SourceAnimationFrameData.Transforms[InTransformIndex];
	}
};

void FCachedSubjectFrame::GetTransformRootSpace(const int32 InTransformIndex, FTransform& OutTransform)
{
	// Case: Root joint or invalid
	OutTransform = FTransform::Identity;
	if (IsValidTransformIndex(InTransformIndex))
	{
		TPair<bool, FTransform>& RootSpaceCache = RootSpaceTransforms[InTransformIndex];
		bool bHasValidCache = RootSpaceCache.Key;
		// Case: Have Cached Value
		if (bHasValidCache)
		{
			OutTransform = RootSpaceCache.Value;
		}
		// Case: Need to generate Cache
		else
		{
			const TArray<int32>& BoneParents = SourceSkeletonData.GetBoneParents();
			int32 ParentIndex = BoneParents[InTransformIndex];

			FTransform& LocalSpaceTransform = SourceAnimationFrameData.Transforms[InTransformIndex];

			FTransform ParentRootSpaceTransform;
			GetTransformRootSpace(ParentIndex, ParentRootSpaceTransform);

			OutTransform = LocalSpaceTransform * ParentRootSpaceTransform;

			// Save cached results
			RootSpaceCache.Key = true;
			RootSpaceCache.Value = OutTransform;
		}
	}
};

int32 FCachedSubjectFrame::GetRootIndex()
{
	const TArray<int32>& BoneParents = SourceSkeletonData.GetBoneParents();
	return BoneParents.IndexOfByPredicate([](const int32& ParentIndex) { return ParentIndex < 0; });
};

void FCachedSubjectFrame::CacheCurves()
{
	bHaveCachedCurves = false;
	CachedCurves.Reset();

	const TArray<FName>& CurveNames = SourceSkeletonData.PropertyNames;
	if (CurveNames.Num() == SourceAnimationFrameData.PropertyValues.Num())
	{
		CachedCurves.Reserve(CurveNames.Num());
		for (int32 CurveIdx = 0; CurveIdx < CurveNames.Num(); ++CurveIdx)
		{
			CachedCurves.Add(CurveNames[CurveIdx], SourceAnimationFrameData.PropertyValues[CurveIdx]);
		}

		bHaveCachedCurves = true;
	}
};

bool FCachedSubjectFrame::IsValidTransformIndex(int32 InTransformIndex)
{
	return (InTransformIndex >= 0) && (InTransformIndex < SourceAnimationFrameData.Transforms.Num());
};

// FLiveLinkTransform

FLiveLinkTransform::FLiveLinkTransform()
// Initialise with invalid index to force transforms
// to evaluate as identity
	: TransformIndex(-1)
{
};

void FLiveLinkTransform::GetName(FName& OutName)
{
	if (CachedFrame.IsValid())
	{
		CachedFrame->GetTransformName(TransformIndex, OutName);
	}
};

void FLiveLinkTransform::GetTransformParentSpace(FTransform& OutTransform)
{
	if (CachedFrame.IsValid())
	{
		CachedFrame->GetTransformParentSpace(TransformIndex, OutTransform);
	}
};

void FLiveLinkTransform::GetTransformRootSpace(FTransform& OutTransform)
{
	if (CachedFrame.IsValid())
	{
		CachedFrame->GetTransformRootSpace(TransformIndex, OutTransform);
	}
};

bool FLiveLinkTransform::HasParent()
{
	if (CachedFrame.IsValid())
	{
		return CachedFrame->GetParentTransformIndex(TransformIndex) >= 0;
	}
	else
	{
		return false;
	}
};

void FLiveLinkTransform::GetParent(FLiveLinkTransform& OutParentTransform)
{
	if (CachedFrame.IsValid())
	{
		int32 ParentIndex = CachedFrame->GetParentTransformIndex(TransformIndex);
		OutParentTransform.SetCachedFrame(CachedFrame);
		OutParentTransform.SetTransformIndex(ParentIndex);
	}
};

int32 FLiveLinkTransform::GetChildCount()
{
	if (CachedFrame.IsValid())
	{
		TArray<int32> ChildIndices;
		CachedFrame->GetChildTransformIndices(TransformIndex, ChildIndices);
		return ChildIndices.Num();
	}
	else
	{
		return 0;
	}
};

void FLiveLinkTransform::GetChildren(TArray<FLiveLinkTransform>& OutChildTransforms)
{
	OutChildTransforms.Reset();
	if (CachedFrame.IsValid())
	{
		TArray<int32> ChildIndices;
		CachedFrame->GetChildTransformIndices(TransformIndex, ChildIndices);
		for (const int32 ChildIndex : ChildIndices)
		{
			int32 NewIndex = OutChildTransforms.AddDefaulted();
			OutChildTransforms[NewIndex].SetCachedFrame(CachedFrame);
			OutChildTransforms[NewIndex].SetTransformIndex(ChildIndex);
		}
	}
};

void FLiveLinkTransform::SetCachedFrame(TSharedPtr<FCachedSubjectFrame> InCachedFrame)
{
	CachedFrame = InCachedFrame;
};

void FLiveLinkTransform::SetTransformIndex(const int32 InTransformIndex)
{
	TransformIndex = InTransformIndex;
};

int32 FLiveLinkTransform::GetTransformIndex() const
{
	return TransformIndex;
};

// FSubjectFrameHandle

void FSubjectFrameHandle::GetCurves(TMap<FName, float>& OutCurves)
{
	if (CachedFrame.IsValid())
	{
		CachedFrame->SetCurvesFromCache(OutCurves);
	}
};

void FSubjectFrameHandle::GetSubjectMetadata(FSubjectMetadata& OutMetadata)
{
	if (CachedFrame.IsValid())
	{
		CachedFrame->GetSubjectMetadata(OutMetadata);
	}
};

int32 FSubjectFrameHandle::GetNumberOfTransforms()
{
	if (CachedFrame.IsValid())
	{
		return CachedFrame->GetNumberOfTransforms();
	}
	else
	{
		return 0;
	}
};

void FSubjectFrameHandle::GetTransformNames(TArray<FName>& OutTransformNames)
{
	if (CachedFrame.IsValid())
	{
		CachedFrame->GetTransformNames(OutTransformNames);
	}
};

void FSubjectFrameHandle::GetRootTransform(FLiveLinkTransform& OutLiveLinkTransform)
{
	OutLiveLinkTransform.SetCachedFrame(CachedFrame);
	if (CachedFrame.IsValid())
	{
		OutLiveLinkTransform.SetTransformIndex(CachedFrame->GetRootIndex());
	}
}

void FSubjectFrameHandle::GetTransformByIndex(int32 InTransformIndex, FLiveLinkTransform& OutLiveLinkTransform)
{
	OutLiveLinkTransform.SetCachedFrame(CachedFrame);
	OutLiveLinkTransform.SetTransformIndex(InTransformIndex);
};

void FSubjectFrameHandle::GetTransformByName(FName InTransformName, FLiveLinkTransform& OutLiveLinkTransform)
{
	int32 TransformIndex = CachedFrame->GetTransformIndexFromName(InTransformName);
	OutLiveLinkTransform.SetCachedFrame(CachedFrame);
	OutLiveLinkTransform.SetTransformIndex(TransformIndex);
};

FLiveLinkSkeletonStaticData* FSubjectFrameHandle::GetSourceSkeletonStaticData()
{
	if (CachedFrame.IsValid())
	{
		return &CachedFrame->GetSourceSkeletonData();
	}
	return nullptr;
}

FLiveLinkAnimationFrameData* FSubjectFrameHandle::GetSourceAnimationFrameData()
{
	if (CachedFrame.IsValid())
	{
		return &CachedFrame->GetSourceAnimationFrameData();
	}
	return nullptr;
}

void FSubjectFrameHandle::SetCachedFrame(TSharedPtr<FCachedSubjectFrame> InCachedFrame)
{
	CachedFrame = InCachedFrame;
};
