// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingTrackViewport;

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ETimingTrackFlags : uint32
{
	None = 0,
	IsVisible = (1 << 0),
	IsSelected = (1 << 1),
	IsHovered = (1 << 2),
	IsHeaderHovered = (1 << 3),
};
ENUM_CLASS_FLAGS(ETimingTrackFlags);

////////////////////////////////////////////////////////////////////////////////////////////////////

class FBaseTimingTrack
{
	friend class FTimingViewDrawHelper;

protected:
	FBaseTimingTrack(uint64 InId)
		: Id(InId)
		, PosY(0.0f)
		, Height(0.0f)
		, Flags(ETimingTrackFlags::IsVisible)
	{}

	virtual ~FBaseTimingTrack()
	{}

public:
	virtual void Reset()
	{
		Id = 0;
		PosY = 0.0f;
		Height = 0.0f;
		Flags = ETimingTrackFlags::IsVisible;
	}

	uint64 GetId() const { return Id; }

	float GetPosY() const { return PosY; }
	virtual void SetPosY(float InPosY) { PosY = InPosY; }

	float GetHeight() const { return Height; }
	virtual void SetHeight(float InHeight) { Height = InHeight; }

	bool IsVisible() const { return EnumHasAnyFlags(Flags, ETimingTrackFlags::IsVisible); }
	void Show() { Flags |= ETimingTrackFlags::IsVisible; OnVisibilityChanged(); }
	void Hide() { Flags &= ~ETimingTrackFlags::IsVisible; OnVisibilityChanged(); }
	void ToggleVisibility() { Flags ^= ETimingTrackFlags::IsVisible; OnVisibilityChanged(); }
	void SetVisibilityFlag(bool bIsVisible) { bIsVisible ? Show() : Hide(); }
	virtual void OnVisibilityChanged() {}

	bool IsSelected() const { return EnumHasAnyFlags(Flags, ETimingTrackFlags::IsSelected); }
	void Select() { Flags |= ETimingTrackFlags::IsSelected; OnSelectedFlagChanged(); }
	void Unselect() { Flags &= ~ETimingTrackFlags::IsSelected; OnSelectedFlagChanged(); }
	void ToggleSelectedFlag() { Flags ^= ETimingTrackFlags::IsSelected; OnSelectedFlagChanged(); }
	void SetSelectedFlag(bool bIsSelected) { bIsSelected ? Select() : Unselect(); }
	virtual void OnSelectedFlagChanged() {}

	bool IsHovered() const { return EnumHasAnyFlags(Flags, ETimingTrackFlags::IsHovered); }
	void SetHoveredState(bool bIsHovered) { bIsHovered ? Flags |= ETimingTrackFlags::IsHovered : Flags &= ~ETimingTrackFlags::IsHovered; }
	bool IsHeaderHovered() const { return EnumHasAllFlags(Flags, ETimingTrackFlags::IsHovered | ETimingTrackFlags::IsHeaderHovered); }
	void SetHeaderHoveredState(bool bIsHeaderHovered) { bIsHeaderHovered ? Flags |= ETimingTrackFlags::IsHeaderHovered : Flags &= ~ETimingTrackFlags::IsHeaderHovered; }
	virtual void UpdateHoveredState(float MouseX, float MouseY, const FTimingTrackViewport& Viewport) = 0;

	virtual void Tick(const double CurrentTime, const float DeltaTime) {}
	virtual void Update(const FTimingTrackViewport& Viewport) {}

	static uint64 GenerateId() { return IdGenerator++; }

private:
	uint64 Id;

	float PosY; // y position, in Slate units
	float Height; // height, in Slate units

	ETimingTrackFlags Flags;

	static uint64 IdGenerator;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
