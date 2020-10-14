// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAnimationProvider.h"
#include "Common/PagedArray.h"
#include "Model/IntervalTimeline.h"
#include "Model/PointTimeline.h"
#include "Containers/ArrayView.h"
#include "Model/IntervalTimeline.h"

namespace Trace { class IAnalysisSession; }
class FGameplayProvider;
struct FObjectInfo;
class USkeletalMesh;

class FAnimationProvider : public IAnimationProvider
{
public:
	static FName ProviderName;

	FAnimationProvider(Trace::IAnalysisSession& InSession, FGameplayProvider& InGameplayProvider);

	/** IAnimationProvider interface */
	virtual bool ReadSkeletalMeshPoseTimeline(uint64 InObjectId, TFunctionRef<void(const SkeletalMeshPoseTimeline&, bool)> Callback) const override;
	virtual void GetSkeletalMeshComponentSpacePose(const FSkeletalMeshPoseMessage& InMessage, const FSkeletalMeshInfo& InMeshInfo, FTransform& OutComponentToWorld, TArray<FTransform>& OutTransforms) const override;
	virtual void EnumerateSkeletalMeshCurveIds(uint64 InObjectId, TFunctionRef<void(uint32)> Callback) const override;
	virtual void EnumerateSkeletalMeshCurves(const FSkeletalMeshPoseMessage& InMessage, TFunctionRef<void(const FSkeletalMeshNamedCurve&)> Callback) const override;
	virtual bool ReadTickRecordTimeline(uint64 InObjectId, TFunctionRef<void(const TickRecordTimeline&)> Callback) const override;
	virtual void EnumerateTickRecordIds(uint64 InObjectId, TFunctionRef<void(uint64, int32)> Callback) const override;
	virtual bool ReadAnimGraphTimeline(uint64 InObjectId, TFunctionRef<void(const AnimGraphTimeline&)> Callback) const override;
	virtual bool ReadAnimNodesTimeline(uint64 InObjectId, TFunctionRef<void(const AnimNodesTimeline&)> Callback) const override;
	virtual bool ReadAnimNodeValuesTimeline(uint64 InObjectId, TFunctionRef<void(const AnimNodeValuesTimeline&)> Callback) const override;
	virtual bool ReadAnimSequencePlayersTimeline(uint64 InObjectId, TFunctionRef<void(const AnimSequencePlayersTimeline&)> Callback) const override;
	virtual bool ReadAnimBlendSpacePlayersTimeline(uint64 InObjectId, TFunctionRef<void(const BlendSpacePlayersTimeline&)> Callback) const override;
	virtual bool ReadStateMachinesTimeline(uint64 InObjectId, TFunctionRef<void(const StateMachinesTimeline&)> Callback) const override;
	virtual bool ReadNotifyTimeline(uint64 InObjectId, TFunctionRef<void(const AnimNotifyTimeline&)> Callback) const override;
	virtual void EnumerateNotifyStateTimelines(uint64 InObjectId, TFunctionRef<void(uint64, const AnimNotifyTimeline&)> Callback) const override;
	virtual bool ReadMontageTimeline(uint64 InObjectId, TFunctionRef<void(const AnimMontageTimeline&)> Callback) const override;
	virtual void EnumerateMontageIds(uint64 InObjectId, TFunctionRef<void(uint64)> Callback) const override;
	virtual const FSkeletalMeshInfo* FindSkeletalMeshInfo(uint64 InObjectId) const override;
	virtual const TCHAR* GetName(uint32 InId) const override;
	virtual FText FormatNodeKeyValue(const FAnimNodeValueMessage& InMessage) const override;
	virtual FText FormatNodeValue(const FAnimNodeValueMessage& InMessage) const override;

	// Check to see if any data is present for this provider. Used as an early-out when processing.
	bool HasAnyData() const;

	/** Add a tick record */
	void AppendTickRecord(uint64 InAnimInstanceId, double InTime, uint64 InAssetId, int32 InNodeId, float InBlendWeight, float InPlaybackTime, float InRootMotionWeight, float InPlayRate, float InBlendSpacePositionX, float InBlendSpacePositionY, uint16 InFrameCounter, bool bInLooping, bool bInIsBlendSpace);

