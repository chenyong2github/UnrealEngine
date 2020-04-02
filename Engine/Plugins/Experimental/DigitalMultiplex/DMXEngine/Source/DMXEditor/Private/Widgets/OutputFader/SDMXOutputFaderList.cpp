// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/OutputFader/SDMXOutputFaderList.h"

#include "Interfaces/IDMXProtocol.h"

#include "DMXEditor.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFader.h"
#include "DMXEditorLog.h"
#include "DMXEditorUtils.h"

#include "Widgets/OutputFader/SDMXFader.h"
#include "Widgets/OutputFader/SDMXFaderChannel.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SDMXOutputFaderList"

void SDMXOutputFaderList::Construct(const FArguments& InArgs)
{
	WeakDMXEditor = InArgs._DMXEditor;
	WeakFaderTemplate = InArgs._FaderTemplate;

	if (TSharedPtr<FDMXEditor> DMXEditorPtr = WeakDMXEditor.Pin())
	{
		WeakDMXLibrary = DMXEditorPtr->GetDMXLibrary();
	}

	static const float Padding_Borders = 5.0f;
	static const float Padding_KeyVal = 10.0f;
	static const float Padding_NewInput = 35.0f;

	ChildSlot
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Left)
			.AutoHeight()
			.Padding(5.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Left)
				.AutoWidth()
				[
					SNew(SBox)
					.HeightOverride(23)
					[
						SNew(SButton)
						.Text(LOCTEXT("AddFader", "Add Fader"))
						.HAlign(HAlign_Center)
						.OnClicked(this, &SDMXOutputFaderList::HandleAddFaderClicked)
					]
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Left)
				.AutoWidth()
				.Padding(10.0f, 0.f, 0.f, 0.f)
				[
					SNew(SBox)
					.HeightOverride(23)
					[
						SNew(SButton)
						.Text(LOCTEXT("UpdateFader", "Update Selected Fader"))
						.HAlign(HAlign_Center)
						.OnClicked(this, &SDMXOutputFaderList::HandleUpdateFaderClicked)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(FaderSlots, SScrollBox)
				.Orientation(EOrientation::Orient_Horizontal)
			]
		];

	// Reconstruct All Widgets
	if (TSharedPtr<FDMXEditor> DMXEditorPtr = WeakDMXEditor.Pin())
	{
		if (UDMXLibrary* DMXLibrary = WeakDMXLibrary.Get())
		{
			DMXLibrary->ForEachEntityOfType<UDMXEntityFader>([this](UDMXEntityFader* Fader)
				{
					AddFader(Fader->GetDisplayName());
				});
		}
	}
}

FReply SDMXOutputFaderList::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// TODO. implement
	}
	return FReply::Handled();
}

FReply SDMXOutputFaderList::HandleAddFaderClicked()
{
	AddFader();
	//DeselectFaders();
	return FReply::Handled();
}

FReply SDMXOutputFaderList::HandleUpdateFaderClicked()
{
	if (TSharedPtr<SDMXFader> SelectedPtr = WeakSelectedFaderWidget.Pin())
	{
		UDMXEntityFader* FaderTemplate = WeakFaderTemplate.Get();
		check(FaderTemplate);

		// Is the Fader protocol still the same?
		if (FaderTemplate->DeviceProtocol == SelectedPtr->GetProtocol())
		{
			// Check for Universes/Channels about to be removed from the fader
			const TArray<TSharedPtr<SDMXFaderChannel>>& FaderWidgetChannels = SelectedPtr->GetChannels();
			const TArray<FDMXUniverse>& FaderEntityUniverses = FaderTemplate->Universes;

			for (const TSharedPtr<SDMXFaderChannel>& WidgetChannel : FaderWidgetChannels)
			{
				bool bHasUniverseAndChannel = false;
				for (const FDMXUniverse& EntityUniverse : FaderEntityUniverses)
				{
					if (WidgetChannel->GetUniverseNumber() == EntityUniverse.UniverseNumber
						&& WidgetChannel->GetChannelNumber() == EntityUniverse.Channel)
					{
						bHasUniverseAndChannel = true;
						break;
					}
				}

				// Has this universe/channel been deleted?
				if (!bHasUniverseAndChannel)
				{
					CompactFragmentMaps(WidgetChannel->GetUniverseNumber(), WidgetChannel->GetChannelNumber(), SelectedPtr.ToSharedRef());
				}
			}
		}
		else // protocol has been changed
		{
			// Try to delete FragmentMaps related to this fader, in case they are not
			// used by any other faders.
			for (const TSharedPtr<SDMXFaderChannel>& FaderChannel : SelectedPtr->GetChannels())
			{
				CompactFragmentMaps(FaderChannel->GetUniverseNumber(), FaderChannel->GetChannelNumber(), SelectedPtr.ToSharedRef());
			}
		}

		check(SelectedPtr->GetWeakFaderEntity().IsValid());
		UDMXEntityFader* FaderEntity = SelectedPtr->GetWeakFaderEntity().Get();

		SelectedPtr->RemoveAllChannelWidgets();
		SetFaderProperties(SelectedPtr, FaderEntity, true);

		if (SelectedPtr->ShouldSendDMX())
		{
			// Make sure the entries in FragmentMaps related to this fader's Universes and Addresses all exist
			HandleFaderSendStateChanged(SelectedPtr.ToSharedRef());
			// Sends data from this fader to keep the output up to date with the new changes
			HandleFaderValueChanged(SelectedPtr.ToSharedRef());
		}
	}

	return FReply::Handled();
}

