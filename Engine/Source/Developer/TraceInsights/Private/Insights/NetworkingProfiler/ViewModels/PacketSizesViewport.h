// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/FrameTrackViewport.h"
//#include "Insights/ViewModels/IndexAxisViewport.h"
//#include "Insights/ViewModels/ValueAxisViewport.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FPacketSizesViewport
{
private:
	static constexpr float SLATE_UNITS_TOLERANCE = 0.1f;

public:
	FPacketSizesViewport()
	{
		Reset();
	}

	void Reset()
	{
		HorizontalAxisViewport.Reset();
		VerticalAxisViewport.Reset();
	}

	const FIndexAxisViewport& GetHorizontalAxisViewport() const { return HorizontalAxisViewport; }
	FIndexAxisViewport& GetHorizontalAxisViewport() { return HorizontalAxisViewport; }

	const FValueAxisViewport& GetVerticalAxisViewport() const { return VerticalAxisViewport; }
	FValueAxisViewport& GetVerticalAxisViewport() { return VerticalAxisViewport; }

	float GetWidth() const { return HorizontalAxisViewport.GetSize(); }
	float GetHeight() const { return VerticalAxisViewport.GetSize(); }

	bool SetSize(const float InWidth, const float InHeight)
	{
		const bool bWidthChanged = HorizontalAxisViewport.SetSize(InWidth);
		const bool bHeightChanged = VerticalAxisViewport.SetSize(InHeight);
		if (bWidthChanged || bHeightChanged)
		{
			OnSizeChanged();
			return true;
		}
		return false;
	}

	float GetSampleWidth() const { return HorizontalAxisViewport.GetSampleSize(); }
	int32 GetNumPacketsPerSample() const { return HorizontalAxisViewport.GetNumIndicesPerSample(); }
	int32 GetFirstFrameIndex() const { return HorizontalAxisViewport.GetIndexAtOffset(0.0f); }

private:
	void OnSizeChanged()
	{
	}

private:
	FIndexAxisViewport HorizontalAxisViewport;
	FValueAxisViewport VerticalAxisViewport;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
