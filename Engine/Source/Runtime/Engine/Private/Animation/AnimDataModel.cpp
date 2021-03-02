// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimData/AnimDataModel.h"
#include "UObject/NameTypes.h"

#include "Animation/AnimSequence.h"

#include "Algo/Transform.h"
#include "Algo/Accumulate.h"
#include "Animation/SmartName.h"


void UAnimDataModel::PostLoad()
{
	UObject::PostLoad();

	GenerateTransientData();
}

void UAnimDataModel::PostDuplicate(bool bDuplicateForPIE)
{
	UObject::PostDuplicate(bDuplicateForPIE);

	GenerateTransientData();
	Notify(EAnimDataModelNotifyType::Populated);
}

const TArray<FBoneAnimationTrack>& UAnimDataModel::GetBoneAnimationTracks() const
{
	return BoneAnimationTracks;
}

const FBoneAnimationTrack& UAnimDataModel::GetBoneTrackByIndex(int32 TrackIndex) const
{
	checkf(BoneAnimationTracks.IsValidIndex(TrackIndex), TEXT("Unable to find animation track by index"));
	return BoneAnimationTracks[TrackIndex];
}

const FBoneAnimationTrack& UAnimDataModel::GetBoneTrackByName(FName TrackName) const
{
	const FBoneAnimationTrack* TrackPtr = BoneAnimationTracks.FindByPredicate([TrackName](FBoneAnimationTrack& Track)
	{
		return Track.Name == TrackName;
	});

	checkf(TrackPtr != nullptr, TEXT("Unable to find animation track by name"));

	return *TrackPtr;
}

const FBoneAnimationTrack* UAnimDataModel::FindBoneTrackByIndex(int32 BoneIndex) const
{
	const FBoneAnimationTrack* TrackPtr = BoneAnimationTracks.FindByPredicate([BoneIndex](FBoneAnimationTrack& Track)
	{
		return Track.BoneTreeIndex == BoneIndex;
	});

	return TrackPtr;
}

int32 UAnimDataModel::GetBoneTrackIndex(const FBoneAnimationTrack& Track) const
{
	return BoneAnimationTracks.IndexOfByPredicate([&Track](const FBoneAnimationTrack SearchTrack)
	{
		return SearchTrack.Name == Track.Name;
	});
}

int32 UAnimDataModel::GetBoneTrackIndexByName(FName TrackName) const
{
	if (const FBoneAnimationTrack* TrackPtr = FindBoneTrackByName(TrackName))
	{
		return GetBoneTrackIndex(*TrackPtr);
	}

	return INDEX_NONE;
}

float UAnimDataModel::GetPlayLength() const
{
	return PlayLength;
}

int32 UAnimDataModel::GetNumberOfFrames() const
{
	return NumberOfFrames;
}

int32 UAnimDataModel::GetNumberOfKeys() const
{
	return NumberOfKeys;
}

const FFrameRate& UAnimDataModel::GetFrameRate() const
{
	return FrameRate;
}

bool UAnimDataModel::IsValidBoneTrackIndex(int32 TrackIndex) const
{
	return BoneAnimationTracks.IsValidIndex(TrackIndex);
}

const int32 UAnimDataModel::GetNumBoneTracks() const
{
	return BoneAnimationTracks.Num();
}

UAnimSequence* UAnimDataModel::GetAnimationSequence() const
{
	return Cast<UAnimSequence>(GetOuter());
}

void UAnimDataModel::GetBoneTrackNames(TArray<FName>& OutNames) const
{
	Algo::Transform(BoneAnimationTracks, OutNames, [](FBoneAnimationTrack Track)
	{
		return Track.Name;
	});
}

const FAnimationCurveData& UAnimDataModel::GetCurveData() const
{
	return CurveData;
}

int32 UAnimDataModel::GetNumberOfTransformCurves() const
{
	return CurveData.TransformCurves.Num();
}

int32 UAnimDataModel::GetNumberOfFloatCurves() const
{
	return CurveData.FloatCurves.Num();
}

const TArray<FFloatCurve>& UAnimDataModel::GetFloatCurves() const
{
	return CurveData.FloatCurves;
}