void SDMXOutputFaderList::HandleFaderValueChanged(TSharedRef<SDMXFader> InFaderWidget)
{
	const IDMXProtocolPtr Protocol = InFaderWidget->GetProtocol();
	check(Protocol.IsValid());

	const FName ProtocolName = Protocol->GetProtocolName();
	const TArray<TSharedPtr<SDMXFaderChannel>>& FaderChannels = InFaderWidget->GetChannels();
	const uint8 NewValue = InFaderWidget->GetCurrentValue();

	TSet<uint16> ChangedUniverseIds;
	ChangedUniverseIds.Reserve(FaderChannels.Num());

	// Update all necessary fragments affected by this fader
	for (const TSharedPtr<SDMXFaderChannel>& FaderChannel : FaderChannels)
	{
		FragmentMaps[ProtocolName][FaderChannel->GetUniverseNumber()][FaderChannel->GetChannelNumber()] = NewValue;
		ChangedUniverseIds.Add(FaderChannel->GetUniverseNumber());
	}

	// Sends fragment maps for each of the affected universes
	for (const uint16& UniverseID : ChangedUniverseIds)
	{
		Protocol->SendDMXFragment(UniverseID, FragmentMaps[ProtocolName][UniverseID]);
	}
}

void SDMXOutputFaderList::HandleFaderSendStateChanged(TSharedRef<SDMXFader> InFaderWidget)
{
	const bool bSendDMXData = InFaderWidget->ShouldSendDMX();

	if (bSendDMXData)
	{
		check(InFaderWidget->GetProtocol());
		const FName ProtocolName = InFaderWidget->GetProtocol();

		if (FragmentMaps.Find(ProtocolName) == nullptr)
		{
			FragmentMaps.Add(ProtocolName);
		}

		// Make sure the fragment maps for the universes and addresses related to this fader
		// exist in the FragmentMaps entries.
		for (const TSharedPtr<SDMXFaderChannel>& FaderChannel : InFaderWidget->GetChannels())
		{
			const uint16 UniverseID = FaderChannel->GetUniverseNumber();
			const uint16 Address = FaderChannel->GetChannelNumber();

			if (FragmentMaps[ProtocolName].Find(UniverseID) == nullptr)
			{
				FragmentMaps[ProtocolName].Add(UniverseID);
			}

			if (FragmentMaps[ProtocolName][UniverseID].Find(Address) == nullptr)
			{
				FragmentMaps[ProtocolName][UniverseID].Add(Address);
			}
		}
	}
	else
	{
		// Try to delete FragmentMaps related to this fader, in case they are not
		// used by any other faders.
		for (const TSharedPtr<SDMXFaderChannel>& FaderChannel : InFaderWidget->GetChannels())
		{
			CompactFragmentMaps(FaderChannel->GetUniverseNumber(), FaderChannel->GetChannelNumber(), InFaderWidget);
		}
	}
}

void SDMXOutputFaderList::ResetFaderBackgrounds()
{
	for (TSharedPtr<SDMXFader> Fader : FaderWidgets)
	{
		if (Fader.IsValid())
		{
			Fader->GetBackgroundBorder()->SetBorderImage(FEditorStyle::GetBrush("DetailsView.CategoryMiddle"));
		}
	}
}

void SDMXOutputFaderList::AddFader(const FString& InName /*= TEXT("")*/)
{
	TSharedRef<SDMXFader> NewFader = SNew(SDMXFader)
		.DMXEditor(WeakDMXEditor)
		.InText(LOCTEXT("FaderLabel", "Fader"))
		.OnValueChanged(this, &SDMXOutputFaderList::HandleFaderValueChanged)
		.OnSendStateChanged(this, &SDMXOutputFaderList::HandleFaderSendStateChanged);

	FaderSlots->AddSlot()
	[
		NewFader
	];

	if (TSharedPtr<FDMXEditor> DMXEditorPtr = WeakDMXEditor.Pin())
	{
		if (UDMXLibrary* DMXLibrary = DMXEditorPtr->GetDMXLibrary())
		{
			if (UDMXEntityFader* FaderEntity = Cast<UDMXEntityFader>(DMXLibrary->GetOrCreateEntityObject(InName, UDMXEntityFader::StaticClass())))
			{
				NewFader->SetFaderEntity(FaderEntity);
				NewFader->SetParentFaderList(SharedThis(this));
				FaderWidgets.Add(NewFader);

				SetFaderProperties(NewFader, FaderEntity, InName.IsEmpty());
				DeselectFaders();
				NewFader->SelectThisFader();

				// Make sure the fader's channels have entries related to them in FragmentMaps
				HandleFaderSendStateChanged(NewFader);
			}
		}
	}
}

