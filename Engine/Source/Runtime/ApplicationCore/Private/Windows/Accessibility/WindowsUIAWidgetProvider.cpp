// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_ACCESSIBILITY

#include "Windows/Accessibility/WindowsUIAWidgetProvider.h"
#include "GenericPlatform/Accessibility/GenericAccessibleInterfaces.h"
#include "Windows/Accessibility/WindowsUIAControlProvider.h"
#include "Windows/Accessibility/WindowsUIAManager.h"
#include "Windows/Accessibility/WindowsUIAPropertyGetters.h"
#include "Windows/WindowsWindow.h"

DECLARE_CYCLE_STAT(TEXT("Windows Accessibility: Navigate"), STAT_AccessibilityWindowsNavigate, STATGROUP_Accessibility);
DECLARE_CYCLE_STAT(TEXT("Windows Accessibility: GetProperty"), STAT_AccessibilityWindowsGetProperty, STATGROUP_Accessibility);

#define LOCTEXT_NAMESPACE "SlateAccessibility"

/** Convert our accessible widget type to a Windows control ID */
ULONG WidgetTypeToControlType(TSharedRef<IAccessibleWidget> Widget)
{
	ULONG* Type = FWindowsUIAManager::WidgetTypeToWindowsTypeMap.Find(Widget->GetWidgetType());
	if (Type)
	{
		return *Type;
	}
	else
	{
		return UIA_CustomControlTypeId;
	}
}

/**
 * Convert our accessible widget type to a human-readable localized string
 * See https://docs.microsoft.com/en-us/windows/desktop/winauto/uiauto-automation-element-propids for rules.
 */
FString WidgetTypeToLocalizedString(TSharedRef<IAccessibleWidget> Widget)
{
	FText* Text = FWindowsUIAManager::WidgetTypeToTextMap.Find(Widget->GetWidgetType());
	if (Text)
	{
		return Text->ToString();
	}
	else
	{
		static FText CustomControlTypeName = LOCTEXT("ControlTypeCustom", "custom");
		return CustomControlTypeName.ToString();
	}
}

// FWindowsUIAWidgetProvider methods

FWindowsUIAWidgetProvider::FWindowsUIAWidgetProvider(FWindowsUIAManager& InManager, TSharedRef<IAccessibleWidget> InWidget)
	: FWindowsUIABaseProvider(InManager, InWidget)
{
	//UpdateCachedProperties();
}

FWindowsUIAWidgetProvider::~FWindowsUIAWidgetProvider()
{
	if (UIAManager)
	{
		UIAManager->OnWidgetProviderRemoved(Widget);
	}
}