const TArray<struct FTransformCurve>& UAnimDataModel::GetTransformCurves() const
{
	return CurveData.TransformCurves;
}

const FFloatCurve* UAnimDataModel::FindFloatCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	ensure(CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Float);
	for (const FFloatCurve& FloatCurve : CurveData.FloatCurves)
	{
		if (FloatCurve.Name == CurveIdentifier.InternalName)
		{
			return &FloatCurve;
		}
	}

	return nullptr;
}

const FTransformCurve* UAnimDataModel::FindTransformCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	ensure(CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Transform);
	for (const FTransformCurve& TransformCurve : CurveData.TransformCurves)
	{
		if (TransformCurve.Name == CurveIdentifier.InternalName)
		{
			return &TransformCurve;
		}
	}

	return nullptr;
}

const FRichCurve* UAnimDataModel::FindRichCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	const FRichCurve* RichCurve = nullptr;

	if (CurveIdentifier.IsValid())
	{
		if (CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Float)
		{
			const FFloatCurve* Curve = FindFloatCurve(CurveIdentifier);
			if (Curve)
			{
				RichCurve = &Curve->FloatCurve;
			}
		}
		else if (CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Transform)
		{
			if (CurveIdentifier.Channel != ETransformCurveChannel::Invalid && CurveIdentifier.Axis != EVectorCurveChannel::Invalid)
			{
				// Dealing with transform curve
				const FTransformCurve* TransformCurve = FindTransformCurve(CurveIdentifier);
				if (TransformCurve)
				{
					const FVectorCurve* VectorCurve = TransformCurve->GetVectorCurveByIndex((int32)CurveIdentifier.Channel);
					if (VectorCurve)
					{
						RichCurve = &VectorCurve->FloatCurves[(int32)CurveIdentifier.Axis];
					}
				}

			}
		}
	}

	return RichCurve;
}

const FAnimCurveBase& UAnimDataModel::GetCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	const FAnimCurveBase* CurvePtr = FindCurve(CurveIdentifier);
	checkf(CurvePtr, TEXT("Tried to retrieve non-existing curve"));
	return *CurvePtr;
}

const FFloatCurve& UAnimDataModel::GetFloatCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	const FFloatCurve* CurvePtr = FindFloatCurve(CurveIdentifier);
	checkf(CurvePtr, TEXT("Tried to retrieve non-existing curve"));
	return *CurvePtr;
}

const FTransformCurve& UAnimDataModel::GetTransformCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	const FTransformCurve* CurvePtr = FindTransformCurve(CurveIdentifier);
	checkf(CurvePtr, TEXT("Tried to retrieve non-existing curve"));
	return *CurvePtr;
}

const FRichCurve& UAnimDataModel::GetRichCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	const FRichCurve* CurvePtr = FindRichCurve(CurveIdentifier);
	checkf(CurvePtr, TEXT("Tried to retrieve non-existing curve"));
	return *CurvePtr;
}

TArrayView<const FAnimatedBoneAttribute> UAnimDataModel::GetAttributes() const
{
	return AnimatedBoneAttributes;
}

int32 UAnimDataModel::GetNumberOfAttributes() const
{
	return AnimatedBoneAttributes.Num();
}

int32 UAnimDataModel::GetNumberOfAttributesForBoneIndex(const int32 BoneIndex) const
{
	// Sum up total number of attributes with provided bone index
	const int32 NumberOfBoneAttributes = Algo::Accumulate<int32>(AnimatedBoneAttributes, 0, [BoneIndex](int32 Sum, const FAnimatedBoneAttribute& Attribute) -> int32
	{
		Sum += Attribute.Identifier.GetBoneIndex() == BoneIndex ? 1 : 0;
		return Sum;
	});
	return NumberOfBoneAttributes;
}

void UAnimDataModel::GetAttributesForBone(const FName& BoneName, TArray<const FAnimatedBoneAttribute*>& OutBoneAttributes) const
{
	// Sum up total number of attributes with provided bone name
	Algo::TransformIf(AnimatedBoneAttributes, OutBoneAttributes, [BoneName](const FAnimatedBoneAttribute& Attribute) -> bool
	{
		return Attribute.Identifier.GetBoneName() == BoneName;
	},
	[](const FAnimatedBoneAttribute& Attribute) 
	{
		return &Attribute;
	});
}

