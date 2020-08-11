// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkXRSourceFactory.h"
#include "LiveLinkXRSource.h"
#include "SLiveLinkXRSourceFactory.h"
#include "LiveLinkXRSourceSettings.h"

#include "ILiveLinkClient.h"
#include "Features/IModularFeatures.h"

#define LOCTEXT_NAMESPACE "LiveLinkXRSourceFactory"

FText ULiveLinkXRSourceFactory::GetSourceDisplayName() const
{
	return LOCTEXT("SourceDisplayName", "LiveLinkXR Source");	
}

FText ULiveLinkXRSourceFactory::GetSourceTooltip() const
{
	return LOCTEXT("SourceTooltip", "Allows creation of multiple LiveLink sources using the XR tracking system");
}

TSharedPtr<SWidget> ULiveLinkXRSourceFactory::BuildCreationPanel(FOnLiveLinkSourceCreated InOnLiveLinkSourceCreated) const
{
	return SNew(SLiveLinkXRSourceFactory)
		.OnSourceSettingAccepted(FOnLiveLinkXRSourceSettingAccepted::CreateUObject(this, &ULiveLinkXRSourceFactory::CreateSourceFromSetting, InOnLiveLinkSourceCreated));
}

TSharedPtr<ILiveLinkSource> ULiveLinkXRSourceFactory::CreateSource(const FString& ConnectionString) const
{
	FLiveLinkXRSettings Setting;
	if (!ConnectionString.IsEmpty())
	{
		FLiveLinkXRSettings::StaticStruct()->ImportText(*ConnectionString, &Setting, nullptr, PPF_None, GLog, TEXT("ULiveLinkXRSourceFactory"));
	}
	return MakeShared<FLiveLinkXRSource>(Setting);
}

void ULiveLinkXRSourceFactory::CreateSourceFromSetting(FLiveLinkXRSettings Setting, FOnLiveLinkSourceCreated OnSourceCreated) const
{
	FString ConnectionString;
	FLiveLinkXRSettings::StaticStruct()->ExportText(ConnectionString, &GetDefault<ULiveLinkXRSettingsObject>()->Settings, nullptr, nullptr, PPF_None, nullptr);
	GetMutableDefault<ULiveLinkXRSettingsObject>()->SaveConfig();

	TSharedPtr<FLiveLinkXRSource> SharedPtr = MakeShared<FLiveLinkXRSource>(Setting);
	OnSourceCreated.ExecuteIfBound(SharedPtr, MoveTemp(ConnectionString));
}

#undef LOCTEXT_NAMESPACE
