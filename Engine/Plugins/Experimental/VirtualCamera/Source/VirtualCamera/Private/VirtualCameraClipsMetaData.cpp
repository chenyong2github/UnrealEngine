// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualCameraClipsMetaData.h"

UVirtualCameraClipsMetaData::UVirtualCameraClipsMetaData(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	FocalLength = 0;
	bIsSelected = false;
}


float UVirtualCameraClipsMetaData::GetFocalLength() const 
{
	return FocalLength; 
}

bool UVirtualCameraClipsMetaData::GetSelected() const
{
	return bIsSelected; 
}

FString UVirtualCameraClipsMetaData::GetRecordedLevelName() const
{
	return RecordedLevelName; 
}

int UVirtualCameraClipsMetaData::GetFrameCountStart() const
{
	return FrameCountStart; 
}

int UVirtualCameraClipsMetaData::GetFrameCountEnd() const
{
	return FrameCountEnd; 
}

void UVirtualCameraClipsMetaData::SetFocalLength(float InFocalLength) 
{
	FocalLength = InFocalLength;
}

void UVirtualCameraClipsMetaData::SetSelected(bool InSelected)
{
	bIsSelected = InSelected;
}

void UVirtualCameraClipsMetaData::SetRecordedLevelName(FString InLevelName)
{
	RecordedLevelName = InLevelName;
}

void UVirtualCameraClipsMetaData::SetFrameCountStart(int InFrame)
{
	FrameCountStart = InFrame; 
}

void UVirtualCameraClipsMetaData::SetFrameCountEnd(int InFrame)
{
	FrameCountEnd = InFrame; 
}

