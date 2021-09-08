// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlateScreenReader.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/Accessibility/GenericAccessibleInterfaces.h"
#include "Announcement/ScreenReaderAnnouncementChannel.h"
#include "GenericPlatform/ScreenReaderUser.h"
#include "InputCoreTypes.h"
#include "SlateScreenReaderLog.h"

// Slate includes
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "Widgets/Accessibility/SlateAccessibleWidgetCache.h"

#define LOCTEXT_NAMESPACE "SlateScreenReader"

FSlateScreenReader::FSlateScreenReader(const TSharedRef<GenericApplication>& InPlatformApplication)
	: FScreenReaderBase(InPlatformApplication)
{

}

FSlateScreenReader::~FSlateScreenReader()
{

}

void FSlateScreenReader::HandleSlateFocusChanging(const FFocusEvent& InFocusEvent, const FWeakWidgetPath& InOldFocusWidgetPath, const TSharedPtr<SWidget>& InOldWidget, const FWidgetPath& InNewWidgetPath, const TSharedPtr<SWidget>& InNewFocusWidget)
{
	if (IsActive() && InNewFocusWidget && InNewFocusWidget->IsAccessible())
	{
		int32 UserId = static_cast<int32>(InFocusEvent.GetUser());
		if (TSharedPtr<FScreenReaderUser> User = GetUser(UserId))
		{
			TSharedPtr<IAccessibleWidget> AccessibleWidget = FSlateAccessibleWidgetCache::GetAccessibleWidgetChecked(InNewFocusWidget);
			if (AccessibleWidget)
			{
				User->SetAccessibleFocusWidget(AccessibleWidget.ToSharedRef());
				User->RequestSpeakWidget(AccessibleWidget.ToSharedRef());
			}
		}
	}
}

void FSlateScreenReader::OnActivate()
{
	UE_LOG(LogSlateScreenReader, Verbose, TEXT("On activation of Slate screen reader."));
	// @TODOAccessibility: Consider removing the binding on deactivation 
	FSlateApplication::Get().OnFocusChanging().AddRaw(this, &FSlateScreenReader::HandleSlateFocusChanging);
}

void FSlateScreenReader::OnDeactivate()
{
	UE_LOG(LogSlateScreenReader, Verbose, TEXT("Deactivation of Slate screen reader."));
} 
#undef LOCTEXT_NAMESPACE