void SDMXOutputFaderList::CompactFragmentMaps(uint16 RemovedUniverseID, uint16 RemovedAddress, const TSharedRef<SDMXFader> FaderInstigator)
{
	bool bUniverseIsStillUsed = false;
	bool bAddressIsStillUsed = false;

	const FDMXProtocolName RemoveFromProtocol = FaderInstigator->GetProtocol();

	// If the universe or address entries already don't exist in FragmentMaps, we don't need to do anything.
	if (FragmentMaps.Find(RemoveFromProtocol) == nullptr
		|| FragmentMaps[RemoveFromProtocol].Find(RemovedUniverseID) == nullptr
		|| FragmentMaps[RemoveFromProtocol][RemovedUniverseID].Find(RemovedAddress) == nullptr)
	{
		return;
	}

	for (const TSharedPtr<SDMXFader>& FaderWidget : FaderWidgets)
	{
		// Skip the fader which channels are being removed from. We only need to know about the other faders.
		if (FaderWidget == FaderInstigator)
		{
			continue;
		}

		// Skip faders that aren't supposed to send data
		if (!FaderWidget->ShouldSendDMX())
		{
			continue;
		}

		// Skip faders with different protocols. They might have the Universe IDs we're looking for,
		// but we're only looking for Universe IDs on the Instigator Fader protocol.
		if (FaderWidget->GetProtocol() != RemoveFromProtocol)
		{
			continue;
		}

		const TArray<TSharedPtr<SDMXFaderChannel>>& FaderChannels = FaderWidget->GetChannels();
		for (const TSharedPtr<SDMXFaderChannel>& FaderChannel : FaderChannels)
		{
			if (FaderChannel->GetUniverseNumber() == RemovedUniverseID)
			{
				// This universe can't be removed. It's used on other faders.
				bUniverseIsStillUsed = true;

				if (FaderChannel->GetChannelNumber() == RemovedAddress)
				{
					// This Address, in this universe, also can't be removed.
					bAddressIsStillUsed = true;

					// Now we know both can't be removed. There's no need to keep searching through this fader's channels.
					break;
				}
			}
		}

		if (bAddressIsStillUsed)
		{
			// If we already know the address is still used, which means its universe is as well,
			// there's no need to keep searching through other faders.
			break;
		}
	}

	if (!bUniverseIsStillUsed)
	{
		// Deletes the whole FragmentMap for the unused Universe ID
		FragmentMaps[RemoveFromProtocol].Remove(RemovedUniverseID);

		UE_LOG_DMXEDITOR(Log, TEXT("%S: Removed unused Universe ID %d from Output"), __FUNCTION__, RemovedUniverseID);
	}
	else if (!bAddressIsStillUsed)
	{
		// Deletes just the specific address value from the FragmentMap
		FragmentMaps[RemoveFromProtocol][RemovedUniverseID].Remove(RemovedAddress);
		// Delete the Universe in case this was the last registered address in it
		if (FragmentMaps[RemoveFromProtocol][RemovedUniverseID].Num() == 0)
		{
			FragmentMaps[RemoveFromProtocol].Remove(RemovedUniverseID);
			UE_LOG_DMXEDITOR(Log, TEXT("%S: Removed unused Universe ID %d from Output"), __FUNCTION__, RemovedUniverseID);
		}
		else
		{
			UE_LOG_DMXEDITOR(Log, TEXT("%S: Removed unused address %d from Universe ID %d from Output"), __FUNCTION__, RemovedAddress, RemovedUniverseID);
		}
	}
}

