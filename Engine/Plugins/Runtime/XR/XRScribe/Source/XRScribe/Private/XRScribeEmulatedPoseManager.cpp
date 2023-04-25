// Copyright Epic Games, Inc. All Rights Reserved.

#include "XRScribeEmulatedPoseManager.h"
#include "XRScribeFileFormat.h"

namespace UE::XRScribe
{

XrReferenceSpaceType FOpenXRPoseManager::GetCapturedReferenceSpaceType(XrSpace CapturedSpace)
{
	XrReferenceSpaceType Result = XR_REFERENCE_SPACE_TYPE_MAX_ENUM;

	FOpenXRCreateReferenceSpacePacket* MatchingInfo = CapturedReferenceSpaces.FindByPredicate([CapturedSpace](const FOpenXRCreateReferenceSpacePacket& CreateRefSpaceInfo)
	{
		return CreateRefSpaceInfo.Space == CapturedSpace;
	});

	if (MatchingInfo != nullptr)
	{
		Result = MatchingInfo->ReferenceSpaceCreateInfo.referenceSpaceType;
	}

	return Result;
}

void FOpenXRPoseManager::RegisterCapturedReferenceSpace(const FOpenXRCreateReferenceSpacePacket& CreateReferenceSpacePacket)
{
	CapturedReferenceSpaces.Add(CreateReferenceSpacePacket);

	// TODO: check for doubles?

	if ((CreateReferenceSpacePacket.Result == XR_SUCCESS) &&
		(CreateReferenceSpacePacket.ReferenceSpaceCreateInfo.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE) &&
		TrackingSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE)
	{
		TrackingSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	}
}

void FOpenXRPoseManager::RegisterCapturedSpaceHistory(const TArray<FOpenXRLocateSpacePacket>& SpaceHistory)
{
	if (SpaceHistory.Num() == 0)
	{
		// error info
		return;
	}

	XrSpace CapturedLocatedSpace = SpaceHistory[0].Space;
	XrReferenceSpaceType RefSpaceType = GetCapturedReferenceSpaceType(CapturedLocatedSpace);

	if (RefSpaceType == XR_REFERENCE_SPACE_TYPE_MAX_ENUM)
	{
		return;
	}

	if (RefSpaceType != XR_REFERENCE_SPACE_TYPE_VIEW)
	{
		return;
	}

	XrSpace CapturedBaseSpace = SpaceHistory[0].BaseSpace;
	XrReferenceSpaceType BaseRefSpaceType = GetCapturedReferenceSpaceType(CapturedBaseSpace);

	if (BaseRefSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE)
	{
		return;
	}

	// we now have our special case: view in tracker space history
	// Pick the last one with matching frametime, as the 'latest' recorded query should have most accurate location.
	// We might want to convert to UE FTransform later on to facilitate easy transformations.

	TArray<FOpenXRLocateSpacePacket> SortedSpaceHistory = SpaceHistory;
	SortedSpaceHistory.Sort([](const FOpenXRLocateSpacePacket& A, const FOpenXRLocateSpacePacket& B)
	{
		return A.Time < B.Time;
	});

	// We could also do this in place, putting elements in the front of the sorted array
	// Basically 'remove duplicates'
	// Alt: use iterators instead of raw indices
	TArray<FOpenXRLocateSpacePacket> FilteredSpaceHistory;
	for (int32 LocationIndex = 0; LocationIndex < SortedSpaceHistory.Num() - 1; LocationIndex++)
	{
		if (SortedSpaceHistory[LocationIndex].Time != SortedSpaceHistory[LocationIndex + 1].Time)
		{
			FilteredSpaceHistory.Add(SortedSpaceHistory[LocationIndex]);
		}
	}
	FilteredSpaceHistory.Add(SortedSpaceHistory.Last());

	// in the 'future' code, this is where we'd convert these poses to primary tracker-space
	ReferencePoseHistories.Add(XR_REFERENCE_SPACE_TYPE_VIEW, MoveTemp(FilteredSpaceHistory));

	// TODO: hypothetically, we can create multiple reference spaces from the same reference space type
	// So we'd want to merge our histories
}

void FOpenXRPoseManager::RegisterEmulatedReferenceSpace(const XrReferenceSpaceCreateInfo& CreateInfo, XrSpace SpaceHandle)
{
	check(!EmulatedReferenceSpaceTypeMap.Contains(SpaceHandle));
	EmulatedReferenceSpaceTypeMap.Add(SpaceHandle, CreateInfo.referenceSpaceType);
}

void FOpenXRPoseManager::AddEmulatedFrameTime(XrTime Time, int32 FrameNum)
{
	// TODO: protect against repeated frame times. would a runtime even do that? I guess it could happen,
	// but this is for our runtime, and we won't do that for now
	// TODO: protect against repeated frame numbers?

	const int32 HistoryIndex = FrameNum % PoseHistorySize;
	FrameTimeHistory[HistoryIndex] = FrameTimeHistoryEntry({ Time , FrameNum });

	LastInsertedFrameIndex = HistoryIndex;
}

XrSpaceLocation FOpenXRPoseManager::GetEmulatedPoseForTime(XrSpace LocatingSpace, XrSpace BaseSpace, XrTime Time)
{
	// artifically constrain to view located in stage space
	if (!EmulatedReferenceSpaceTypeMap.Contains(BaseSpace) ||
		EmulatedReferenceSpaceTypeMap[BaseSpace] != XR_REFERENCE_SPACE_TYPE_STAGE ||
		!EmulatedReferenceSpaceTypeMap.Contains(LocatingSpace) ||
		EmulatedReferenceSpaceTypeMap[LocatingSpace] != XR_REFERENCE_SPACE_TYPE_VIEW)
	{
		XrSpaceLocation DummyLocation;
		DummyLocation.locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
		DummyLocation.pose.orientation = ToXrQuat(FQuat::Identity);
		DummyLocation.pose.position = ToXrVector(FVector::ZeroVector);

		return DummyLocation;
	}

	int32 SearchIndex = LastInsertedFrameIndex;
	int32 StopIndex = SearchIndex - PoseHistorySize;
	StopIndex = (StopIndex < 1) ? 1 : StopIndex;
	while (SearchIndex >= StopIndex)
	{
		const int32 HistoryIndex = SearchIndex % PoseHistorySize;
		if (FrameTimeHistory[HistoryIndex].Time == Time)
		{
			break;
		}

		SearchIndex--;
	}

	if (SearchIndex < StopIndex)
	{
		// We didn't find a match, just use most recent pose
		SearchIndex = LastInsertedFrameIndex;
	}

	const int32 PoseIndex = FrameTimeHistory[SearchIndex].FrameIndex;

	const TArray<FOpenXRLocateSpacePacket>& CapturedPoseHistories = ReferencePoseHistories[XR_REFERENCE_SPACE_TYPE_VIEW];
	const int32 CountCapturedPoses = CapturedPoseHistories.Num();
	const int32 CapturedIndex = PoseIndex % CountCapturedPoses;

	return CapturedPoseHistories[CapturedIndex].Location;
}

} // namespace UE::XRScribe
