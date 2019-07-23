// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkSettings.h"
#include "LiveLinkSubjectSettings.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkBasicRole.h"

FLiveLinkRoleProjectSetting::FLiveLinkRoleProjectSetting()
	: SettingClass(ULiveLinkSubjectSettings::StaticClass())
	, FrameInterpolationProcessor(ULiveLinkBasicFrameInterpolateProcessor::StaticClass())
{}


ULiveLinkSettings::ULiveLinkSettings()
	: MessageBusPingRequestFrequency(1.0)
	, MessageBusHeartbeatFrequency(1.0)
	, MessageBusHeartbeatTimeout(2.0)
	, TimeWithoutFrameToBeConsiderAsInvalid(0.5)
	, ValidColor(FLinearColor::Green)
	, InvalidColor(FLinearColor::Yellow)
{
	// Add the role animation default settings
	FLiveLinkRoleProjectSetting BaseAnimationSetting;
	BaseAnimationSetting.Role = ULiveLinkAnimationRole::StaticClass();
	BaseAnimationSetting.FrameInterpolationProcessor = ULiveLinkAnimationFrameInterpolateProcessor::StaticClass();
	DefaultRoleSettings.Add(BaseAnimationSetting);
}

FLiveLinkRoleProjectSetting ULiveLinkSettings::GetDefaultSettingForRole(TSubclassOf<ULiveLinkRole> Role) const
{
	int32 IndexOf = DefaultRoleSettings.IndexOfByPredicate([Role](const FLiveLinkRoleProjectSetting& Other) {return Other.Role == Role; });
	if (IndexOf != INDEX_NONE)
	{
		return DefaultRoleSettings[IndexOf];
	}
	FLiveLinkRoleProjectSetting Result;
	Result.Role = Role;
	return Result;
}