const FAnimatedBoneAttribute& UAnimDataModel::GetAttribute(const FAnimationAttributeIdentifier& AttributeIdentifier) const
{
	const FAnimatedBoneAttribute* AttributePtr = FindAttribute(AttributeIdentifier);
	checkf(AttributePtr, TEXT("Unable to find attribute for provided identifier"));

	return *AttributePtr;
}

const FAnimatedBoneAttribute* UAnimDataModel::FindAttribute(const FAnimationAttributeIdentifier& AttributeIdentifier) const
{
	return AnimatedBoneAttributes.FindByPredicate([AttributeIdentifier](const FAnimatedBoneAttribute& Attribute)
	{
		return Attribute.Identifier == AttributeIdentifier;
	});
}

const FAnimCurveBase* UAnimDataModel::FindCurve(const FAnimationCurveIdentifier& CurveIdentifier) const
{
	switch (CurveIdentifier.CurveType)
	{
		case ERawCurveTrackTypes::RCT_Float:
			return FindFloatCurve(CurveIdentifier);
		case ERawCurveTrackTypes::RCT_Transform:
			return FindTransformCurve(CurveIdentifier);
	}

	return nullptr;
}

FGuid UAnimDataModel::GenerateGuid() const
{
	FSHA1 Sha;
	
	auto UpdateSHAWithArray = [&](const auto& Array)
	{
		Sha.Update((uint8*)Array.GetData(), Array.Num() * Array.GetTypeSize());
	};
	   	
	auto UpdateWithData = [&](const auto& Data)
	{
		Sha.Update((uint8*)(&Data), sizeof(Data));
	};

	for (const FBoneAnimationTrack& Track : BoneAnimationTracks)
	{
		UpdateSHAWithArray(Track.InternalTrackData.PosKeys);
		UpdateSHAWithArray(Track.InternalTrackData.RotKeys);
		UpdateSHAWithArray(Track.InternalTrackData.ScaleKeys);
	}

	for (const FFloatCurve& Curve : CurveData.FloatCurves)
	{
		UpdateWithData(Curve.Name.UID);
		UpdateWithData(Curve.FloatCurve.DefaultValue);
		UpdateSHAWithArray(Curve.FloatCurve.GetConstRefOfKeys());
		UpdateWithData(Curve.FloatCurve.PreInfinityExtrap);
		UpdateWithData(Curve.FloatCurve.PostInfinityExtrap);
	}
	
	for (const FAnimatedBoneAttribute& Attribute : AnimatedBoneAttributes)
	{
		UpdateWithData(Attribute.Identifier);
		UpdateSHAWithArray(Attribute.Curve.GetConstRefOfKeys());
	}

	Sha.Final();

	uint32 Hash[5];
	Sha.GetHash((uint8*)Hash);
	FGuid Guid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
	
	return Guid;
}

const TArray<FRawAnimSequenceTrack>& UAnimDataModel::GetTransientRawAnimationTracks() const
{
	return RawAnimationTracks;
}

const TArray<FName>& UAnimDataModel::GetTransientRawAnimationTrackNames() const
{
	return RawAnimationTrackNames;
}

const TArray<FTrackToSkeletonMap>& UAnimDataModel::GetTransientRawAnimationTrackSkeletonMappings() const
{
	return RawAnimationTrackSkeletonMappings;
}

FRawAnimSequenceTrack& UAnimDataModel::GetNonConstRawAnimationTrackByIndex(int32 TrackIndex)
{
	checkf(BoneAnimationTracks.IsValidIndex(TrackIndex), TEXT("Invalid track index"));
	return BoneAnimationTracks[TrackIndex].InternalTrackData;
}

const FRawCurveTracks& UAnimDataModel::GetTransientRawCurveTracks() const
{
	return RawCurveTracks;
}

FAnimationCurveData& UAnimDataModel::GetNonConstCurveData()
{
	return CurveData;	
}

