// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"

struct FSampledSequenceGridMetrics
{
	int32 NumMinorGridDivisions = 0;
	uint32 SampleRate = 0;
	uint32 StartFrame = 0;
	double PixelsPerFrame = 0;
	double FirstMajorTickX = 0;
	double MajorGridXStep = 0;
};

class ISampledSequenceGridService
{
public:
	virtual ~ISampledSequenceGridService() = default;
	virtual const FSampledSequenceGridMetrics GetGridMetrics() const = 0;
	virtual const float SnapPositionToClosestFrame(const float InPixelPosition) const = 0;
};