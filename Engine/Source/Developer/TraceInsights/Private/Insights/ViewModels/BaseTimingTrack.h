// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingTrackViewport;

class FBaseTimingTrack
{
	friend class FTimingViewDrawHelper;

protected:
	FBaseTimingTrack(uint64 InId)
		: Id(InId)
		, Y(0.0f)
		, H(0.0f)
		, bIsVisible(true)
		, bIsSelected(false)
		, bIsHovered(false)
	{}

	virtual ~FBaseTimingTrack()
	{}

public:
	virtual void Reset()
	{
		Id = 0;
		Y = 0.0f;
		H = 0.0f;
		bIsVisible = true;
		bIsSelected = false;
		bIsHovered = false;
	}

	uint64 GetId() const { return Id; }

	float GetPosY() const { return Y; }
	virtual void SetPosY(float InPosY) { Y = InPosY; }

	float GetHeight() const { return H; }
	virtual void SetHeight(float InHeight) { H = InHeight; }

	bool IsVisible() const { return bIsVisible; }
	void Show() { SetVisibilityFlag(true); }
	void Hide() { SetVisibilityFlag(false); }
	virtual void SetVisibilityFlag(bool bInIsVisible) { bIsVisible = bInIsVisible; }

	bool IsSelected() const { return bIsSelected; }
	void Select() { SetSelectedFlag(true); }
	void Unselect() { SetSelectedFlag(false); }
	virtual void SetSelectedFlag(bool bInIsSelected) { bIsSelected = bInIsSelected; }

	bool IsHovered() const { return bIsHovered; }
	void SetHoveredState(bool bInIsHovered) { bIsHovered = bInIsHovered; }
	bool IsHeaderHovered() const { return bIsHovered && bIsHeaderHovered; }
	void SetHeaderHoveredState(bool bInIsHeaderHovered) { bIsHeaderHovered = bInIsHeaderHovered; }
	virtual void UpdateHoveredState(float MX, float MY, const FTimingTrackViewport& Viewport) = 0;

	virtual void Tick(const double InCurrentTime, const float InDeltaTime) {}
	virtual void Update(const FTimingTrackViewport& InViewport) {}

protected:
	uint64 Id;

	float Y; // y position, in Slate units
	float H; // height, in Slate units

	bool bIsVisible;
	bool bIsSelected;
	bool bIsHovered;
	bool bIsHeaderHovered;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