void UAnimDataModel::GenerateTransientData()
{
	RawAnimationTracks.Empty(BoneAnimationTracks.Num());
	RawAnimationTrackNames.Empty(BoneAnimationTracks.Num());
	RawAnimationTrackSkeletonMappings.Empty(BoneAnimationTracks.Num());

	for (const FBoneAnimationTrack& AnimTrack : BoneAnimationTracks)
	{
		RawAnimationTracks.Add(AnimTrack.InternalTrackData);
		RawAnimationTrackNames.Add(AnimTrack.Name);
		RawAnimationTrackSkeletonMappings.Add(AnimTrack.BoneTreeIndex);
	}

	RawCurveTracks.FloatCurves = CurveData.FloatCurves;
#if WITH_EDITOR
	RawCurveTracks.TransformCurves = CurveData.TransformCurves;
#endif
}

FRichCurve* UAnimDataModel::GetMutableRichCurve(const FAnimationCurveIdentifier& CurveIdentifier)
{
	FRichCurve* RichCurve = nullptr;

	if (CurveIdentifier.IsValid())
	{
		if (CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Float)
		{
			FFloatCurve* Curve = FindMutableFloatCurveById(CurveIdentifier);
			if (Curve)
			{
				RichCurve = &Curve->FloatCurve;
			}
		}
		else if (CurveIdentifier.CurveType == ERawCurveTrackTypes::RCT_Transform)
		{
			if (CurveIdentifier.Channel != ETransformCurveChannel::Invalid && CurveIdentifier.Axis != EVectorCurveChannel::Invalid)
			{
				// Dealing with transform curve
				FTransformCurve* TransformCurve = FindMutableTransformCurveById(CurveIdentifier);
				if (TransformCurve)
				{
					FVectorCurve* VectorCurve = TransformCurve->GetVectorCurveByIndex((int32)CurveIdentifier.Channel);
					if (VectorCurve)
					{
						RichCurve = &VectorCurve->FloatCurves[(int32)CurveIdentifier.Axis];
					}
				}

			}
		}
	}

	return RichCurve;
}

FBoneAnimationTrack* UAnimDataModel::FindMutableBoneTrackByName(FName Name)
{
	return BoneAnimationTracks.FindByPredicate([Name](FBoneAnimationTrack& Track)
	{
		return Track.Name == Name;
	});
}

const FBoneAnimationTrack* UAnimDataModel::FindBoneTrackByName(FName Name) const
{
	return BoneAnimationTracks.FindByPredicate([Name](FBoneAnimationTrack& Track)
	{
		return Track.Name == Name;
	});
}

FBoneAnimationTrack& UAnimDataModel::GetMutableBoneTrackByName(FName Name)
{
	FBoneAnimationTrack* TrackPtr = BoneAnimationTracks.FindByPredicate([Name](FBoneAnimationTrack& Track)
	{
		return Track.Name == Name;
	});

	checkf(TrackPtr, TEXT("Failed to find track by name"));

	return *TrackPtr;
}

FTransformCurve* UAnimDataModel::FindMutableTransformCurveById(const FAnimationCurveIdentifier& CurveIdentifier)
{
	for (FTransformCurve& TransformCurve : CurveData.TransformCurves)
	{
		if (TransformCurve.Name.UID == CurveIdentifier.InternalName.UID)
		{
			return &TransformCurve;
		}
	}

	return nullptr;
}

FFloatCurve* UAnimDataModel::FindMutableFloatCurveById(const FAnimationCurveIdentifier& CurveIdentifier)
{
	for (FFloatCurve& FloatCurve : CurveData.FloatCurves)
	{
		if (FloatCurve.Name.UID == CurveIdentifier.InternalName.UID)
		{
			return &FloatCurve;
		}
	}

	return nullptr;
}

FAnimCurveBase* UAnimDataModel::FindMutableCurveById(const FAnimationCurveIdentifier& CurveIdentifier)
{
	switch (CurveIdentifier.CurveType)
	{
	case ERawCurveTrackTypes::RCT_Float:
		return FindMutableFloatCurveById(CurveIdentifier);
	case ERawCurveTrackTypes::RCT_Transform:
		return FindMutableTransformCurveById(CurveIdentifier);
	}

	return nullptr;
}