void SDMXOutputFaderList::RemoveFader(TSharedPtr<SDMXFader> FaderToRemove)
{
	if (!FaderToRemove.IsValid())
	{
		UE_LOG_DMXEDITOR(Warning, TEXT("FaderToRemove is invalid pointer"));
		return;
	}

	FaderWidgets.Remove(FaderToRemove);
	if (TSharedPtr<FDMXEditor> DMXEditorPtr = WeakDMXEditor.Pin())
	{
		if (UDMXLibrary* DMXLibrary = DMXEditorPtr->GetDMXLibrary())
		{
			if (UDMXEntityFader* FaderEntity = FaderToRemove->GetWeakFaderEntity().Get())
			{
				// Try to delete FragmentMaps related to this fader, in case they are not
				// used anymore by any other faders.
				for (const FDMXUniverse& Universe : FaderEntity->Universes)
				{
					CompactFragmentMaps(Universe.UniverseNumber, Universe.Channel, FaderToRemove.ToSharedRef());
				}

				DMXLibrary->RemoveEntity(FaderEntity);
			}
		}
	}
	FaderSlots->RemoveSlot(FaderToRemove.ToSharedRef());
}

void SDMXOutputFaderList::SetFaderProperties(const TSharedPtr<SDMXFader>& InFaderWidget, const TWeakObjectPtr<UDMXEntityFader>& InFaderObject, bool bIsTransferObject)
{
	if (!InFaderWidget.IsValid())
	{
		UE_LOG_DMXEDITOR(Warning, TEXT("FaderWidget is invalid pointer"));
		return;
	}

	if (!InFaderObject.IsValid())
	{
		UE_LOG_DMXEDITOR(Warning, TEXT("FaderObject is invalid pointer"));
		return;
	}

	// Transfer Object Properties if object is new
	if (bIsTransferObject)
	{
		TransferCreatedFaderObjectProperties(InFaderObject, WeakFaderTemplate);
	}

	InFaderWidget->SetProtocol(InFaderObject->DeviceProtocol);
	InFaderWidget->SetFaderLabel(InFaderObject->GetDisplayName());

	for (const FDMXUniverse& Universe : InFaderObject->Universes)
	{
		// Add protocol universe
		FString TmpFinalUniverse = FString::FromInt(Universe.UniverseNumber);
		FString TmpChannel = FString::FromInt(Universe.Channel);
		InFaderWidget->AddChannelWidget(TmpFinalUniverse, TmpChannel, Universe.UniverseNumber, Universe.Channel);
	}
}

void SDMXOutputFaderList::TransferCreatedFaderObjectProperties(const TWeakObjectPtr<UDMXEntityFader>& TransferTo, const TWeakObjectPtr<UDMXEntityFader>& TransferFrom)
{
	if (TransferTo.IsValid() && TransferFrom.IsValid())
	{
		if (TSharedPtr<FDMXEditor> DMXEditorPtr = WeakDMXEditor.Pin())
		{
			if (UDMXLibrary* DMXLibrary = DMXEditorPtr->GetDMXLibrary())
			{
				// Check if name is different or empty
				if (TransferTo->GetDisplayName().IsEmpty() ||
					(TransferFrom->GetDisplayName().Equals(TransferTo->GetDisplayName()) == false))
				{
					FText Reason;
					bool bIsNameIsValid = FDMXEditorUtils::ValidateEntityName(TransferFrom->GetDisplayName(), DMXLibrary, UDMXEntityFader::StaticClass(), Reason);
					if (!bIsNameIsValid)
					{
						FString UniqueEntityName = FDMXEditorUtils::FindUniqueEntityName(DMXLibrary, UDMXEntityFader::StaticClass());
						TransferTo->SetName(UniqueEntityName);
						TransferFrom->SetName(UniqueEntityName);
					}
					else
					{
						TransferTo->SetName(TransferFrom->GetDisplayName());
					}
				}

				TransferTo->Universes = TransferFrom->Universes;
				TransferTo->DeviceProtocol = TransferFrom->DeviceProtocol;
			}
		}
	}
	else
	{
		UE_LOG_DMXEDITOR(Warning, TEXT("No valid Template or Entity"));
	}
}

void SDMXOutputFaderList::TransferSelectedFaderObjectProperties(const TWeakObjectPtr<UDMXEntityFader>& TransferTo, const TWeakObjectPtr<UDMXEntityFader>& TransferFrom)
{
	if (TransferTo.IsValid() && TransferFrom.IsValid())
	{
		TransferTo->SetName(TransferFrom->GetDisplayName());
		TransferTo->Universes = TransferFrom->Universes;
		TransferTo->DeviceProtocol = TransferFrom->DeviceProtocol;
	}
	else
	{
		UE_LOG_DMXEDITOR(Warning, TEXT("No valid Template or Entity"));
	}
}

void SDMXOutputFaderList::UpdateFaderTemplateObject(const TWeakObjectPtr<UDMXEntityFader>& InFaderObject)
{
	TransferSelectedFaderObjectProperties(WeakFaderTemplate, InFaderObject);
}

void SDMXOutputFaderList::DeselectFaders()
{
	ResetFaderBackgrounds();
	WeakSelectedFaderWidget = nullptr;
}

#undef LOCTEXT_NAMESPACE
