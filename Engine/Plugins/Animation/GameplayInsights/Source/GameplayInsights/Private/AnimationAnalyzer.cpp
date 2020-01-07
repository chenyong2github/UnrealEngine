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
	Builder.RouteEvent(RouteId_SkeletalMeshFrame, "Animation", "SkeletalMeshFrame");
	Builder.RouteEvent(RouteId_AnimGraph, "Animation", "AnimGraph");
	Builder.RouteEvent(RouteId_AnimNodeStart, "Animation", "AnimNodeStart");
	Builder.RouteEvent(RouteId_AnimNodeValueBool, "Animation", "AnimNodeValueBool");
	Builder.RouteEvent(RouteId_AnimNodeValueInt, "Animation", "AnimNodeValueInt");
	Builder.RouteEvent(RouteId_AnimNodeValueFloat, "Animation", "AnimNodeValueFloat");
	Builder.RouteEvent(RouteId_AnimNodeValueVector, "Animation", "AnimNodeValueVector");
	Builder.RouteEvent(RouteId_AnimNodeValueString, "Animation", "AnimNodeValueString");
	Builder.RouteEvent(RouteId_AnimNodeValueObject, "Animation", "AnimNodeValueObject");
	Builder.RouteEvent(RouteId_AnimNodeValueClass, "Animation", "AnimNodeValueClass");
	Builder.RouteEvent(RouteId_AnimSequencePlayer, "Animation", "AnimSequencePlayer");
	Builder.RouteEvent(RouteId_BlendSpacePlayer, "Animation", "BlendSpacePlayer");
	Builder.RouteEvent(RouteId_StateMachineState, "Animation", "StateMachineState");
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
		uint64 AnimInstanceId = EventData.GetValue<uint64>("AnimInstanceId");
		uint64 AssetId = EventData.GetValue<uint64>("AssetId");
		int32 NodeId = EventData.GetValue<int32>("NodeId");
		float BlendWeight = EventData.GetValue<float>("BlendWeight");
		float PlaybackTime = EventData.GetValue<float>("PlaybackTime");
		float RootMotionWeight = EventData.GetValue<float>("RootMotionWeight");
		float PlayRate = EventData.GetValue<float>("PlayRate");
		float BlendSpacePositionX = EventData.GetValue<float>("BlendSpacePositionX");
		float BlendSpacePositionY = EventData.GetValue<float>("BlendSpacePositionY");
		uint16 FrameCounter = EventData.GetValue<uint16>("FrameCounter");
		bool bLooping = EventData.GetValue<bool>("Looping");
		bool bIsBlendSpace = EventData.GetValue<bool>("IsBlendSpace");
		AnimationProvider.AppendTickRecord(AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), AssetId, NodeId, BlendWeight, PlaybackTime, RootMotionWeight, PlayRate, BlendSpacePositionX, BlendSpacePositionY, FrameCounter, bLooping, bIsBlendSpace);
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
	case RouteId_SkeletalMeshFrame:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 ComponentId = EventData.GetValue<uint64>("ComponentId");
		uint16 FrameCounter = EventData.GetValue<uint16>("FrameCounter");
		AnimationProvider.AppendSkeletalMeshFrame(ComponentId, Context.SessionContext.TimestampFromCycle(Cycle), FrameCounter);
		break;
	}
	case RouteId_AnimGraph:
	{
		uint64 StartCycle = EventData.GetValue<uint64>("StartCycle");
		uint64 EndCycle = EventData.GetValue<uint64>("EndCycle");
		uint64 AnimInstanceId = EventData.GetValue<uint64>("AnimInstanceId");
		int32 NodeCount = EventData.GetValue<int32>("NodeCount");
		uint16 FrameCounter = EventData.GetValue<uint16>("FrameCounter");
		uint8 Phase = EventData.GetValue<uint8>("Phase");
		AnimationProvider.AppendAnimGraph(AnimInstanceId, Context.SessionContext.TimestampFromCycle(StartCycle), Context.SessionContext.TimestampFromCycle(EndCycle), NodeCount, FrameCounter, Phase);
		break;
	}
	case RouteId_AnimNodeStart:
	{
		uint64 StartCycle = EventData.GetValue<uint64>("StartCycle");
		uint64 AnimInstanceId = EventData.GetValue<uint64>("AnimInstanceId");
		int32 NodeId = EventData.GetValue<int32>("NodeId");
		int32 PreviousNodeId = EventData.GetValue<int32>("PreviousNodeId");
		float Weight = EventData.GetValue<float>("Weight");
		float RootMotionWeight = EventData.GetValue<float>("RootMotionWeight");
		uint16 FrameCounter = EventData.GetValue<uint16>("FrameCounter");
		uint8 Phase = EventData.GetValue<uint8>("Phase");
		const TCHAR* TargetNodeName = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());
		AnimationProvider.AppendAnimNodeStart(AnimInstanceId, Context.SessionContext.TimestampFromCycle(StartCycle), FrameCounter, NodeId, PreviousNodeId, Weight, RootMotionWeight, TargetNodeName, Phase);
		break;
	}
	case RouteId_AnimNodeValueBool:
	case RouteId_AnimNodeValueInt:
	case RouteId_AnimNodeValueFloat:
	case RouteId_AnimNodeValueVector:
	case RouteId_AnimNodeValueString:
	case RouteId_AnimNodeValueObject:
	case RouteId_AnimNodeValueClass:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 AnimInstanceId = EventData.GetValue<uint64>("AnimInstanceId");
		int32 NodeId = EventData.GetValue<int32>("NodeId");
		int32 KeyLength = EventData.GetValue<uint32>("KeyLength");
		uint16 FrameCounter = EventData.GetValue<uint16>("FrameCounter");
		const TCHAR* Key = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());

		switch (RouteId)
		{
		case RouteId_AnimNodeValueBool:
		{
			bool Value = EventData.GetValue<bool>("Value");
			AnimationProvider.AppendAnimNodeValue(AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), FrameCounter, NodeId, Key, Value);
			break;
		}
		case RouteId_AnimNodeValueInt:
		{
			int32 Value = EventData.GetValue<int32>("Value");
			AnimationProvider.AppendAnimNodeValue(AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), FrameCounter, NodeId, Key, Value);
			break;
		}
		case RouteId_AnimNodeValueFloat:
		{
			float Value = EventData.GetValue<float>("Value");
			AnimationProvider.AppendAnimNodeValue(AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), FrameCounter, NodeId, Key, Value);
			break;
		}
		case RouteId_AnimNodeValueVector:
		{
			FVector Value(EventData.GetValue<float>("ValueX"), EventData.GetValue<float>("ValueY"), EventData.GetValue<float>("ValueZ"));
			AnimationProvider.AppendAnimNodeValue(AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), FrameCounter, NodeId, Key, Value);
			break;
		}
		case RouteId_AnimNodeValueString:
		{
			const TCHAR* Value = reinterpret_cast<const TCHAR*>(EventData.GetAttachment()) + KeyLength;
			AnimationProvider.AppendAnimNodeValue(AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), FrameCounter, NodeId, Key, Value);
			break;
		}
		case RouteId_AnimNodeValueObject:
		{
			uint64 Value = EventData.GetValue<uint64>("Value");
			AnimationProvider.AppendAnimNodeValueObject(AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), FrameCounter, NodeId, Key, Value);
			break;
		}
		case RouteId_AnimNodeValueClass:
		{
			uint64 Value = EventData.GetValue<uint64>("Value");
			AnimationProvider.AppendAnimNodeValueClass(AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), FrameCounter, NodeId, Key, Value);
			break;
		}
		}
		break;
	}
	case RouteId_AnimSequencePlayer:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 AnimInstanceId = EventData.GetValue<uint64>("AnimInstanceId");
		int32 NodeId = EventData.GetValue<int32>("NodeId");
		float Position = EventData.GetValue<float>("Position");
		float Length = EventData.GetValue<float>("Length");
		int32 FrameCount = EventData.GetValue<int32>("FrameCount");
		AnimationProvider.AppendAnimSequencePlayer(AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), NodeId, Position, Length, FrameCount);
		break;
	}
	case RouteId_BlendSpacePlayer:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 AnimInstanceId = EventData.GetValue<uint64>("AnimInstanceId");
		uint64 BlendSpaceId = EventData.GetValue<uint64>("BlendSpaceId");
		int32 NodeId = EventData.GetValue<int32>("NodeId");
		float PositionX = EventData.GetValue<float>("PositionX");
		float PositionY = EventData.GetValue<float>("PositionY");
		float PositionZ = EventData.GetValue<float>("PositionZ");
		AnimationProvider.AppendBlendSpacePlayer(AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), NodeId, BlendSpaceId, PositionX, PositionY, PositionZ);
		break;
	}
	case RouteId_StateMachineState:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 AnimInstanceId = EventData.GetValue<uint64>("AnimInstanceId");
		int32 NodeId = EventData.GetValue<int32>("NodeId");
		int32 StateMachineIndex = EventData.GetValue<int32>("StateMachineIndex");
		int32 StateIndex = EventData.GetValue<int32>("StateIndex");
		float StateWeight = EventData.GetValue<float>("StateWeight");
		float ElapsedTime = EventData.GetValue<float>("ElapsedTime");
		AnimationProvider.AppendStateMachineState(AnimInstanceId, Context.SessionContext.TimestampFromCycle(Cycle), NodeId, StateMachineIndex, StateIndex, StateWeight, ElapsedTime);
		break;
	}
	}

	return true;
}