//void FWindowsUIAWidgetProvider::UpdateCachedProperty(PROPERTYID PropertyId)
//{
//	FVariant& CachedValue = CachedPropertyValues.FindOrAdd(PropertyId);
//	FVariant CurrentValue = WindowsUIAPropertyGetters::GetPropertyValue(Widget, PropertyId);
//	if (CachedValue.IsEmpty())
//	{
//		CachedValue = CurrentValue;
//	}
//	else if (CachedValue != CurrentValue)
//	{
//		UE_LOG(LogAccessibility, VeryVerbose, TEXT("UIA Property Changed: %i"), PropertyId);
//		UiaRaiseAutomationPropertyChangedEvent(
//			static_cast<IRawElementProviderSimple*>(this), PropertyId,
//			WindowsUIAPropertyGetters::FVariantToWindowsVariant(CachedValue),
//			WindowsUIAPropertyGetters::FVariantToWindowsVariant(CurrentValue));
//
//		CachedValue = CurrentValue;
//	}
//}
//
//void FWindowsUIAWidgetProvider::UpdateCachedProperties()
//{
//	if (IsValid())
//	{
//		if (SupportsInterface(UIA_RangeValuePatternId))
//		{
//			UpdateCachedProperty(UIA_RangeValueIsReadOnlyPropertyId);
//			UpdateCachedProperty(UIA_RangeValueValuePropertyId);
//			UpdateCachedProperty(UIA_RangeValueIsReadOnlyPropertyId);
//			UpdateCachedProperty(UIA_RangeValueMinimumPropertyId);
//			UpdateCachedProperty(UIA_RangeValueMaximumPropertyId);
//			UpdateCachedProperty(UIA_RangeValueLargeChangePropertyId);
//			UpdateCachedProperty(UIA_RangeValueSmallChangePropertyId);
//		}
//
//		if (SupportsInterface(UIA_TogglePatternId))
//		{
//			UpdateCachedProperty(UIA_ToggleToggleStatePropertyId);
//		}
//
//		if (SupportsInterface(UIA_ValuePatternId))
//		{
//			UpdateCachedProperty(UIA_ValueIsReadOnlyPropertyId);
//			UpdateCachedProperty(UIA_ValueValuePropertyId);
//		}
//
//		if (SupportsInterface(UIA_WindowPatternId))
//		{
//			UpdateCachedProperty(UIA_WindowCanMaximizePropertyId);
//			UpdateCachedProperty(UIA_WindowCanMinimizePropertyId);
//			UpdateCachedProperty(UIA_WindowIsModalPropertyId);
//			UpdateCachedProperty(UIA_WindowIsTopmostPropertyId);
//			UpdateCachedProperty(UIA_WindowWindowInteractionStatePropertyId);
//			UpdateCachedProperty(UIA_WindowWindowVisualStatePropertyId);
//			UpdateCachedProperty(UIA_TransformCanMovePropertyId);
//			UpdateCachedProperty(UIA_TransformCanRotatePropertyId);
//			UpdateCachedProperty(UIA_TransformCanResizePropertyId);
//		}
//	}
//}

HRESULT STDCALL FWindowsUIAWidgetProvider::QueryInterface(REFIID riid, void** ppInterface)
{
	if (riid == __uuidof(IUnknown))
	{
		*ppInterface = static_cast<IRawElementProviderSimple*>(this);
	}
	else if (riid == __uuidof(IRawElementProviderSimple))
	{
		*ppInterface = static_cast<IRawElementProviderSimple*>(this);
	}
	else if (riid == __uuidof(IRawElementProviderFragment))
	{
		*ppInterface = static_cast<IRawElementProviderFragment*>(this);
	}
	else
	{
		*ppInterface = nullptr;
	}

	if (*ppInterface)
	{
		// QueryInterface is the one exception where we need to call AddRef without going through GetWidgetProvider().
		AddRef();
		return S_OK;
	}
	else
	{
		return E_NOINTERFACE;
	}
}

ULONG STDCALL FWindowsUIAWidgetProvider::AddRef()
{
	return FWindowsUIABaseProvider::IncrementRef();
}

ULONG STDCALL FWindowsUIAWidgetProvider::Release()
{
	return FWindowsUIABaseProvider::DecrementRef();
}

bool FWindowsUIAWidgetProvider::SupportsInterface(PATTERNID PatternId) const
{
	switch (PatternId)
	{
	case UIA_InvokePatternId:
	{
		IAccessibleActivatable* Activatable = Widget->AsActivatable();
		// Toggle and Invoke are mutually exclusive
		return Activatable && !Activatable->IsCheckable();
	}
	case UIA_RangeValuePatternId:
	{
		IAccessibleProperty* Property = Widget->AsProperty();
		// Value and RangeValue are mutually exclusive
		return Property && Property->GetStepSize() > 0.0f;
	}
	case UIA_TextPatternId:
	{
		return Widget->AsText() != nullptr;
	}
	case UIA_TogglePatternId:
	{
		IAccessibleActivatable* Activatable = Widget->AsActivatable();
		return Activatable && Activatable->IsCheckable();
	}
	case UIA_ValuePatternId:
	{
		IAccessibleProperty* Property = Widget->AsProperty();
		return Property && FMath::IsNearlyZero(Property->GetStepSize());
	}
	}
	return false;
}

