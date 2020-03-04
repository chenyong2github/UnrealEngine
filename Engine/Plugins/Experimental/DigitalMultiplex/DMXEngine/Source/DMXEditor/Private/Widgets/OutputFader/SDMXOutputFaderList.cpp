// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/OutputFader/SDMXOutputFaderList.h"

#include "Interfaces/IDMXProtocol.h"

#include "DMXEditor.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFader.h"
#include "DMXEditorLog.h"
#include "DMXEditorUtils.h"

#include "Widgets/OutputFader/SDMXFader.h"

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
		SelectedPtr->RemoveAllChannelWidgets();
		SetFaderProperties(SelectedPtr, SelectedPtr->GetWeakFaderEntity(), true);
	}
	return FReply::Handled();
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
	TSharedPtr<SDMXFader> NewFader = SNew(SDMXFader)
		.DMXEditor(WeakDMXEditor)
		.InText(LOCTEXT("FaderLabel", "Fader"));
		FaderSlots->AddSlot()
		[
			NewFader.ToSharedRef()
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
			}
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
			DMXLibrary->RemoveEntity(FaderToRemove->GetWeakFaderEntity()->GetDisplayName());
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

	TSharedPtr<IDMXProtocol> DMXProtocol = InFaderObject->DeviceProtocol;

	InFaderWidget->SetFaderLabel(InFaderObject->GetDisplayName());
	for (const FDMXUniverse& Universe : InFaderObject->Universes)
	{
		// Add protocol universe
		FString TmpFinalUniverse = FString::FromInt(Universe.UniverseNumber);
		FString TmpChannel = FString::FromInt(Universe.Channel);
		InFaderWidget->AddChannelWidget(TmpFinalUniverse, TmpChannel, Universe.UniverseNumber, Universe.Channel, DMXProtocol);
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
