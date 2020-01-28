// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_ACCESSIBILITY

#include "Windows/Accessibility/WindowsUIAManager.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <Ole2.h>
#include "Windows/HideWindowsPlatformTypes.h"
#include <UIAutomation.h>

#include "Misc/Variant.h"
#include "Windows/Accessibility/WindowsUIAControlProvider.h"
#include "Windows/Accessibility/WindowsUIAWidgetProvider.h"
#include "Windows/Accessibility/WindowsUIAPropertyGetters.h"
#include "Windows/WindowsApplication.h"
#include "Windows/WindowsWindow.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"


TMap<EAccessibleWidgetType, ULONG> FWindowsUIAManager::WidgetTypeToWindowsTypeMap;
TMap<EAccessibleWidgetType, FText> FWindowsUIAManager::WidgetTypeToTextMap;

#define LOCTEXT_NAMESPACE "SlateAccessibility"

/**
 * Helper function to raise a UIA property changed event.
 */
void EmitPropertyChangedEvent(FWindowsUIAWidgetProvider* Provider, PROPERTYID Property, const FVariant& OldValue, const FVariant& NewValue)
{
	UE_LOG(LogAccessibility, VeryVerbose, TEXT("UIA Property Changed: %i"), Property);
	UiaRaiseAutomationPropertyChangedEvent(static_cast<IRawElementProviderSimple*>(Provider), Property,
										   WindowsUIAPropertyGetters::FVariantToWindowsVariant(OldValue),
										   WindowsUIAPropertyGetters::FVariantToWindowsVariant(NewValue));
}

FWindowsUIAManager::FWindowsUIAManager(const FWindowsApplication& InApplication)
	: WindowsApplication(InApplication)
	, OnCultureChangedHandle()
	, CachedCurrentLocaleLCID(0)
{
	OnAccessibleMessageHandlerChanged();

#if !UE_BUILD_SHIPPING
	IConsoleManager::Get().RegisterConsoleCommand
	(
		TEXT("Accessibility.DumpStatsWindows"),
		TEXT("Writes to LogAccessibility the memory stats for the platform-level accessibility data (Providers) required for Windows support."),
		FConsoleCommandDelegate::CreateRaw(this, &FWindowsUIAManager::DumpAccessibilityStats),
		ECVF_Default
	);
#endif

	if (WidgetTypeToWindowsTypeMap.Num() == 0)
	{
			WidgetTypeToWindowsTypeMap.Add(EAccessibleWidgetType::Button, UIA_ButtonControlTypeId);
			WidgetTypeToWindowsTypeMap.Add(EAccessibleWidgetType::CheckBox, UIA_CheckBoxControlTypeId);
			WidgetTypeToWindowsTypeMap.Add(EAccessibleWidgetType::ComboBox, UIA_ComboBoxControlTypeId);
			WidgetTypeToWindowsTypeMap.Add(EAccessibleWidgetType::Hyperlink, UIA_HyperlinkControlTypeId);
			WidgetTypeToWindowsTypeMap.Add(EAccessibleWidgetType::Image, UIA_ImageControlTypeId);
			WidgetTypeToWindowsTypeMap.Add(EAccessibleWidgetType::Layout, UIA_PaneControlTypeId);
			WidgetTypeToWindowsTypeMap.Add(EAccessibleWidgetType::ScrollBar, UIA_ScrollBarControlTypeId);
			WidgetTypeToWindowsTypeMap.Add(EAccessibleWidgetType::Slider, UIA_SliderControlTypeId);
			WidgetTypeToWindowsTypeMap.Add(EAccessibleWidgetType::Text, UIA_TextControlTypeId);
			WidgetTypeToWindowsTypeMap.Add(EAccessibleWidgetType::TextEdit, UIA_EditControlTypeId);
			WidgetTypeToWindowsTypeMap.Add(EAccessibleWidgetType::Window, UIA_WindowControlTypeId);

			WidgetTypeToTextMap.Add(EAccessibleWidgetType::Button, LOCTEXT("ControlTypeButton", "button"));
			WidgetTypeToTextMap.Add(EAccessibleWidgetType::CheckBox, LOCTEXT("ControlTypeCheckBox", "check box"));
			WidgetTypeToTextMap.Add(EAccessibleWidgetType::ComboBox, LOCTEXT("ControlTypeComboBox", "combobox"));
			WidgetTypeToTextMap.Add(EAccessibleWidgetType::Hyperlink, LOCTEXT("ControlTypeHyperlink", "hyperlink"));
			WidgetTypeToTextMap.Add(EAccessibleWidgetType::Image, LOCTEXT("ControlTypeImage", "image"));
			WidgetTypeToTextMap.Add(EAccessibleWidgetType::Layout, LOCTEXT("ControlTypeLayout", "pane"));
			WidgetTypeToTextMap.Add(EAccessibleWidgetType::ScrollBar, LOCTEXT("ControlTypeScrollBar", "scroll bar"));
			WidgetTypeToTextMap.Add(EAccessibleWidgetType::Slider, LOCTEXT("ControlTypeSlider", "slider"));
			WidgetTypeToTextMap.Add(EAccessibleWidgetType::Text, LOCTEXT("ControlTypeText", "text"));
			WidgetTypeToTextMap.Add(EAccessibleWidgetType::TextEdit, LOCTEXT("ControlTypeTextEdit", "edit"));
			WidgetTypeToTextMap.Add(EAccessibleWidgetType::Window, LOCTEXT("ControlTypeWindow", "window"));
	}
}

