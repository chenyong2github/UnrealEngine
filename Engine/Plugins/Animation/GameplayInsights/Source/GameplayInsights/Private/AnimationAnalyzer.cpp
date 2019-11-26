// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimationAnalyzer.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "AnimationProvider.h"
#include "Containers/ArrayView.h"

FAnimationAnalyzer::FAnimationAnalyzer(Trace::IAnalysisSession& InSession, FAnimationProvider& InAnimationProvider)
	: Session(InSession)
	, AnimationProvider(InAnimationProvider)
{
}

void FAnimationAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_TickRecord, "Animation", "TickRecord");
	Builder.RouteEvent(RouteId_SkeletalMesh, "Animation", "SkeletalMesh");
	Builder.RouteEvent(RouteId_SkeletalMeshComponent, "Animation", "SkeletalMeshComponent");
	Builder.RouteEvent(RouteId_Name, "Animation", "Name");
}

bool FAnimationAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_TickRecord:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 ComponentId = EventData.GetValue<uint64>("ComponentId");
		uint64 AnimInstanceId = EventData.GetValue<uint64>("AnimInstanceId");
		uint64 AssetId = EventData.GetValue<uint64>("AssetId");
		float BlendWeight = EventData.GetValue<float>("BlendWeight");
		float PlaybackTime = EventData.GetValue<float>("PlaybackTime");
		float RootMotionWeight = EventData.GetValue<float>("RootMotionWeight");
		float PlayRate = EventData.GetValue<float>("PlayRate");
		uint16 FrameCounter = EventData.GetValue<uint16>("FrameCounter");
		bool bLooping = EventData.GetValue<bool>("Looping");
		AnimationProvider.AppendTickRecord(ComponentId, AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), AssetId, BlendWeight, PlaybackTime, RootMotionWeight, PlayRate, FrameCounter, bLooping);
		break;
	}
	case RouteId_SkeletalMesh:
	{
		uint64 Id = EventData.GetValue<uint64>("Id");
		uint32 BoneCount = EventData.GetValue<uint32>("BoneCount");
		TArrayView<const int32> ParentIndices(reinterpret_cast<const int32*>(EventData.GetAttachment()), BoneCount);
		AnimationProvider.AppendSkeletalMesh(Id, ParentIndices);
		break;
	}
	case RouteId_SkeletalMeshComponent:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 ComponentId = EventData.GetValue<uint64>("ComponentId");
		uint64 MeshId = EventData.GetValue<uint64>("MeshId");
		uint32 BoneCount = EventData.GetValue<uint32>("BoneCount");
		uint32 CurveCount = EventData.GetValue<uint32>("CurveCount");
		uint16 FrameCounter = EventData.GetValue<uint16>("FrameCounter");
		uint16 LodIndex = EventData.GetValue<uint16>("LodIndex");
		TArrayView<const FTransform> Pose(reinterpret_cast<const FTransform*>(EventData.GetAttachment()), BoneCount);
		TArrayView<const FSkeletalMeshNamedCurve> Curves(reinterpret_cast<const FSkeletalMeshNamedCurve*>(EventData.GetAttachment() + (sizeof(FTransform) * BoneCount)), CurveCount);
		AnimationProvider.AppendSkeletalMeshComponent(ComponentId, MeshId, Context.SessionContext.TimestampFromCycle(Cycle), LodIndex, FrameCounter, Pose, Curves);
		break;
	}
	case RouteId_Name:
	{
		uint32 Id = EventData.GetValue<uint32>("Id");
		AnimationProvider.AppendName(Id, reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
		break;
	}
	}

	return true;
}