HRESULT STDCALL FWindowsUIAWidgetProvider::get_ProviderOptions(ProviderOptions* pRetVal)
{
	// ServerSideProvider means that we are creating the definition of the accessible widgets for Clients (eg screenreaders) to consume.
	// UseComThreading is necessary to ensure that COM messages are properly routed to the main thread.
	*pRetVal = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
	return S_OK;
}

HRESULT STDCALL FWindowsUIAWidgetProvider::GetPatternProvider(PATTERNID patternId, IUnknown** pRetVal)
{
	if (IsValid())
	{
		if (SupportsInterface(patternId))
		{
			// FWindowsUIAControlProvider implements all possible control providers that we support.
			FWindowsUIAControlProvider* ControlProvider = new FWindowsUIAControlProvider(*UIAManager, Widget);
			switch (patternId)
			{
			case UIA_InvokePatternId:
				*pRetVal = static_cast<IInvokeProvider*>(ControlProvider);
				break;
			case UIA_RangeValuePatternId:
				*pRetVal = static_cast<IRangeValueProvider*>(ControlProvider);
				break;
			case UIA_TextPatternId:
				*pRetVal = static_cast<ITextProvider*>(ControlProvider);
				break;
			case UIA_TogglePatternId:
				*pRetVal = static_cast<IToggleProvider*>(ControlProvider);
				break;
			case UIA_ValuePatternId:
				*pRetVal = static_cast<IValueProvider*>(ControlProvider);
				break;
			default:
				UE_LOG(LogAccessibility, Error, TEXT("FWindowsUIAWidgetProvider::SupportsInterface() returned true, but was unhandled in GetPatternProvider(). PatternId = %i"), patternId);
				*pRetVal = nullptr;
				ControlProvider->Release();
				break;
			}
		}
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAWidgetProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* pRetVal)
{
	SCOPE_CYCLE_COUNTER(STAT_AccessibilityWindowsGetProperty);

	if (!IsValid())
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}

	bool bValid = true;

	// https://docs.microsoft.com/en-us/windows/desktop/winauto/uiauto-automation-element-propids
	// potential other properties:
	// - UIA_CenterPointPropertyId
	// - UIA_ControllerForPropertyId (eg. to make clicking on a label also click a checkbox)
	// - UIA_DescribedByPropertyId (same as LabeledBy? not sure)
	// - UIA_IsDialogPropertyId (doesn't seem to exist in our version of UIA)
	// - UIA_ItemStatusPropertyId (eg. busy, loading)
	// - UIA_LabeledByPropertyId (eg. to describe a checkbox from a separate label)
	// - UIA_NativeWindowHandlePropertyId (unclear if this is only for windows or all widgets)
	// - UIA_PositionInSetPropertyId (position in list of items, could be useful)
	// - UIA_SizeOfSetPropertyId (number of items in list, could be useful)
	switch (propertyId)
	{
	//case UIA_AcceleratorKeyPropertyId:
	//	pRetVal->vt = VT_BSTR;
	//	// todo: hotkey, used with IInvokeProvider
	//	pRetVal->bstrVal = SysAllocString(L"");
	//	break;
	//case UIA_AccessKeyPropertyId:
	//	pRetVal->vt = VT_BSTR;
	//	// todo: activates menu, like F in &File
	//	pRetVal->bstrVal = SysAllocString(L"");
	//	break;
	//case UIA_AutomationIdPropertyId:
	//	pRetVal->vt = VT_BSTR;
	//	// todo: an identifiable name for the widget for automation testing, used like App.Find("MainMenuBar")
	//	pRetVal->bstrVal = SysAllocString(L"");
	//	break;
	case UIA_BoundingRectanglePropertyId:
		pRetVal->vt = VT_R8 | VT_ARRAY;
		pRetVal->parray = SafeArrayCreateVector(VT_R8, 0, 4);
		if (pRetVal->parray)
		{
			FBox2D Box = Widget->GetBounds();
			LONG i = 0;
			bValid &= (SafeArrayPutElement(pRetVal->parray, &i, &Box.Min.X) != S_OK);
			i = 1;
			bValid &= (SafeArrayPutElement(pRetVal->parray, &i, &Box.Max.X) != S_OK);
			i = 2;
			bValid &= (SafeArrayPutElement(pRetVal->parray, &i, &Box.Min.Y) != S_OK);
			i = 3;
			bValid &= (SafeArrayPutElement(pRetVal->parray, &i, &Box.Max.Y) != S_OK);
		}
		else
		{
			bValid = false;
		}
		break;
	case UIA_ClassNamePropertyId:
		pRetVal->vt = VT_BSTR;
		pRetVal->bstrVal = SysAllocString(*Widget->GetClassName());
		break;
	case UIA_ControlTypePropertyId:
		pRetVal->vt = VT_I4;
		pRetVal->lVal = WidgetTypeToControlType(Widget);
		break;
	case UIA_CulturePropertyId:
		pRetVal->vt = VT_I4;
		pRetVal->lVal = UIAManager->GetCachedCurrentLocaleLCID();
		break;
	case UIA_FrameworkIdPropertyId:
		pRetVal->vt = VT_BSTR;
		// todo: figure out what goes here
		pRetVal->bstrVal = SysAllocString(*LOCTEXT("Slate", "Slate").ToString());
		break;
	case UIA_HasKeyboardFocusPropertyId:
		pRetVal->vt = VT_BOOL;
		pRetVal->boolVal = Widget->HasFocus() ? VARIANT_TRUE : VARIANT_FALSE;
		break;
	case UIA_HelpTextPropertyId:
		pRetVal->vt = VT_BSTR;
		pRetVal->bstrVal = SysAllocString(*Widget->GetHelpText());
		break;
	case UIA_IsContentElementPropertyId:
		pRetVal->vt = VT_BOOL;
		// todo: https://docs.microsoft.com/en-us/windows/desktop/winauto/uiauto-treeoverview
		pRetVal->boolVal = VARIANT_TRUE;
		break;
	case UIA_IsControlElementPropertyId:
		pRetVal->vt = VT_BOOL;
		// todo: https://docs.microsoft.com/en-us/windows/desktop/winauto/uiauto-treeoverview
		pRetVal->boolVal = VARIANT_TRUE;
		break;
	case UIA_IsEnabledPropertyId:
		pRetVal->vt = VT_BOOL;
		pRetVal->boolVal = Widget->IsEnabled() ? VARIANT_TRUE : VARIANT_FALSE;
		break;
	case UIA_IsKeyboardFocusablePropertyId:
		pRetVal->vt = VT_BOOL;
		pRetVal->boolVal = Widget->SupportsFocus() ? VARIANT_TRUE : VARIANT_FALSE;
		break;
	case UIA_IsOffscreenPropertyId:
		pRetVal->vt = VT_BOOL;
		pRetVal->boolVal = Widget->IsHidden() ? VARIANT_TRUE : VARIANT_FALSE;
		break;
	case UIA_IsPasswordPropertyId:
		if (Widget->AsProperty())
		{
			pRetVal->vt = VT_BOOL;
			pRetVal->boolVal = Widget->AsProperty()->IsPassword() ? VARIANT_TRUE : VARIANT_FALSE;
		}
		break;
//#if WINVER >= 0x0603 // Windows 8.1
//	case UIA_IsPeripheralPropertyId:
//		pRetVal->vt = VT_BOOL;
//		// todo: see https://docs.microsoft.com/en-us/windows/desktop/winauto/uiauto-automation-element-propids for list of control types
//		pRetVal->boolVal = VARIANT_FALSE;
//		break;
//#endif
//	case UIA_ItemTypePropertyId:
//		pRetVal->vt = VT_BSTR;
//		// todo: friendly name of what's in a listview
//		pRetVal->bstrVal = SysAllocString(L"");
//		break;
//#if WINVER >= 0x0602 // Windows 8
//	case UIA_LiveSettingPropertyId:
//		pRetVal->vt = VT_I4;
//		// todo: "politeness" setting
//		pRetVal->lVal = 0;
//		break;
//#endif
	case UIA_LocalizedControlTypePropertyId:
		pRetVal->vt = VT_BSTR;
		pRetVal->bstrVal = SysAllocString(*WidgetTypeToLocalizedString(Widget));
		break;
	case UIA_NamePropertyId:
		pRetVal->vt = VT_BSTR;
		// todo: slate widgets don't have names, screen reader may read this as accessible text
		pRetVal->bstrVal = SysAllocString(*Widget->GetWidgetName());
		break;
	//case UIA_OrientationPropertyId:
	//	pRetVal->vt = VT_I4;
	//	// todo: sliders, scroll bars, layouts
	//	pRetVal->lVal = OrientationType_None;
	//	break;
	case UIA_ProcessIdPropertyId:
		pRetVal->vt = VT_I4;
		pRetVal->lVal = ::GetCurrentProcessId();
		break;
	default:
		pRetVal->vt = VT_EMPTY;
		break;
	}

	if (!bValid)
	{
		pRetVal->vt = VT_EMPTY;
		return E_FAIL;
	}

	return S_OK;
}