void FWindowsUIAManager::OnAccessibleMessageHandlerChanged()
{
	WindowsApplication.GetAccessibleMessageHandler()->SetAccessibleEventDelegate(FGenericAccessibleMessageHandler::FAccessibleEvent::CreateRaw(this, &FWindowsUIAManager::OnEventRaised));
}

FWindowsUIAManager::~FWindowsUIAManager()
{
	WindowsApplication.GetAccessibleMessageHandler()->SetAccessibleEventDelegate(FGenericAccessibleMessageHandler::FAccessibleEvent());
	for (FWindowsUIABaseProvider* Provider : ProviderList)
	{
		// The UIA Manager may be deleted before the Providers are, and external applications may attempt to call functions on those Providers.
		Provider->OnUIAManagerDestroyed();
	}

	if (OnCultureChangedHandle.IsValid()&& FInternationalization::Get().IsAvailable())
	{
		FInternationalization::Get().OnCultureChanged().Remove(OnCultureChangedHandle);
		OnCultureChangedHandle.Reset();
	}
}

FWindowsUIAWidgetProvider& FWindowsUIAManager::GetWidgetProvider(TSharedRef<IAccessibleWidget> InWidget)
{
	FWindowsUIAWidgetProvider** CachedProvider = CachedWidgetProviders.Find(InWidget);
	if (CachedProvider)
	{
		(*CachedProvider)->AddRef();
		return **CachedProvider;
	}
	else if (InWidget->AsWindow())
	{
		return *CachedWidgetProviders.Add(InWidget, new FWindowsUIAWindowProvider(*this, InWidget));
	}
	else
	{
		return *CachedWidgetProviders.Add(InWidget, new FWindowsUIAWidgetProvider(*this, InWidget));
	}
}

FWindowsUIAWindowProvider& FWindowsUIAManager::GetWindowProvider(TSharedRef<FWindowsWindow> InWindow)
{
	if (CachedWidgetProviders.Num() == 0)
	{
		// The first Get*Provider() request / MUST go through this function since IAccessibleWidgets will not exist untilthe accessible message handler is set active.
		OnAccessibilityEnabled();
	}

	TSharedPtr<IAccessibleWidget> AccessibleWindow = WindowsApplication.GetAccessibleMessageHandler()->GetAccessibleWindow(InWindow);
	checkf(AccessibleWindow.IsValid(), TEXT("%s is not an accessible window. All windows must be accessible."), *InWindow->GetDefinition().Title);
	return static_cast<FWindowsUIAWindowProvider&>(GetWidgetProvider(AccessibleWindow.ToSharedRef()));
}

void FWindowsUIAManager::OnAccessibilityEnabled()
{
	WindowsApplication.GetAccessibleMessageHandler()->SetActive(true);
	// Register for language and locale changes for internationalization.
	// Updates the LCID to be returned in FWindowsUIAWidgetProvider  as a UIA Property 
	UpdateCachedCurrentLocaleLCID();
	OnCultureChangedHandle = FInternationalization::Get().OnCultureChanged().AddRaw(this, &FWindowsUIAManager::UpdateCachedCurrentLocaleLCID);
}

