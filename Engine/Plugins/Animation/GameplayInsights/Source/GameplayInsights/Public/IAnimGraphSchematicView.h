// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IAnimGraphSchematicView : public SCompoundWidget
{
	public:
		virtual void SetTimeMarker(double InTimeMarker) = 0;
	    virtual void SetAnimInstanceId(uint64 InAnimInstanceId) = 0;
};
