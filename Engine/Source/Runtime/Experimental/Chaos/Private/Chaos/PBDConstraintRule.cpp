// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintRule.h"

#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDPositionConstraints.h"
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
			ConstraintGraph->AddConstraint(ContainerId, ConstraintHandle, ConstraintHandle->GetConstrainedParticles());
		}

	}

	template<class T_CONSTRAINTS>
	int32 TPBDConstraintGraphRuleImpl<T_CONSTRAINTS>::NumConstraints() const
	{ 
		return Constraints.NumConstraints(); 
	}

	template class TSimpleConstraintRule<TPBDCollisionConstraints<float, 3>>;
	template class TSimpleConstraintRule<FPBDJointConstraints>;

	template class TPBDConstraintGraphRuleImpl<TPBDCollisionConstraints<float, 3>>;
	template class TPBDConstraintGraphRuleImpl<FPBDJointConstraints>;
	template class TPBDConstraintGraphRuleImpl<TPBDPositionConstraints<float, 3>>;
	template class TPBDConstraintGraphRuleImpl<TPBDRigidDynamicSpringConstraints<float, 3>>;
	template class TPBDConstraintGraphRuleImpl<TPBDRigidSpringConstraints<float, 3>>;

	template class TPBDConstraintColorRule<TPBDCollisionConstraints<float, 3>>;
	template class TPBDConstraintIslandRule<FPBDJointConstraints>;
	template class TPBDConstraintIslandRule<TPBDPositionConstraints<float, 3>>;
	template class TPBDConstraintIslandRule<TPBDRigidDynamicSpringConstraints<float, 3>>;
	template class TPBDConstraintIslandRule<TPBDRigidSpringConstraints<float, 3>>;
}