HRESULT STDCALL FWindowsUIAWidgetProvider::get_HostRawElementProvider(IRawElementProviderSimple** pRetVal)
{
	// Only return host provider for native windows
	*pRetVal = nullptr;
	return S_OK;
}

HRESULT STDCALL FWindowsUIAWidgetProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** pRetVal)
{
	SCOPE_CYCLE_COUNTER(STAT_AccessibilityWindowsNavigate);

	if (!IsValid())
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}

	TSharedPtr<IAccessibleWidget> Relative = nullptr;
	switch (direction)
	{
	case NavigateDirection_Parent:
		Relative = Widget->GetParent();
		break;
	case NavigateDirection_NextSibling:
		Relative = Widget->GetNextSibling();
		break;
	case NavigateDirection_PreviousSibling:
		Relative = Widget->GetPreviousSibling();
		break;
	case NavigateDirection_FirstChild:
		if (Widget->GetNumberOfChildren() > 0)
		{
			Relative = Widget->GetChildAt(0);
		}
		break;
	case NavigateDirection_LastChild:
	{
		const int32 NumChildren = Widget->GetNumberOfChildren();
		if (NumChildren > 0)
		{
			Relative = Widget->GetChildAt(NumChildren - 1);
		}
		break;
	}
	}

	if (Relative.IsValid())
	{
		*pRetVal = static_cast<IRawElementProviderFragment*>(&UIAManager->GetWidgetProvider(Relative.ToSharedRef()));
	}
	else
	{
		*pRetVal = nullptr;
	}
	return S_OK;
}

