// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_ACCESSIBILITY

#include "Windows/Accessibility/WindowsUIAControlProvider.h"
#include "Windows/Accessibility/WindowsUIAWidgetProvider.h"
#include "Windows/Accessibility/WindowsUIAPropertyGetters.h"
#include "Windows/Accessibility/WindowsUIAManager.h"
#include "GenericPlatform/GenericAccessibleInterfaces.h"

// FWindowsUIATextRangeProvider

FWindowsUIATextRangeProvider::FWindowsUIATextRangeProvider(FWindowsUIAManager& InManager, TSharedRef<IAccessibleWidget> InWidget, FTextRange InRange)
	: FWindowsUIABaseProvider(InManager, InWidget)
	, TextRange(InRange)
{
}

FWindowsUIATextRangeProvider::~FWindowsUIATextRangeProvider()
{
}

HRESULT STDCALL FWindowsUIATextRangeProvider::QueryInterface(REFIID riid, void** ppInterface)
{
	*ppInterface = nullptr;

	if (riid == __uuidof(IUnknown))
	{
		*ppInterface = static_cast<ITextRangeProvider*>(this);
	}
	else if (riid == __uuidof(ITextRangeProvider))
	{
		*ppInterface = static_cast<ITextRangeProvider*>(this);
	}

	if (*ppInterface)
	{
		AddRef();
		return S_OK;
	}
	else
	{
		return E_NOINTERFACE;
	}
}

FString FWindowsUIATextRangeProvider::TextFromTextRange()
{
	return TextFromTextRange(Widget->AsText()->GetText(), TextRange);
}

FString FWindowsUIATextRangeProvider::TextFromTextRange(const FString& InString, const FTextRange& InRange)
{
	return InString.Mid(InRange.BeginIndex, InRange.EndIndex);
}

ULONG STDCALL FWindowsUIATextRangeProvider::AddRef()
{
	return FWindowsUIABaseProvider::IncrementRef();
}

ULONG STDCALL FWindowsUIATextRangeProvider::Release()
{
	return FWindowsUIABaseProvider::DecrementRef();
}

