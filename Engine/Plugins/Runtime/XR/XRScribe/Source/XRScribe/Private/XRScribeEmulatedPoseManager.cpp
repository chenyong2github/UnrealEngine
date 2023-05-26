// Copyright Epic Games, Inc. All Rights Reserved.

#include "XRScribeEmulatedPoseManager.h"
#include "XRScribeFileFormat.h"

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogXRScribePoseManager, Log, All);

namespace UE::XRScribe
{

void FOpenXRPoseManager::RegisterCapturedReferenceSpaces(const TArray<FOpenXRCreateReferenceSpacePacket>& CreateReferenceSpacePackets)
{
	CapturedReferenceSpaces.Append(CreateReferenceSpacePackets);

	// TODO: check for doubles?
}

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

void FOpenXRPoseManager::RegisterCapturedActions(const TArray <FOpenXRCreateActionPacket>& CreateActionPackets)
{
	for (const FOpenXRCreateActionPacket& CreateAction : CreateActionPackets)
	{
		if (CreateAction.ActionType == XR_ACTION_TYPE_POSE_INPUT)
		{
			CapturedPoseActions.Add(CreateAction.Action, CreateAction);
		}
	}
}

void FOpenXRPoseManager::RegisterCapturedActionSpaces(const TArray<FOpenXRCreateActionSpacePacket>& CreateActionSpacePackets)
{
	CapturedActionSpaces.Append(CreateActionSpacePackets);
}

bool FOpenXRPoseManager::VerifyCapturedActionSpace(XrSpace CapturedSpace)
{
	return CapturedActionSpaces.ContainsByPredicate([CapturedSpace](const FOpenXRCreateActionSpacePacket& CreateActionSpaceInfo)
	{
		return CreateActionSpaceInfo.Space == CapturedSpace;
	});
}

bool FOpenXRPoseManager::FilterSpaceHistory(const TArray<FOpenXRLocateSpacePacket>& RawHistory, TArray<FOpenXRLocateSpacePacket>& FilteredHistory)
{
	XrSpace CapturedBaseSpace = RawHistory[0].BaseSpace;
	XrReferenceSpaceType BaseRefSpaceType = GetCapturedReferenceSpaceType(CapturedBaseSpace);

	if (BaseRefSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE)
	{
		UE_LOG(LogXRScribePoseManager, Warning, TEXT("Unable to process space history with non-stage base space"));
		return false;
	}

	// we now have our core case: locate space relative to stage tracker
	// Pick the last one with matching frametime, as the 'latest' recorded query should have most accurate location.
	// We might want to convert to UE FTransform later on to facilitate easy transformations.
	// TODO: account for different starting poses in stage space

	FilteredHistory = RawHistory;
	FilteredHistory.Sort([](const FOpenXRLocateSpacePacket& A, const FOpenXRLocateSpacePacket& B)
	{
		return A.Time < B.Time;
	});

	// We could also do this in place like Algo::Unique, putting elements in the front of the sorted array
	// But we want to select the 'last' element in the run sequence
	int32 InsertIndex = 0;
	for (int32 LocationIndex = 0; LocationIndex < FilteredHistory.Num() - 1; LocationIndex++)
	{
		if (FilteredHistory[LocationIndex].Time != FilteredHistory[LocationIndex + 1].Time)
		{
			FilteredHistory[InsertIndex++] = FilteredHistory[LocationIndex];
		}
	}
	FilteredHistory[InsertIndex++] = FilteredHistory.Last();

	FilteredHistory.SetNumUninitialized(InsertIndex);

	// TODO: clients should transform these sorted poses into a unified space independent of changing base space

	return true;
}

void FOpenXRPoseManager::ProcessCapturedReferenceSpaceHistory(const TArray<FOpenXRLocateSpacePacket>& SpaceHistory)
{
	const XrSpace CapturedLocatedSpace = SpaceHistory[0].Space;
	const XrReferenceSpaceType RefSpaceType = GetCapturedReferenceSpaceType(CapturedLocatedSpace);

	if (RefSpaceType != XR_REFERENCE_SPACE_TYPE_VIEW)
	{
		UE_LOG(LogXRScribePoseManager, Warning, TEXT("Unable to process space history for non-view reference spaces"));
		return;
	}

	TArray<FOpenXRLocateSpacePacket> FilteredSpaceHistory;
	if (FilterSpaceHistory(SpaceHistory, FilteredSpaceHistory))
	{
		ReferencePoseHistories.Add(XR_REFERENCE_SPACE_TYPE_VIEW, MoveTemp(FilteredSpaceHistory));
	}
}

void FOpenXRPoseManager::ProcessCapturedActionSpaceHistory(const TArray<FOpenXRLocateSpacePacket>& SpaceHistory)
{
	const XrSpace LocatedSpace = SpaceHistory[0].Space;

	FOpenXRCreateActionSpacePacket* MatchingInfo = CapturedActionSpaces.FindByPredicate([LocatedSpace](const FOpenXRCreateActionSpacePacket& CreateActionSpaceInfo)
	{
		return CreateActionSpaceInfo.Space == LocatedSpace;

	});
	check(MatchingInfo);

	const FOpenXRCreateActionPacket& CreateAction = CapturedPoseActions[MatchingInfo->ActionSpaceCreateInfo.action];

	check(CreateAction.ActionType == XR_ACTION_TYPE_POSE_INPUT);

	TArray<FOpenXRLocateSpacePacket> FilteredSpaceHistory;
	if (FilterSpaceHistory(SpaceHistory, FilteredSpaceHistory))
	{
		const FName FetchedActionName(CreateAction.ActionName.GetData());
		ActionPoseHistories.Add(FetchedActionName, MoveTemp(FilteredSpaceHistory));

		// TODO: we should be resilient to multiple action spaces per name
	}
}

void FOpenXRPoseManager::RegisterCapturedSpaceHistories(const TMap<XrSpace, TArray<FOpenXRLocateSpacePacket>>& SpaceHistories)
{
	for (const auto& SpaceHistoryTuple : SpaceHistories)
	{
		const TArray<FOpenXRLocateSpacePacket>& SpaceHistory = SpaceHistoryTuple.Value;
		if (SpaceHistory.Num() == 0)
		{
			continue;
		}

		const XrSpace CapturedLocatedSpace = SpaceHistoryTuple.Key;
		check(CapturedLocatedSpace == SpaceHistory[0].Space);

		const XrReferenceSpaceType RefSpaceType = GetCapturedReferenceSpaceType(CapturedLocatedSpace);
		if (RefSpaceType != XR_REFERENCE_SPACE_TYPE_MAX_ENUM)
		{
			ProcessCapturedReferenceSpaceHistory(SpaceHistory);
		}
		else if (VerifyCapturedActionSpace(CapturedLocatedSpace))
		{
			ProcessCapturedActionSpaceHistory(SpaceHistory);
		}
		else
		{
			UE_LOG(LogXRScribePoseManager, Warning, TEXT("Unable to process space history for captured space: %p"), reinterpret_cast<const void*>(CapturedLocatedSpace));
		}

	}
}

void FOpenXRPoseManager::RegisterEmulatedReferenceSpace(const XrReferenceSpaceCreateInfo& CreateInfo, XrSpace SpaceHandle)
{
	check(!EmulatedReferenceSpaceTypeMap.Contains(SpaceHandle));
	EmulatedReferenceSpaceTypeMap.Add(SpaceHandle, CreateInfo.referenceSpaceType);
}

void FOpenXRPoseManager::RegisterEmulatedActionSpace(TStaticArray<ANSICHAR, XR_MAX_ACTION_NAME_SIZE>& ActionName, const XrActionSpaceCreateInfo& SpaceCreateInfo, XrSpace SpaceHandle)
{
	check(!EmulatedActionSpaceNameMap.Contains(SpaceHandle));

	const FName EmulatedActionName(ActionName.GetData());
	EmulatedActionSpaceNameMap.Add(SpaceHandle, EmulatedActionName);
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

bool FOpenXRPoseManager::DoesActionContainPoseHistory(TStaticArray<ANSICHAR, XR_MAX_ACTION_NAME_SIZE>& ActionName)
{
	return ActionPoseHistories.Contains(ActionName.GetData());
}

XrSpaceLocation FOpenXRPoseManager::GetEmulatedPoseForTime(XrSpace LocatingSpace, XrSpace BaseSpace, XrTime Time)
{
	XrSpaceLocation DummyLocation;
	DummyLocation.locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	DummyLocation.pose.orientation = ToXrQuat(FQuat::Identity);
	DummyLocation.pose.position = ToXrVector(FVector::ZeroVector);

	// artifically constrain locations to stage space
	if (!EmulatedReferenceSpaceTypeMap.Contains(BaseSpace) ||
			EmulatedReferenceSpaceTypeMap[BaseSpace] != XR_REFERENCE_SPACE_TYPE_STAGE)
	{
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

	if (EmulatedReferenceSpaceTypeMap.Contains(LocatingSpace) && EmulatedReferenceSpaceTypeMap[LocatingSpace] == XR_REFERENCE_SPACE_TYPE_VIEW)
	{
		const TArray<FOpenXRLocateSpacePacket>& CapturedPoseHistories = ReferencePoseHistories[XR_REFERENCE_SPACE_TYPE_VIEW];
		const int32 CountCapturedPoses = CapturedPoseHistories.Num();
		const int32 CapturedIndex = PoseIndex % CountCapturedPoses;

		return CapturedPoseHistories[CapturedIndex].Location;
	}
	else if (EmulatedActionSpaceNameMap.Contains(LocatingSpace) && ActionPoseHistories.Contains(EmulatedActionSpaceNameMap[LocatingSpace]))
	{
		const FName ActionName = EmulatedActionSpaceNameMap[LocatingSpace];
		const TArray<FOpenXRLocateSpacePacket>& CapturedPoseHistories = ActionPoseHistories[ActionName]; // TODO: should actually check that that we have a history...
		const int32 CountCapturedPoses = CapturedPoseHistories.Num();
		const int32 CapturedIndex = PoseIndex % CountCapturedPoses;

		return CapturedPoseHistories[CapturedIndex].Location;
	}

	return DummyLocation;
}

} // namespace UE::XRScribe