void FWindowsUIAManager::OnWidgetProviderRemoved(TSharedRef<IAccessibleWidget> InWidget)
{
	CachedWidgetProviders.Remove(InWidget);

	if (CachedWidgetProviders.Num() == 0)
	{
		// If the last widget Provider is being removed, we can disable application accessibility.
		// Technically an external application could still be running listening for mouse/keyboard events,
		// but in practice I don't think its realistic to do this while having no references to any Provider.
		//
		// Note that there could still be control Providers with valid references. In practice I don't think
		// this is possible, but if we run into problems then we can simply call AddRef and Release on the
		// underlying widget Provider whenever a control Provider gets added/removed.
		OnAccessibilityDisabled();
	}
}

void FWindowsUIAManager::OnAccessibilityDisabled()
{
	WindowsApplication.GetAccessibleMessageHandler()->SetActive(false);
	CachedCurrentLocaleLCID = 0;
	if (OnCultureChangedHandle.IsValid())
	{
		FInternationalization::Get().OnCultureChanged().Remove(OnCultureChangedHandle);
		OnCultureChangedHandle.Reset();
	}
}

void FWindowsUIAManager::OnEventRaised(TSharedRef<IAccessibleWidget> Widget, EAccessibleEvent Event, FVariant OldValue, FVariant NewValue)
{
	if (UiaClientsAreListening())
	{
		FScopedWidgetProvider ScopedProvider(GetWidgetProvider(Widget));

		switch (Event)
		{
		case EAccessibleEvent::FocusChange:
		{
			// On focus change, emit a generic FocusChanged event as well as a per-Provider PropertyChanged event
			// todo: handle difference between any focus vs keyboard focus
			if (Widget->HasFocus())
			{
				UiaRaiseAutomationEvent(&ScopedProvider.Provider, UIA_AutomationFocusChangedEventId);
			}
			EmitPropertyChangedEvent(&ScopedProvider.Provider, UIA_HasKeyboardFocusPropertyId, OldValue, NewValue);
			break;
		}
		case EAccessibleEvent::Activate:
			if (ScopedProvider.Provider.SupportsInterface(UIA_TogglePatternId))
			{
				EmitPropertyChangedEvent(&ScopedProvider.Provider, UIA_ToggleToggleStatePropertyId, OldValue, NewValue);

			}
			else if (ScopedProvider.Provider.SupportsInterface(UIA_InvokePatternId))
			{
				UiaRaiseAutomationEvent(&ScopedProvider.Provider, UIA_Invoke_InvokedEventId);
			}
			break;
#if WINVER >= 0x0A00
		case EAccessibleEvent::Notification:
		{
			typedef HRESULT(WINAPI* UiaRaiseNotificationEventFunc)(IRawElementProviderSimple*, NotificationKind, NotificationProcessing, BSTR, BSTR);
			static UiaRaiseNotificationEventFunc NotificationFunc = nullptr;
			static bool bDoOnce = true;
			if (bDoOnce)
			{
				HMODULE Handle = GetModuleHandle(TEXT("Uiautomationcore.dll"));
				bDoOnce = false;
				if (Handle)
				{
					// Cast to intermediary void* to avoid compiler warning 4191, since GetProcAddress doesn't know function arguments
					NotificationFunc = (UiaRaiseNotificationEventFunc)(void*)GetProcAddress(Handle, "UiaRaiseNotificationEvent");
				}
			}
			if (NotificationFunc)
			{
				NotificationFunc(&ScopedProvider.Provider, NotificationKind_ActionCompleted, NotificationProcessing_All, SysAllocString(*NewValue.GetValue<FString>()), SysAllocString(TEXT("")));
			}
			break;
		}
#endif
		// IMPORTANT: Calling UiaRaiseStructureChangedEvent seems to raise our per-frame timing for accessibility by
		// over 10x (.04ms to .7ms, tested by clicking on the "All Classes" button in the modes panel of the Editor).
		// For now, I'm disabling this until we figure out if it's absolutely necessary.
		//case EAccessibleEvent::ParentChanged:
		//{
		//	const AccessibleWidgetId OldId = OldValue.GetValue<AccessibleWidgetId>();
		//	const AccessibleWidgetId NewId = NewValue.GetValue<AccessibleWidgetId>();
		//	if (OldId != IAccessibleWidget::InvalidAccessibleWidgetId)
		//	{
		//		GetWidgetProvider(WindowsApplication.GetAccessibleMessageHandler()->GetAccessibleWidgetFromId(OldId).ToSharedRef());
		//		int id[2] = { UiaAppendRuntimeId, Widget->GetId() };
		//		FScopedWidgetProvider ParentProvider(GetWidgetProvider(WindowsApplication.GetAccessibleMessageHandler()->GetAccessibleWidgetFromId(OldId).ToSharedRef()));
		//		UiaRaiseStructureChangedEvent(&ParentProvider.Provider, StructureChangeType_ChildRemoved, id, ARRAYSIZE(id));
		//	}
		//	if (NewId != IAccessibleWidget::InvalidAccessibleWidgetId)
		//	{
		//		FScopedWidgetProvider ParentProvider(GetWidgetProvider(WindowsApplication.GetAccessibleMessageHandler()->GetAccessibleWidgetFromId(NewId).ToSharedRef()));
		//		UiaRaiseStructureChangedEvent(&ParentProvider.Provider, StructureChangeType_ChildAdded, nullptr, 0);
		//	}
		//	break;
		//}
		case EAccessibleEvent::WidgetRemoved:
		{
			typedef HRESULT(WINAPI* UiaDisconnectProviderFunc)(IRawElementProviderSimple*);
			static UiaDisconnectProviderFunc DisconnectFunc = nullptr;
			static bool bDoOnce = true;
			if (bDoOnce)
			{
				bDoOnce = false;
				HMODULE Handle = GetModuleHandle(TEXT("Uiautomationcore.dll"));
				if (Handle)
				{
					// Cast to intermediary void* to avoid compiler warning 4191, since GetProcAddress doesn't know function arguments
					DisconnectFunc = (UiaDisconnectProviderFunc)(void*)GetProcAddress(Handle, "UiaDisconnectProvider");
				}
			}
			if (DisconnectFunc)
			{
				DisconnectFunc(&ScopedProvider.Provider);
			}
			break;
		}
		}
	}
}

