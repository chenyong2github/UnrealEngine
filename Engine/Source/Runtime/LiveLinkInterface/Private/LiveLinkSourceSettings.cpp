// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkSourceSettings.h"
#include "UObject/EnterpriseObjectVersion.h"

void ULiveLinkSourceSettings::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FEnterpriseObjectVersion::GUID);
}

#if WITH_EDITOR
bool ULiveLinkSourceSettings::CanEditChange(const FProperty* InProperty) const
{
	if (Super::CanEditChange(InProperty))
	{
		if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, TimecodeFrameOffset)
			|| InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, TimecodeFrameRate)
			|| InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, ValidTimecodeFrame))
		{
			return Mode == ELiveLinkSourceMode::Timecode;
		}

		if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, ValidEngineTime)
			|| InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, EngineTimeOffset))
		{
			return Mode == ELiveLinkSourceMode::EngineTime;
		}

		return true;
	}
	return false;
}
#endif //WITH_EDITOR
