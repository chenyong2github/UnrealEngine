// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/RichCurve.h"
#include "InterchangeCommonAnimationPayload.h"

namespace UE::Interchange
{
	struct FAnimationCurveTransformPayloadData
	{
		EInterchangeTransformCurveChannel TransformChannel;
		FRichCurve Curve;
	};
	struct FAnimationTransformPayloadData
	{
		TArray<FAnimationCurveTransformPayloadData> TransformCurves;
	};
}//ns UE::Interchange