HRESULT STDCALL FWindowsUIATextRangeProvider::Clone(ITextRangeProvider** pRetVal)
{
	if (IsValid())
	{
		*pRetVal = new FWindowsUIATextRangeProvider(*UIAManager, Widget, TextRange);
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIATextRangeProvider::Compare(ITextRangeProvider* range, BOOL* pRetVal)
{
	// The documentation states that different endpoints that produce the same text are not equal,
	// but doesn't say anything about the same endpoints that come from different control providers.
	// Perhaps we can assume that comparing text ranges from different Widgets is not valid.
	*pRetVal = TextRange == static_cast<FWindowsUIATextRangeProvider*>(range)->TextRange;
	return S_OK;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider* targetRange, TextPatternRangeEndpoint targetEndpoint, int* pRetVal)
{
	FWindowsUIATextRangeProvider* CastedRange = static_cast<FWindowsUIATextRangeProvider*>(targetRange);
	int32 ThisEndpointIndex = (endpoint == TextPatternRangeEndpoint_Start) ? TextRange.BeginIndex : TextRange.EndIndex;
	int32 OtherEndpointIndex = (targetEndpoint == TextPatternRangeEndpoint_Start) ? CastedRange->TextRange.BeginIndex : CastedRange->TextRange.EndIndex;
	*pRetVal = ThisEndpointIndex - OtherEndpointIndex;
	return S_OK;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::ExpandToEnclosingUnit(TextUnit unit)
{
	if (IsValid())
	{
		switch (unit)
		{
		case TextUnit_Character:
			TextRange.EndIndex = FMath::Min(TextRange.BeginIndex + 1, Widget->AsText()->GetText().Len());
			break;
		case TextUnit_Format:
			return E_NOTIMPL;
		case TextUnit_Word:
			return E_NOTIMPL;
		case TextUnit_Line:
			return E_NOTIMPL;
		case TextUnit_Paragraph:
			return E_NOTIMPL;
		case TextUnit_Page:
			return E_NOTIMPL;
		case TextUnit_Document:
			TextRange = FTextRange(0, Widget->AsText()->GetText().Len());
			break;
		}

		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIATextRangeProvider::FindAttribute(TEXTATTRIBUTEID attributeId, VARIANT val, BOOL backward, ITextRangeProvider** pRetVal)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::FindText(BSTR text, BOOL backward, BOOL ignoreCase, ITextRangeProvider** pRetVal)
{
	if (IsValid())
	{
		FString TextToSearch(text);
		int32 FoundIndex = TextFromTextRange().Find(TextToSearch, ignoreCase ? ESearchCase::IgnoreCase : ESearchCase::CaseSensitive, backward ? ESearchDir::FromEnd : ESearchDir::FromStart);
		if (FoundIndex == INDEX_NONE)
		{
			*pRetVal = nullptr;
		}
		else
		{
			const int32 StartIndex = TextRange.BeginIndex + FoundIndex;
			*pRetVal = new FWindowsUIATextRangeProvider(*UIAManager, Widget, FTextRange(StartIndex, StartIndex + TextToSearch.Len()));
		}
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIATextRangeProvider::GetAttributeValue(TEXTATTRIBUTEID attributeId, VARIANT* pRetVal)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::GetBoundingRectangles(SAFEARRAY** pRetVal)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::GetEnclosingElement(IRawElementProviderSimple** pRetVal)
{
	if (IsValid())
	{
		*pRetVal = static_cast<IRawElementProviderSimple*>(&UIAManager->GetWidgetProvider(Widget));
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIATextRangeProvider::GetText(int maxLength, BSTR* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = SysAllocString(*TextFromTextRange().Left(maxLength));
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIATextRangeProvider::Move(TextUnit unit, int count, int* pRetVal)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count, int* pRetVal)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider* targetRange, TextPatternRangeEndpoint targetEndpoint)
{
	FWindowsUIATextRangeProvider* CastedRange = static_cast<FWindowsUIATextRangeProvider*>(targetRange);
	int32 NewIndex = (targetEndpoint == TextPatternRangeEndpoint_Start) ? CastedRange->TextRange.BeginIndex : CastedRange->TextRange.EndIndex;
	if (endpoint == TextPatternRangeEndpoint_Start)
	{
		TextRange.BeginIndex = NewIndex;
		if (TextRange.BeginIndex > TextRange.EndIndex)
		{
			TextRange.EndIndex = TextRange.BeginIndex;
		}
	}
	else
	{
		TextRange.EndIndex = NewIndex;
		if (TextRange.BeginIndex > TextRange.EndIndex)
		{
			TextRange.BeginIndex = TextRange.EndIndex;
		}
	}
	return S_OK;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::Select()
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::AddToSelection()
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::RemoveFromSelection()
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::ScrollIntoView(BOOL alignToTop)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIATextRangeProvider::GetChildren(SAFEARRAY** pRetVal)
{
	*pRetVal = nullptr;
	return S_OK;
}

// ~

// FWindowsUIAControlProvider

FWindowsUIAControlProvider::FWindowsUIAControlProvider(FWindowsUIAManager& InManager, TSharedRef<IAccessibleWidget> InWidget)
	: FWindowsUIABaseProvider(InManager, InWidget)
{
}

FWindowsUIAControlProvider::~FWindowsUIAControlProvider()
{
}

HRESULT STDCALL FWindowsUIAControlProvider::QueryInterface(REFIID riid, void** ppInterface)
{
	*ppInterface = nullptr;

	if (riid == __uuidof(IInvokeProvider))
	{
		*ppInterface = static_cast<IInvokeProvider*>(this);
	}
	else if (riid == __uuidof(IRangeValueProvider))
	{
		*ppInterface = static_cast<IRangeValueProvider*>(this);
	}
	else if (riid == __uuidof(ITextProvider))
	{
		*ppInterface = static_cast<ITextProvider*>(this);
	}
	else if (riid == __uuidof(IToggleProvider))
	{
		*ppInterface = static_cast<IToggleProvider*>(this);
	}
	else if (riid == __uuidof(IValueProvider))
	{
		*ppInterface = static_cast<IValueProvider*>(this);
	}
	else if (riid == __uuidof(IWindowProvider))
	{
		*ppInterface = static_cast<IWindowProvider*>(this);
	}

	if (*ppInterface)
	{
		AddRef();
		return S_OK;
	}
	else
	{
		return E_NOINTERFACE;
	}
}

ULONG STDCALL FWindowsUIAControlProvider::AddRef()
{
	return FWindowsUIABaseProvider::IncrementRef();
}

ULONG STDCALL FWindowsUIAControlProvider::Release()
{
	return FWindowsUIABaseProvider::DecrementRef();
}

HRESULT STDCALL FWindowsUIAControlProvider::Invoke()
{
	if (IsValid())
	{
		Widget->AsActivatable()->Activate();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::SetValue(double val)
{
	if (IsValid())
	{
		Widget->AsProperty()->SetValue(FString::SanitizeFloat(val));
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_Value(double* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_RangeValueValuePropertyId).GetValue<double>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_IsReadOnly(BOOL* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_ValueIsReadOnlyPropertyId).GetValue<bool>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_Maximum(double* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_RangeValueMaximumPropertyId).GetValue<double>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_Minimum(double* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_RangeValueMinimumPropertyId).GetValue<double>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_LargeChange(double* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_RangeValueLargeChangePropertyId).GetValue<double>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_SmallChange(double* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_RangeValueSmallChangePropertyId).GetValue<double>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_DocumentRange(ITextRangeProvider** pRetVal)
{
	if (IsValid())
	{
		*pRetVal = static_cast<ITextRangeProvider*>(new FWindowsUIATextRangeProvider(*UIAManager, Widget, FTextRange(0, Widget->AsText()->GetText().Len())));
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_SupportedTextSelection(SupportedTextSelection* pRetVal)
{
	// todo: implement selection
	*pRetVal = SupportedTextSelection_None;
	return S_OK;
}

HRESULT STDCALL FWindowsUIAControlProvider::GetSelection(SAFEARRAY** pRetVal)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIAControlProvider::GetVisibleRanges(SAFEARRAY** pRetVal)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIAControlProvider::RangeFromChild(IRawElementProviderSimple* childElement, ITextRangeProvider** pRetVal)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIAControlProvider::RangeFromPoint(UiaPoint point, ITextRangeProvider** pRetVal)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIAControlProvider::get_ToggleState(ToggleState* pRetVal)
{
	//ECheckBoxState CheckState = Widget->AsActivatable()->GetCheckedState();
	//switch (CheckState)
	//{
	//case ECheckBoxState::Checked:
	//	*pRetVal = ToggleState_On;
	//	break;
	//case ECheckBoxState::Unchecked:
	//	*pRetVal = ToggleState_Off;
	//	break;
	//case ECheckBoxState::Undetermined:
	//	*pRetVal = ToggleState_Indeterminate;
	//	break;
	//}
	if (IsValid())
	{
		*pRetVal = static_cast<ToggleState>(WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_ToggleToggleStatePropertyId).GetValue<int32>());
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::Toggle()
{
	if (IsValid())
	{
		Widget->AsActivatable()->Activate();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_CanMove(BOOL *pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_TransformCanMovePropertyId).GetValue<bool>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_CanResize(BOOL *pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_TransformCanResizePropertyId).GetValue<bool>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_CanRotate(BOOL *pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_TransformCanRotatePropertyId).GetValue<bool>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::Move(double x, double y)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIAControlProvider::Resize(double width, double height)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIAControlProvider::Rotate(double degrees)
{
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIAControlProvider::SetValue(LPCWSTR val)
{
	if (IsValid())
	{
		Widget->AsProperty()->SetValue(FString(val));
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_Value(BSTR* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = SysAllocString(*WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_ValueValuePropertyId).GetValue<FString>());
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

//HRESULT STDCALL FWindowsUIAControlProvider::get_IsReadOnly(BOOL* pRetVal)
//{
//	*pRetVal = Widget->AsProperty()->IsReadOnly();
//	return S_OK;
//}

HRESULT STDCALL FWindowsUIAControlProvider::Close()
{
	if (IsValid())
	{
		Widget->AsWindow()->Close();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_CanMaximize(BOOL* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_WindowCanMaximizePropertyId).GetValue<bool>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_CanMinimize(BOOL* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_WindowCanMinimizePropertyId).GetValue<bool>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_IsModal(BOOL* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_WindowIsModalPropertyId).GetValue<bool>();
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::get_IsTopmost(BOOL* pRetVal)
{
	// todo: not 100% sure what this is looking for. top window in hierarchy of child windows? on top of all other windows in Windows OS?
	*pRetVal = WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_WindowIsTopmostPropertyId).GetValue<bool>();
	return E_NOTIMPL;
}

HRESULT STDCALL FWindowsUIAControlProvider::get_WindowInteractionState(WindowInteractionState* pRetVal)
{
	if (IsValid())
	{
		// todo: do we have a way to identify if the app is processing data vs idling?
		*pRetVal = static_cast<WindowInteractionState>(WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_WindowWindowInteractionStatePropertyId).GetValue<int32>());
	}
	else
	{
		*pRetVal = WindowInteractionState_Closing;
	}
	return S_OK;
}

HRESULT STDCALL FWindowsUIAControlProvider::get_WindowVisualState(WindowVisualState* pRetVal)
{
	if (IsValid())
	{
		*pRetVal = static_cast<WindowVisualState>(WindowsUIAPropertyGetters::GetPropertyValue(Widget, UIA_WindowWindowVisualStatePropertyId).GetValue<int32>());
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::SetVisualState(WindowVisualState state)
{
	if (IsValid())
	{
		switch (state)
		{
		case WindowVisualState_Normal:
			Widget->AsWindow()->SetDisplayState(IAccessibleWindow::EWindowDisplayState::Normal);
			break;
		case WindowVisualState_Minimized:
			Widget->AsWindow()->SetDisplayState(IAccessibleWindow::EWindowDisplayState::Minimize);
			break;
		case WindowVisualState_Maximized:
			Widget->AsWindow()->SetDisplayState(IAccessibleWindow::EWindowDisplayState::Maximize);
			break;
		}
		return S_OK;
	}
	else
	{
		return UIA_E_ELEMENTNOTAVAILABLE;
	}
}

HRESULT STDCALL FWindowsUIAControlProvider::WaitForInputIdle(int milliseconds, BOOL* pRetVal)
{
	return E_NOTIMPL;
}

#endif
