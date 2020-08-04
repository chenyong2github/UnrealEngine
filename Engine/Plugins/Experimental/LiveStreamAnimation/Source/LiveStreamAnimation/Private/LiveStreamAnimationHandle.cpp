// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveStreamAnimationHandle.h"
#include "LiveStreamAnimationLog.h"
#include "LiveStreamAnimationSettings.h"

#include "Serialization/Archive.h"

LIVESTREAMANIMATION_API class FArchive& operator<<(class FArchive& InAr, FLiveStreamAnimationHandle& SubjectHandle)
{
	if (InAr.IsSaving() && !SubjectHandle.IsValid())
	{
		UE_LOG(LogLiveStreamAnimation, Warning, TEXT("Failed to serialize FLiveStreamAnimationHandle. (Invalid handle while saving)."));
		InAr.SetError();
	}

	uint32 Handle = static_cast<uint32>(SubjectHandle.Handle);
	InAr.SerializeIntPacked(Handle);
	SubjectHandle.Handle = static_cast<int32>(Handle);

	if (InAr.IsLoading() && !SubjectHandle.IsValid())
	{
		UE_LOG(LogLiveStreamAnimation, Warning, TEXT("Failed to serialize FLiveStreamAnimationHandle. (Invalid handle while loading)."));
		InAr.SetError();
	}

	return InAr;
}

FName FLiveStreamAnimationHandle::GetName() const
{
	const TArrayView<const FName> HandleNames = ULiveStreamAnimationSettings::GetHandleNames();
	return HandleNames.IsValidIndex(Handle) ? HandleNames[Handle] : NAME_None;
}

int32 FLiveStreamAnimationHandle::ValidateHandle(FName InName)
{
	return ULiveStreamAnimationSettings::GetHandleNames().Find(InName);
}

int32 FLiveStreamAnimationHandle::ValidateHandle(int32 InHandle)
{
	const TArrayView<const FName> HandleNames = ULiveStreamAnimationSettings::GetHandleNames();
	return HandleNames.IsValidIndex(InHandle) ? InHandle : INDEX_NONE;
}