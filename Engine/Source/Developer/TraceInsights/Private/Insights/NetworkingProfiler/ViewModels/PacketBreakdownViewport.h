// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/AxisViewportDouble.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FPacketBreakdownViewport
{
public:
	FPacketBreakdownViewport()
	{
		Reset();
	}

	void Reset()
	{
		HorizontalAxisViewport.Reset();
		Height = 0.0f;
	}

	const FAxisViewportDouble& GetHorizontalAxisViewport() const { return HorizontalAxisViewport; }
	FAxisViewportDouble& GetHorizontalAxisViewport() { return HorizontalAxisViewport; }

	float GetWidth() const { return HorizontalAxisViewport.GetSize(); }
	float GetHeight() const { return Height; }

	bool SetSize(const float InWidth, const float InHeight)
	{
		const bool bWidthChanged = HorizontalAxisViewport.SetSize(InWidth);
		if (bWidthChanged || Height != InHeight)
		{
			Height = InHeight;
			OnSizeChanged();
			return true;
		}
		return false;
	}

private:
	void OnSizeChanged()
	{
	}

private:
	FAxisViewportDouble HorizontalAxisViewport;
	float Height;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
