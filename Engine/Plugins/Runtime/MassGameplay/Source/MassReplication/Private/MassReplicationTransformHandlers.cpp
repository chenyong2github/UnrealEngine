// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassReplicationTransformHandlers.h"
#include "EntityQuery.h"
#include "MassCommonFragments.h"
#include "AIHelpers.h"

void FMassReplicationProcessorTransformHandlerBase::AddRequirements(FLWComponentQuery& InQuery)
{
	InQuery.AddRequirement<FDataFragment_Transform>(ELWComponentAccess::ReadWrite);
}

void FMassReplicationProcessorTransformHandlerBase::CacheComponentViews(FLWComponentSystemExecutionContext& ExecContext)
{
	TransformList = ExecContext.GetMutableComponentView<FDataFragment_Transform>();
}

void FMassReplicationProcessorPositionYawHandler::AddEntity(const int32 EntityIdx, FReplicatedAgentPositionYawData& InOutReplicatedPositionYawData) const
{
	const FDataFragment_Transform& TransformFragment = TransformList[EntityIdx];
	InOutReplicatedPositionYawData.SetPosition(TransformFragment.GetTransform().GetLocation());

	if (const TOptional<float> Yaw = UE::AI::GetYawFromQuaternion(TransformFragment.GetTransform().GetRotation()))
	{
		InOutReplicatedPositionYawData.SetYaw(Yaw.GetValue());
	}
}