void FWindowsUIAManager::UpdateCachedCurrentLocaleLCID()
{
	CachedCurrentLocaleLCID = FInternationalization::Get().GetCurrentLocale()->GetLCID();
	// an LCID of 0 is invalid  and and UIA won't do anything with it.
	// Get the default OS locale LCID to give to screen readers  in that case
	if (CachedCurrentLocaleLCID == 0)
	{
		CachedCurrentLocaleLCID = FInternationalization::Get().GetDefaultLocale()->GetLCID();
	}
}

void FWindowsUIAManager::RunInGameThreadBlocking(CONST TFunction<void()>& Function) const
{
		WindowsApplication.GetAccessibleMessageHandler()->RunInThread(Function, true, ENamedThreads::GameThread);
}

#if !UE_BUILD_SHIPPING
void FWindowsUIAManager::DumpAccessibilityStats() const
{
	const uint32 NumWidgetProviders = CachedWidgetProviders.Num();
	// This isn't exactly right since some ControlProviders will be TextRangeProviders, but it should be close
	const uint32 NumControlProviders = ProviderList.Num() - NumWidgetProviders;

	const uint32 SizeOfWidgetProvider = sizeof(FWindowsUIAWidgetProvider);
	const uint32 SizeOfControlProvider = sizeof(FWindowsUIAControlProvider);
	const uint32 SizeOfCachedWidgetProviders = CachedWidgetProviders.GetAllocatedSize();
	const uint32 SizeOfProviderList = ProviderList.GetAllocatedSize();
	const uint32 CacheSize = NumWidgetProviders * SizeOfWidgetProvider + NumControlProviders * SizeOfControlProvider + SizeOfCachedWidgetProviders + SizeOfProviderList;

	UE_LOG(LogAccessibility, Log, TEXT("Dumping Windows accessibility stats:"));
	UE_LOG(LogAccessibility, Log, TEXT("Number of Widget Providers: %i"), NumWidgetProviders);
	UE_LOG(LogAccessibility, Log, TEXT("Number of non-Widget Providers: %i"), NumControlProviders);
	UE_LOG(LogAccessibility, Log, TEXT("Size of FWindowsUIAWidgetProvider: %u"), SizeOfWidgetProvider);
	UE_LOG(LogAccessibility, Log, TEXT("Size of FWindowsUIAControlProvider: %u"), SizeOfControlProvider);
	UE_LOG(LogAccessibility, Log, TEXT("Size of WidgetProvider* map: %u"), SizeOfCachedWidgetProviders);
	UE_LOG(LogAccessibility, Log, TEXT("Size of all Provider* set: %u"), SizeOfProviderList);
	UE_LOG(LogAccessibility, Log, TEXT("Memory stored in cache: %u kb"), CacheSize / 1000);
}
#endif

#undef LOCTEXT_NAMESPACE

#endif
