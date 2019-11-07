// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintRule.h"

#include "Chaos/PBD6DJointConstraints.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/PBDRigidDynamicSpringConstraints.h"


namespace Chaos
{
	template class TPBDConstraintColorRule<TPBDCollisionConstraint<float, 3>>;
	template class TPBDConstraintIslandRule<FPBD6DJointConstraints>;
	template class TPBDConstraintIslandRule<FPBDJointConstraints>;
	template class TPBDConstraintIslandRule<TPBDPositionConstraints<float, 3>>;
	template class TPBDConstraintIslandRule<TPBDRigidDynamicSpringConstraints<float, 3>>;
}