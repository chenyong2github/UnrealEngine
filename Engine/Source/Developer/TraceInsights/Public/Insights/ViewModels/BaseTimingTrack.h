// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/SharedPointer.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingTrackViewport;
class FMenuBuilder;
namespace Trace { class IAnalysisSession; };

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ETimingTrackFlags : uint32
{
	None = 0,
	IsVisible = (1 << 0),
	IsDirty = (1 << 1),
	IsSelected = (1 << 2),
	IsHovered = (1 << 3),
	IsHeaderHovered = (1 << 4),
};
ENUM_CLASS_FLAGS(ETimingTrackFlags);

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTS_API FBaseTimingTrack : public TSharedFromThis<FBaseTimingTrack>
{
	friend class FTimingViewDrawHelper;

protected:
	explicit FBaseTimingTrack(uint64 InId, const FName& InType = NAME_None, const FName& InSubType = NAME_None, const FString& InName = FString())
		: Id(InId)
		, Type(InType)
		, SubType(InSubType)
		, Name(InName)
		, Order(0)
		, PosY(0.0f)
		, Height(0.0f)
		, Flags(ETimingTrackFlags::IsVisible | ETimingTrackFlags::IsDirty)
	{}

	virtual ~FBaseTimingTrack()
	{}

public:
	virtual void Reset()
	{
		Id = 0;
		PosY = 0.0f;
		Height = 0.0f;
		Flags = ETimingTrackFlags::IsVisible | ETimingTrackFlags::IsDirty;
	}

	uint64 GetId() const { return Id; }

	const FName& GetType() const { return Type; }
	const FName& GetSubType() const { return SubType; }

	const FString& GetName() const { return Name; }

	void SetOrder(int32 InOrder) { Order = InOrder; }
	int32 GetOrder() const { return Order; }

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

	bool IsDirty() const { return EnumHasAnyFlags(Flags, ETimingTrackFlags::IsDirty); }
	void SetDirtyFlag() { Flags |= ETimingTrackFlags::IsDirty; OnDirtyFlagChanged(); }
	void ClearDirtyFlag() { Flags &= ~ETimingTrackFlags::IsDirty; OnDirtyFlagChanged(); }
	virtual void OnDirtyFlagChanged() {}

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

	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder);

private:
	uint64 Id;
	FName Type; // Thread, Loading, FileActivity
	FName SubType; // Cpu, Gpu, MainThread, AsyncThread, Overview, Detailed
	FString Name;
	int32 Order;
	float PosY; // y position, in Slate units
	float Height; // height, in Slate units
	ETimingTrackFlags Flags;

	static uint64 IdGenerator;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
