// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/PBDRigidDynamicSpringConstraints.h"


namespace Chaos
{
	template class TPBDConstraintRule<float, 3>;
	template class TPBDConstraintIslandRule<TPBDJointConstraints<float, 3>, float, 3>;
	template class TPBDConstraintIslandRule<TPBDPositionConstraints<float, 3>, float, 3>;
	template class TPBDConstraintIslandRule<TPBDRigidDynamicSpringConstraints<float, 3>, float, 3>;
	template class TPBDConstraintColorRule<TPBDCollisionConstraint<float, 3>, float, 3>;
}