	/** Add a skeletal mesh */
	void AppendSkeletalMesh(uint64 InObjectId, const TArrayView<const int32>& ParentIndices);

	/** Add a skeletal mesh pose/curves etc. */
	void AppendSkeletalMeshComponent(uint64 InObjectId, uint64 InMeshId, double InTime, uint16 InLodIndex, uint16 InFrameCounter, const TArrayView<const FTransform>& InPose, const TArrayView<const FSkeletalMeshNamedCurve>& InCurves);
	void AppendSkeletalMeshComponent(uint64 InObjectId, uint64 InMeshId, double InTime, uint16 InLodIndex, uint16 InFrameCounter, const FTransform& InComponentToWorld, const TArrayView<const FTransform>& InPose, const TArrayView<const uint32>& InCurveIds, const TArrayView<const float>& InCurveValues);

	/** Get a skeletal mesh for a specified path. If the mesh no longer exists, make a fake one */
	USkeletalMesh* GetSkeletalMesh(const TCHAR* InPath);

	/** Add a name, referenced by ID (for curves etc.) */
	void AppendName(uint32 InId, const TCHAR* InName);

	/** Add a skeletal mesh frame */
	void AppendSkeletalMeshFrame(uint64 InObjectId, double InTime, uint16 InFrameCounter);
	
	/** Add an anim graph */
	void AppendAnimGraph(uint64 InAnimInstanceId, double InStartTime, double InEndTime, int32 InNodeCount, uint16 InFrameCounter, uint8 InPhase);

	/** Add an anim node */
	void AppendAnimNodeStart(uint64 InAnimInstanceId, double InStartTime, uint16 InFrameCounter, int32 InTargetLinkId, int32 InSourceLinkId, float InWeight, float InRootMotionWeight, const TCHAR* InTargetNodeName, uint8 InPhase);

	/** Add anim node values */
	void AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, bool bInValue);
	void AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, int32 InValue);
	void AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, float InValue);
	void AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, const FVector2D& InValue);
	void AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, const FVector& InValue);
	void AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, const TCHAR* InValue);
	void AppendAnimNodeValueObject(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, uint64 InValue);
	void AppendAnimNodeValueClass(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, uint64 InValue);

	/** Add a sequence player record */
	void AppendAnimSequencePlayer(uint64 InAnimInstanceId, double InTime, int32 InNodeId, float InPosition, float InLength, uint16 InFrameCounter);

	/** Add a blend space player record */
	void AppendBlendSpacePlayer(uint64 InAnimInstanceId, double InTime, int32 InNodeId, uint64 InBlendSpaceId, float InPositionX, float InPositionY, float InPositionZ);

	/** Add a state machine state */
	void AppendStateMachineState(uint64 InAnimInstanceId, double InTime, int32 InNodeId, int32 InStateMachineIndex, int32 InStateIndex, float InStateWeight, float InElapsedTime);

	/** Add an anim notify */
	void AppendNotify(uint64 InAnimInstanceId, double InTime, uint64 InAssetId, uint64 InNotifyId, uint32 InNameId, float InNotifyTime, float InNotifyDuration, EAnimNotifyMessageType InNotifyEventType);

	/** Append montage data */
	void AppendMontage(uint64 InAnimInstanceId, double InTime, uint64 InMontageId, uint32 InCurrentSectionNameId, uint32 InNextSectionNameId, float InWeight, float InDesiredWeight, uint16 InFrameCounter);

private:
	/** Add anim node values helper */
	void AppendAnimNodeValue(uint64 InAnimInstanceId, double InTime, uint16 InFrameCounter, int32 InNodeId, const TCHAR* InKey, FAnimNodeValueMessage& InMessage);

	/** Handle an object ending play - terminate existing scopes */
	void HandleObjectEndPlay(uint64 InObjectId, double InTime, const FObjectInfo& InObjectInfo);