HRESULT STDCALL FWindowsUIAWidgetProvider::GetRuntimeId(SAFEARRAY** pRetVal)
{
	if (IsValid())
	{
		int rtId[] = { UiaAppendRuntimeId, Widget->GetId() };
		*pRetVal = SafeArrayCreateVector(VT_I4, 0, 2);
		if (*pRetVal)
		{
			for (LONG i = 0; i < 2; ++i)
			{
				if (SafeArrayPutElement(*pRetVal, &i, &rtId[i]) != S_OK)
				{
					return E_FAIL;
				}
			}
		};
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAWidgetProvider::get_BoundingRectangle(UiaRect* pRetVal)
{
	if (IsValid())
	{
		FBox2D Box = Widget->GetBounds();
		pRetVal->left = Box.Min.X;
		pRetVal->top = Box.Min.Y;
		pRetVal->width = Box.Max.X - Box.Min.X;
		pRetVal->height = Box.Max.Y - Box.Min.Y;
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}
HRESULT STDCALL FWindowsUIAWidgetProvider::GetEmbeddedFragmentRoots(SAFEARRAY** pRetVal)
{
	// This would technically only be valid in our case for a window within a window
	*pRetVal = nullptr;
	return S_OK;
}

HRESULT STDCALL FWindowsUIAWidgetProvider::SetFocus()
{
	if (IsValid())
	{
		if (Widget->SupportsFocus())
		{
			Widget->SetFocus();
			return S_OK;
		}
		else
		{
			return UIA_E_NOTSUPPORTED;
		}
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAWidgetProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** pRetVal)
{
	if (IsValid())
	{
		TSharedPtr<IAccessibleWidget> Window = Widget->GetWindow();
		if (Window.IsValid())
		{
			*pRetVal = static_cast<IRawElementProviderFragmentRoot*>(&static_cast<FWindowsUIAWindowProvider&>(UIAManager->GetWidgetProvider(Window.ToSharedRef())));
			return S_OK;
		}
	}

	return UIA_E_ELEMENTNOTAVAILABLE;
}

// ~

// FWindowsUIAWindowProvider methods

FWindowsUIAWindowProvider::FWindowsUIAWindowProvider(FWindowsUIAManager& InManager, TSharedRef<IAccessibleWidget> InWidget)
	: FWindowsUIAWidgetProvider(InManager, InWidget)
{
	ensure(InWidget->AsWindow() != nullptr);
}

FWindowsUIAWindowProvider::~FWindowsUIAWindowProvider()
{
}

HRESULT STDCALL FWindowsUIAWindowProvider::QueryInterface(REFIID riid, void** ppInterface)
{
	if (riid == __uuidof(IRawElementProviderFragmentRoot))
	{
		*ppInterface = static_cast<IRawElementProviderFragmentRoot*>(this);
		AddRef();
		return S_OK;
	}
	else
	{
		return FWindowsUIAWidgetProvider::QueryInterface(riid, ppInterface);
	}
}

ULONG STDCALL FWindowsUIAWindowProvider::AddRef()
{
	return FWindowsUIABaseProvider::IncrementRef();
}

ULONG STDCALL FWindowsUIAWindowProvider::Release()
{
	return FWindowsUIABaseProvider::DecrementRef();
}

HRESULT STDCALL FWindowsUIAWindowProvider::get_HostRawElementProvider(IRawElementProviderSimple** pRetVal)
{
	if (Widget->IsValid())
	{
		TSharedPtr<FGenericWindow> NativeWindow = Widget->AsWindow()->GetNativeWindow();
		if (NativeWindow.IsValid())
		{
			HWND Hwnd = static_cast<HWND>(NativeWindow->GetOSWindowHandle());
			if (Hwnd != nullptr)
			{
				return UiaHostProviderFromHwnd(Hwnd, pRetVal);
			}
		}
		return UIA_E_INVALIDOPERATION;
	}
	
	return UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT STDCALL FWindowsUIAWindowProvider::GetPatternProvider(PATTERNID patternId, IUnknown** pRetVal)
{
	if (IsValid())
	{
		switch (patternId)
		{
		case UIA_WindowPatternId:
			*pRetVal = static_cast<IWindowProvider*>(new FWindowsUIAControlProvider(*UIAManager, Widget));
			return S_OK;
		default:
			return FWindowsUIAWidgetProvider::GetPatternProvider(patternId, pRetVal);
		}
	}

	return UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT STDCALL FWindowsUIAWindowProvider::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** pRetVal)
{
	if (IsValid())
	{
		TSharedPtr<IAccessibleWidget> Child = Widget->AsWindow()->GetChildAtPosition(x, y);
		if (Child.IsValid())
		{
			*pRetVal = static_cast<IRawElementProviderFragment*>(&UIAManager->GetWidgetProvider(Child.ToSharedRef()));
		}
		else
		{
			*pRetVal = nullptr;
		}
		return S_OK;
	}

	return UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT STDCALL FWindowsUIAWindowProvider::GetFocus(IRawElementProviderFragment** pRetVal)
{
	*pRetVal = nullptr;
	if (IsValid())
	{
		TSharedPtr<IAccessibleWidget> Focus = Widget->AsWindow()->GetFocusedWidget();
		if (Focus.IsValid())
		{
			*pRetVal = static_cast<IRawElementProviderFragment*>(&UIAManager->GetWidgetProvider(Focus.ToSharedRef()));
		}
		return S_OK;
	}

	return UIA_E_ELEMENTNOTAVAILABLE;
}

// ~

#undef LOCTEXT_NAMESPACE

#endif
