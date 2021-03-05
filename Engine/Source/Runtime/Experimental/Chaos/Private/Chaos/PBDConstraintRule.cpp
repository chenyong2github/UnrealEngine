// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintRule.h"

#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/PBDSuspensionConstraints.h"
#include "Chaos/PBDRigidDynamicSpringConstraints.h"
#include "Chaos/PBDRigidSpringConstraints.h"

namespace Chaos
{

	template<class T_CONSTRAINTS>
	TPBDConstraintGraphRuleImpl<T_CONSTRAINTS>::TPBDConstraintGraphRuleImpl(FConstraints& InConstraints, int32 InPriority)
		: FPBDConstraintGraphRule(InPriority)
		, Constraints(InConstraints)
		, ConstraintGraph(nullptr)
	{
	}


	template<class T_CONSTRAINTS>
	void TPBDConstraintGraphRuleImpl<T_CONSTRAINTS>::BindToGraph(FPBDConstraintGraph& InContactGraph, uint32 InContainerId)
	{
		ConstraintGraph = &InContactGraph;
		ContainerId = InContainerId;
	}

	template<class T_CONSTRAINTS>
	void TPBDConstraintGraphRuleImpl<T_CONSTRAINTS>::UpdatePositionBasedState(const FReal Dt)
	{
		Constraints.UpdatePositionBasedState(Dt);
	}


	template<class T_CONSTRAINTS>
	void TPBDConstraintGraphRuleImpl<T_CONSTRAINTS>::AddToGraph()
	{
		ConstraintGraph->ReserveConstraints(Constraints.NumConstraints());

		for (typename FConstraints::FConstraintContainerHandle * ConstraintHandle : Constraints.GetConstraintHandles())
		{
			if (ConstraintHandle->IsEnabled())
			{
				ConstraintGraph->AddConstraint(ContainerId, ConstraintHandle, ConstraintHandle->GetConstrainedParticles());
			}
		}

	}

	template<class T_CONSTRAINTS>
	void TPBDConstraintGraphRuleImpl<T_CONSTRAINTS>::DisableConstraints(const TSet<FGeometryParticleHandle*>& RemovedParticles)
	{
		Constraints.DisableConstraints(RemovedParticles);
	}


	template<class T_CONSTRAINTS>
	int32 TPBDConstraintGraphRuleImpl<T_CONSTRAINTS>::NumConstraints() const
	{ 
		return Constraints.NumConstraints(); 
	}

	template class TSimpleConstraintRule<FPBDCollisionConstraints>;
	template class TSimpleConstraintRule<FPBDJointConstraints>;
	template class TSimpleConstraintRule<FPBDRigidSpringConstraints>;

	template class TPBDConstraintGraphRuleImpl<FPBDCollisionConstraints>;
	template class TPBDConstraintGraphRuleImpl<FPBDJointConstraints>;
	template class TPBDConstraintGraphRuleImpl<FPBDPositionConstraints>;
	template class TPBDConstraintGraphRuleImpl<FPBDSuspensionConstraints>;
	template class TPBDConstraintGraphRuleImpl<FPBDRigidDynamicSpringConstraints>;
	template class TPBDConstraintGraphRuleImpl<FPBDRigidSpringConstraints>;

	template class TPBDConstraintColorRule<FPBDCollisionConstraints>;
	template class TPBDConstraintIslandRule<FPBDJointConstraints>;
	template class TPBDConstraintIslandRule<FPBDPositionConstraints>;
	template class TPBDConstraintIslandRule<FPBDSuspensionConstraints>;
	template class TPBDConstraintIslandRule<FPBDRigidDynamicSpringConstraints>;
	template class TPBDConstraintIslandRule<FPBDRigidSpringConstraints>;
}