// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleFaderDetails.h"

#include "DMXControlConsoleEditorManager.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleRawFader.h"

#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Algo/AllOf.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleFaderDetails"

namespace UE::DMXControlConsole
{
	void FDMXControlConsoleFaderDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
	{
		PropertyUtilities = InDetailLayout.GetPropertyUtilities();

		// Value property handle
		const TSharedPtr<IPropertyHandle> FaderValueHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderBase::GetValuePropertyName());
		FaderValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleFaderDetails::OnSelectedFadersValueChanged));

		// MinValue property handle
		const TSharedPtr<IPropertyHandle> FaderMinValueHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderBase::GetMinValuePropertyName(), UDMXControlConsoleFaderBase::StaticClass());
		FaderMinValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleFaderDetails::OnSelectedFadersMinValueChanged));

		// MaxValue property handle
		const TSharedPtr<IPropertyHandle> FaderMaxValueHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderBase::GetMaxValuePropertyName(), UDMXControlConsoleFaderBase::StaticClass());
		FaderMaxValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleFaderDetails::OnSelectedFadersMaxValueChanged));

		// UniverseID property handle
		const TSharedPtr<IPropertyHandle> FaderUniverseIDHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderBase::GetUniverseIDPropertyName(), UDMXControlConsoleFaderBase::StaticClass());
		FaderUniverseIDHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleFaderDetails::OnSelectedFadersUniverseIDChanged));

		// StartingAddress property handle
		const TSharedPtr<IPropertyHandle> FaderStartingAddressHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderBase::GetStartingAddressPropertyName(), UDMXControlConsoleFaderBase::StaticClass());
		FaderStartingAddressHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleFaderDetails::OnSelectedFadersDataTypeChanged));

		// DataType property handle
		const TSharedPtr<IPropertyHandle> FaderDataTypeHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderBase::GetDataTypePropertyName(), UDMXControlConsoleFaderBase::StaticClass());
		FaderDataTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXControlConsoleFaderDetails::OnSelectedFadersDataTypeChanged));
		if (!HasOnlyRawFadersSelected())
		{
			InDetailLayout.HideProperty(FaderDataTypeHandle);
		}
	}

	void FDMXControlConsoleFaderDetails::ForceRefresh() const
	{
		if (!PropertyUtilities.IsValid())
		{
			return;
		}

		PropertyUtilities->ForceRefresh();
	}

	bool FDMXControlConsoleFaderDetails::HasOnlyRawFadersSelected() const
	{
		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
		const TArray<TWeakObjectPtr<UObject>> SelectedFaderObjects = SelectionHandler->GetSelectedFaders();

		auto AreAllRawFadersLambda = [](const TWeakObjectPtr<UObject>& SelectedFaderObject)
		{
			const UDMXControlConsoleRawFader* SelectedRawFader = Cast<UDMXControlConsoleRawFader>(SelectedFaderObject);
			if (SelectedRawFader)
			{
				return true;
			}

			return false;
		};

		return Algo::AllOf(SelectedFaderObjects, AreAllRawFadersLambda);
	}

	void FDMXControlConsoleFaderDetails::OnSelectedFadersValueChanged() const
	{
		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
		const TArray<TWeakObjectPtr<UObject>> SelectedFaderObjects = SelectionHandler->GetSelectedFaders();
		for (const TWeakObjectPtr<UObject>& SelectedFaderObject : SelectedFaderObjects)
		{
			UDMXControlConsoleFaderBase* SelectedFader = Cast<UDMXControlConsoleFaderBase>(SelectedFaderObject);
			if (!SelectedFader)
			{
				continue;
			}

			const uint32 CurrentValue = SelectedFader->GetValue();
			SelectedFader->SetValue(CurrentValue);
		}
	}

	void FDMXControlConsoleFaderDetails::OnSelectedFadersMinValueChanged() const
	{
		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
		const TArray<TWeakObjectPtr<UObject>> SelectedFaderObjects = SelectionHandler->GetSelectedFaders();
		for (const TWeakObjectPtr<UObject>& SelectedFaderObject : SelectedFaderObjects)
		{
			UDMXControlConsoleFaderBase* SelectedFader = Cast<UDMXControlConsoleFaderBase>(SelectedFaderObject);
			if (!SelectedFader)
			{
				continue;
			}

			const uint32 CurrentMinValue = SelectedFader->GetMinValue();
			SelectedFader->SetMinValue(CurrentMinValue);
		}
	}

	void FDMXControlConsoleFaderDetails::OnSelectedFadersMaxValueChanged() const
	{
		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
		const TArray<TWeakObjectPtr<UObject>> SelectedFaderObjects = SelectionHandler->GetSelectedFaders();
		for (const TWeakObjectPtr<UObject>& SelectedFaderObject : SelectedFaderObjects)
		{
			UDMXControlConsoleFaderBase* SelectedFader = Cast<UDMXControlConsoleFaderBase>(SelectedFaderObject);
			if (!SelectedFader)
			{
				continue;
			}

			const uint32 CurrentMaxValue = SelectedFader->GetMaxValue();
			SelectedFader->SetMaxValue(CurrentMaxValue);
		}
	}

	void FDMXControlConsoleFaderDetails::OnSelectedFadersUniverseIDChanged() const
	{
		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
		const TArray<TWeakObjectPtr<UObject>> SelectedFaderObjects = SelectionHandler->GetSelectedFaders();
		for (const TWeakObjectPtr<UObject>& SelectedFaderObject : SelectedFaderObjects)
		{
			UDMXControlConsoleRawFader* SelectedFader = Cast<UDMXControlConsoleRawFader>(SelectedFaderObject);
			if (!SelectedFader)
			{
				continue;
			}

			const int32 CurrentUniverseID = SelectedFader->GetUniverseID();
			SelectedFader->SetUniverseID(CurrentUniverseID);
		}
	}

	void FDMXControlConsoleFaderDetails::OnSelectedFadersDataTypeChanged() const
	{
		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
		const TArray<TWeakObjectPtr<UObject>> SelectedFaderObjects = SelectionHandler->GetSelectedFaders();
		for (const TWeakObjectPtr<UObject>& SelectedFaderObject : SelectedFaderObjects)
		{
			UDMXControlConsoleRawFader* SelectedFader = Cast<UDMXControlConsoleRawFader>(SelectedFaderObject);
			if (!SelectedFader)
			{
				continue;
			}

			const EDMXFixtureSignalFormat CurrentDataType = SelectedFader->GetDataType();
			SelectedFader->SetDataType(CurrentDataType);
			const uint32 CurrentValue = SelectedFader->GetValue();
			SelectedFader->SetValue(CurrentValue);
		}
	}
}

#undef LOCTEXT_NAMESPACE