private:
	Trace::IAnalysisSession& Session;
	FGameplayProvider& GameplayProvider;

	/** Maps into timeline arrays per-object */
	TMap<uint64, uint32> ObjectIdToTickRecordTimelineStorage;
	TMap<uint64, uint32> ObjectIdToSkeletalMeshPoseTimelines;
	TMap<uint64, uint32> ObjectIdToSkeletalMeshFrameTimelines;
	TMap<uint64, uint32> ObjectIdToAnimGraphTimelines;
	TMap<uint64, uint32> ObjectIdToAnimNodeTimelines;
	TMap<uint64, uint32> ObjectIdToStateMachineTimelines;
	TMap<uint64, uint32> ObjectIdToAnimNodeValueTimelines;
	TMap<uint64, uint32> ObjectIdToAnimSequencePlayerTimelines;
	TMap<uint64, uint32> ObjectIdToBlendSpacePlayerTimelines;
	TMap<uint64, uint32> ObjectIdToAnimNotifyStateTimelines;
	TMap<uint64, uint32> ObjectIdToAnimNotifyTimelines;
	TMap<uint64, uint32> ObjectIdToAnimMontageTimelines;

	/** All the skeletal mesh info we have seen, grow only for stable indices */
	TArray<FSkeletalMeshInfo> SkeletalMeshInfos;

	/** Map of skeletal mesh->index into SkeletalMeshInfos index */
	TMap<uint64, uint32> SkeletalMeshIdToIndexMap;

	/** Map of name IDs to name string */
	TMap<uint32, const TCHAR*> NameMap;
	
	struct FTickRecordTimelineStorage
	{
		TSet<TTuple<uint64, int32>> AssetIdAndPlayers;
		TSharedPtr<Trace::TPointTimeline<FTickRecordMessage>> Timeline;
	};

	struct FSkeletalMeshTimelineStorage
	{
		TSharedPtr<Trace::TIntervalTimeline<FSkeletalMeshPoseMessage>> Timeline;
		TSet<uint32> AllCurveIds;
	};

	struct FAnimNotifyStateTimelineStorage
	{
		TMap<uint64, uint32> NotifyIdToAnimNotifyStateTimeline;
		TArray<TSharedRef<Trace::TIntervalTimeline<FAnimNotifyMessage>>> Timelines;
	};

	struct FMontageTimelineStorage
	{
		TSharedPtr<Trace::TPointTimeline<FAnimMontageMessage>> Timeline;
		TSet<uint64> AllMontageIds;
	};

	/** Message storage */
	TArray<TSharedRef<FTickRecordTimelineStorage>> TickRecordTimelineStorage;
	TArray<TSharedRef<FSkeletalMeshTimelineStorage>> SkeletalMeshPoseTimelineStorage;
	TArray<TSharedRef<Trace::TIntervalTimeline<FSkeletalMeshFrameMessage>>> SkeletalMeshFrameTimelines;
	TArray<TSharedRef<Trace::TIntervalTimeline<FAnimGraphMessage>>> AnimGraphTimelines;
	TArray<TSharedRef<Trace::TPointTimeline<FAnimNodeMessage>>> AnimNodeTimelines;
	TArray<TSharedRef<Trace::TPointTimeline<FAnimNodeValueMessage>>> AnimNodeValueTimelines;
	TArray<TSharedRef<Trace::TPointTimeline<FAnimSequencePlayerMessage>>> AnimSequencePlayerTimelines;
	TArray<TSharedRef<Trace::TPointTimeline<FBlendSpacePlayerMessage>>> BlendSpacePlayerTimelines;
	TArray<TSharedRef<Trace::TPointTimeline<FAnimStateMachineMessage>>> StateMachineTimelines;
	TArray<TSharedRef<FAnimNotifyStateTimelineStorage>> AnimNotifyStateTimelineStorage;
	TArray<TSharedRef<Trace::TPointTimeline<FAnimNotifyMessage>>> AnimNotifyTimelines;
	TArray<TSharedRef<FMontageTimelineStorage>> AnimMontageTimelineStorage;
	TPagedArray<FTransform> SkeletalMeshPoseTransforms;
	TPagedArray<FSkeletalMeshNamedCurve> SkeletalMeshCurves;
	TPagedArray<int32> SkeletalMeshParentIndices;

	// Flag to indicate if any data is present
	bool bHasAnyData;
};