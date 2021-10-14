// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SegmentTypes.h"
#include "BoxTypes.h"

namespace UE
{
namespace Geometry
{

// Return true if Segment and Box intersect and false otherwise
template<typename Real>
bool TestIntersection(const TSegment2<Real>& Segment, const TAxisAlignedBox2<Real>& Box);
		
} // namespace UE::Geometry
} // namespace UE
