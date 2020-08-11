// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXFixturePatcher.h"

#include "DMXEditor.h"
#include "DMXEditorTabs.h"
#include "DMXEntityDragDropOp.h"
#include "DMXFixturePatchSharedData.h"
#include "DMXFixturePatchNode.h"
#include "SDMXPatchedUniverse.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SNullWidget.h"

#include "EditorStyleSet.h"
#include "ScopedTransaction.h"
#include "SlateOptMacros.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "SDMXFixturePatcher"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SDMXFixturePatcher::Construct(const FArguments& InArgs)
{
	DMXEditorPtr = InArgs._DMXEditor;
	OnPatched = InArgs._OnPatched;

	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		SharedData = DMXEditor->GetFixturePatchSharedData();
		check(SharedData.IsValid());
		
		const FLinearColor BackgroundTint(0.6f, 0.6f, 0.6f, 1.0f);

		ChildSlot			
			[
				SNew(SBox)
				.HAlign(HAlign_Left)
				.ToolTipText(this, &SDMXFixturePatcher::GetTooltipText)
				[
					SNew(SVerticalBox)				

					// Settings area

					+ SVerticalBox::Slot()
					.HAlign(HAlign_Fill)
					.AutoHeight()
					[

						SNew(SBorder)					
						.HAlign(HAlign_Fill)
						.BorderBackgroundColor(BackgroundTint)
						.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
						[
							SNew(SHorizontalBox)			

							+ SHorizontalBox::Slot()						
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(4.0f, 4.0f, 15.0f, 4.0f))
							[
								SNew(STextBlock)							
								.MinDesiredWidth(75.0f)
								.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
								.TextStyle(FEditorStyle::Get(), "DetailsView.CategoryTextStyle")
								.IsEnabled(this, &SDMXFixturePatcher::IsUniverseSelectionEnabled)
								.Text(LOCTEXT("UniverseSelectorLabel", "Universe"))
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(4.0f, 4.0f, 15.0f, 4.0f))
							[
								SNew(SBox)
								.MinDesiredWidth(210.0f)
								.MaxDesiredWidth(420.0f)
								[
									SNew(SSpinBox<int32>)								
									.SliderExponent(1000.0f)								
									.MinSliderValue(0)
									.MaxSliderValue(DMX_MAX_UNIVERSE - 1)
									.MinValue(0)
									.MaxValue(DMX_MAX_UNIVERSE - 1)
									.IsEnabled(this, &SDMXFixturePatcher::IsUniverseSelectionEnabled)
									.Value(this, &SDMXFixturePatcher::GetSelectedUniverse)
									.OnValueChanged(this, &SDMXFixturePatcher::SelectUniverse)
								]
							]
						
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(4.0f, 4.0f, 15.0f, 4.0f))						
							[
								SNew(SSeparator)
								.Orientation(EOrientation::Orient_Vertical)
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(4.0f, 4.0f, 15.0f, 4.0f))
							[
								SNew(STextBlock)
								.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
								.Text(LOCTEXT("UniverseDisplayAllText", "Show all patched Universes"))
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(4.0f, 4.0f, 15.0f, 4.0f))
							[
								SAssignNew(ShowAllUniversesCheckBox, SCheckBox)
								.IsChecked(false)
								.OnCheckStateChanged(this, &SDMXFixturePatcher::OnToggleDisplayAllUniverses)
							]
						]
					]

					// Patched Universes

					+ SVerticalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Fill)
					[	
						SAssignNew(PatchedUniverseScrollBox, SScrollBox)					
						.Orientation(EOrientation::Orient_Vertical)	
					]
				]
			];

		// Bind to selection
		SharedData->OnFixturePatchSelectionChanged.AddSP(this, &SDMXFixturePatcher::OnFixturePatchSelectionChanged);
		SharedData->OnUniverseSelectionChanged.AddSP(this, &SDMXFixturePatcher::OnUniverseSelectionChanged);

		// If the selected universe has no patches, try to find one with patches instead
		UDMXLibrary* Library = GetDMXLibrary();
		if (Library)
		{
			TArray<UDMXEntityFixturePatch*> Patches = Library->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
			UDMXEntityFixturePatch** ExistingPatchPtr = Patches.FindByPredicate([&](UDMXEntityFixturePatch* Patch) {
				return Patch->UniverseID == SharedData->GetSelectedUniverse();
				});
			if (!ExistingPatchPtr && Patches.Num() > 0)
			{
				SharedData->SelectUniverse(Patches[0]->UniverseID);
			}
		}

		FGlobalTabmanager::Get()->OnActiveTabChanged_Subscribe(FOnActiveTabChanged::FDelegate::CreateSP(this, &SDMXFixturePatcher::OnActiveTabChanged));

		GEditor->RegisterForUndo(this);

		ShowSelectedUniverse();
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SDMXFixturePatcher::NotifyPropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXEntityFixturePatch, UniverseID) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXEntityFixturePatch, ManualStartingAddress))
	{
		if (IsUniverseSelectionEnabled() && PropertyChangedEvent.GetNumObjectsBeingEdited() == 1)
		{
			const UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(PropertyChangedEvent.GetObjectBeingEdited(0));
			check(FixturePatch);

			SelectUniverse(FixturePatch->UniverseID);
		}
		else
		{
			RefreshFromProperties();
		}
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXEntityFixturePatch, bAutoAssignAddress) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXEntityFixturePatch, EditorColor) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXEntityFixturePatch, ActiveMode))
	{
		RefreshFromProperties();
	}
}

void SDMXFixturePatcher::RefreshFromProperties()
{
	if (IsUniverseSelectionEnabled())
	{
		ShowSelectedUniverse();
	}
	else
	{
		ShowAllPatchedUniverses();
	}
}

void SDMXFixturePatcher::RefreshFromLibrary()
{
	bool bForceReconstructWidget = true;
	if (IsUniverseSelectionEnabled())
	{
		ShowSelectedUniverse(bForceReconstructWidget);
	}
	else
	{
		ShowAllPatchedUniverses(bForceReconstructWidget);
	}
}

void SDMXFixturePatcher::SelectUniverseThatContainsSelectedPatches()
{
	UDMXLibrary* Library = GetDMXLibrary();
	if (Library)
	{
		// If the selected universe no longer contains a patch, select another universe with patches		
		check(SharedData.IsValid());
		const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>& SelectedFixturePatches = SharedData->GetSelectedFixturePatches();

		if (SelectedFixturePatches.Num() == 0)
		{
			return;
		}

		TArray<UDMXEntityController*> Controllers = Library->GetEntitiesTypeCast<UDMXEntityController>();

		int32 SelectedUniverseID = GetSelectedUniverse();

		// Show a universe that contains a selected patch
		int32 PatchInSelectedUniverseIndex = SelectedFixturePatches.IndexOfByPredicate([SelectedUniverseID](TWeakObjectPtr<UDMXEntityFixturePatch> Patch) {
			return 
				Patch.IsValid() && 
				Patch->UniverseID == SelectedUniverseID;
			});

		if (PatchInSelectedUniverseIndex == INDEX_NONE)
		{
			SharedData->SelectUniverse(SelectedFixturePatches[0]->UniverseID);
		}
	}
}

void SDMXFixturePatcher::OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated)
{
	if (!NewlyActivated.IsValid() ||
		NewlyActivated->GetLayoutIdentifier().TabType == FDMXEditorTabs::DMXFixturePatchEditorTabId)
	{
		RefreshFromLibrary();
	}
}

void SDMXFixturePatcher::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (UniverseToSetNextTick != INDEX_NONE)
	{
		SharedData->SelectUniverse(UniverseToSetNextTick);
		UniverseToSetNextTick = INDEX_NONE;
	}
}

void SDMXFixturePatcher::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (!Operation.IsValid())
	{
		return;
	}

	if (TSharedPtr<FDMXEntityDragDropOperation> EntityDragDropOp = DragDropEvent.GetOperationAs<FDMXEntityDragDropOperation>())
	{		
		const TArray<TWeakObjectPtr<UDMXEntity>>& DraggedEntities = EntityDragDropOp->GetDraggedEntities();

		InitDragDrop(DraggedEntities);
	}
}

void SDMXFixturePatcher::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	check(DraggedNode.IsValid());

	DraggedNode->SetVisiblity(EVisibility::Visible);
	DraggedNode = nullptr;
}

FReply SDMXFixturePatcher::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (!Operation.IsValid())
	{
		return FReply::Unhandled();
	}

	if (TSharedPtr<FDMXEntityDragDropOperation> EntityDragDropOp = DragDropEvent.GetOperationAs<FDMXEntityDragDropOperation>())
	{
		if (DraggedNode.IsValid())
		{
			DraggedNode->SetVisiblity(EVisibility::Visible);
			DraggedNode.Reset();
		}

		return FReply::Handled().EndDragDrop();
	}

	return FReply::Unhandled();
}

void SDMXFixturePatcher::OnDragEnterChannel(int32 UniverseID, int32 ChannelID, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (!Operation.IsValid())
	{
		return;
	}

	if (TSharedPtr<FDMXEntityDragDropOperation> EntityDragDropOp = DragDropEvent.GetOperationAs<FDMXEntityDragDropOperation>())
	{
		const TArray<TWeakObjectPtr<UDMXEntity>>& DraggedEntities = EntityDragDropOp->GetDraggedEntities();

		if (!DraggedNode.IsValid() ||
			!DraggedEntities.Contains(DraggedNode->GetFixturePatch()))
		{
			InitDragDrop(DraggedEntities);
		}

		if (DraggedEntities.Num() > 1)
		{
			EntityDragDropOp->SetFeedbackMessageError(LOCTEXT("CannotDragDropMoreThanOnePatch", "Multi asset drag drop is not supported."));
		}
		else if (DraggedNode.IsValid())
		{
			const TSharedPtr<SDMXPatchedUniverse>& Universe = PatchedUniversesByID.FindChecked(UniverseID);
			
			bool bCreateTransaction = false;
			if (Universe->Patch(DraggedNode, ChannelID, bCreateTransaction))
			{
				UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(DraggedEntities[0]);
				TSharedRef<SWidget> DragDropDecorator = CreateDragDropDecorator(FixturePatch, ChannelID);
				EntityDragDropOp->SetCustomFeedbackWidget(DragDropDecorator);
			}
			else if (!DraggedNode->IsPatched())
			{
				UDMXEntityFixturePatch* Patch = DraggedNode->GetFixturePatch().Get();
				if (Patch)
				{
					if (ChannelID + Patch->GetChannelSpan() > DMX_UNIVERSE_SIZE)
					{
						EntityDragDropOp->SetFeedbackMessageError(LOCTEXT("CannotDragDropOnOccupiedChannels", "Channels range overflows max channels address (512)"));
					}
				}				
			}
		}
	}
}

void SDMXFixturePatcher::InitDragDrop(const TArray<TWeakObjectPtr<UDMXEntity>>& DraggedEntities)
{
	if (DraggedEntities.Num() == 1)
	{
		if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(DraggedEntities[0]))
		{
			DraggedNode = FindPatchNode(FixturePatch);

			if (!DraggedNode.IsValid())
			{
				DraggedNode = FDMXFixturePatchNode::Create(DMXEditorPtr, FixturePatch);
			}

			// Remove auto assign to let drag drop set it
			if (FixturePatch->bAutoAssignAddress)
			{
				DisableAutoAssignAdress(FixturePatch);
			}

			DraggedNode->SetVisiblity(EVisibility::HitTestInvisible);

			return;
		}
	}	

	DraggedNode = nullptr;
}

FReply SDMXFixturePatcher::OnDropOntoChannel(int32 UniverseID, int32 ChannelID, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (!Operation.IsValid())
	{
		return FReply::Unhandled();
	}

	if (TSharedPtr<FDMXEntityDragDropOperation> EntityDragDropOp = DragDropEvent.GetOperationAs<FDMXEntityDragDropOperation>())
	{
		const TArray<TWeakObjectPtr<UDMXEntity>>& DraggedEntities = EntityDragDropOp->GetDraggedEntities();

		if (DraggedNode.IsValid())
		{
			const TSharedPtr<SDMXPatchedUniverse>& Universe = PatchedUniversesByID.FindChecked(UniverseID);

			DraggedNode->SetVisiblity(EVisibility::Visible);

			bool bCreateTransaction = true;
			if (Universe->Patch(DraggedNode, ChannelID, bCreateTransaction))
			{
				OnPatched.ExecuteIfBound();

				return FReply::Handled().EndDragDrop();
			}
		}
	}

	DraggedNode.Reset();
	return FReply::Unhandled();
}

TSharedRef<SWidget> SDMXFixturePatcher::CreateDragDropDecorator(TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch, int32 ChannelID) const
{
	if (FixturePatch.IsValid())
	{
		int32 StartingChannel = ChannelID;
		int32 EndingChannel = ChannelID + FixturePatch->GetChannelSpan() - 1;

		if (EndingChannel > DMX_UNIVERSE_SIZE)
		{
			StartingChannel = DMX_UNIVERSE_SIZE - FixturePatch->GetChannelSpan() + 1;
			EndingChannel = DMX_UNIVERSE_SIZE;
		}

		FText PatchName = FText::Format(LOCTEXT("PatchName", "{0}"), FText::FromString(FixturePatch->GetDisplayName()));;								
		FText ChannelRangeName = FText::Format(LOCTEXT("ChannelRangeName", "Channel {0} - {1}"), StartingChannel, EndingChannel);

		return SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("Graph.ConnectorFeedback.Border"))
			[
				SNew(SVerticalBox)				
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Fill)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
					.Text(ChannelRangeName)
				]
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Bottom)
				[
					SNew(STextBlock)
					.Text(PatchName)
					.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
					.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f))
				]
			];
	}

	return SNullWidget::NullWidget;
}

void SDMXFixturePatcher::PostUndo(bool bSuccess)
{
	RefreshFromProperties();
}

void SDMXFixturePatcher::PostRedo(bool bSuccess)
{
	RefreshFromProperties();
}

TSharedPtr<FDMXFixturePatchNode> SDMXFixturePatcher::FindPatchNode(TWeakObjectPtr<UDMXEntityFixturePatch> Patch) const
{
	if (Patch.IsValid())
	{
		TSharedPtr<FDMXFixturePatchNode> ExistingNode;
		for (const TPair<int32, TSharedPtr<SDMXPatchedUniverse>>& UniverseByID : PatchedUniversesByID)
		{
			ExistingNode = UniverseByID.Value->FindPatchNode(Patch);

			if (ExistingNode.IsValid())
			{
				return ExistingNode;
			}
		}
	}
	return nullptr;
}

TSharedPtr<FDMXFixturePatchNode> SDMXFixturePatcher::FindPatchNodeOfType(UDMXEntityFixtureType* Type, const TSharedPtr<FDMXFixturePatchNode>& IgoredNode) const
{
	if (Type)
	{
		TSharedPtr<FDMXFixturePatchNode> ExistingNode;
		for (const TPair<int32, TSharedPtr<SDMXPatchedUniverse>>& UniverseByID : PatchedUniversesByID)
		{		
			return UniverseByID.Value->FindPatchNodeOfType(Type, IgoredNode);
		}
	}
	return nullptr;
}

void SDMXFixturePatcher::SelectUniverse(int32 NewUniverseID)
{
	check(SharedData.IsValid());
	UniverseToSetNextTick = NewUniverseID;
}

int32 SDMXFixturePatcher::GetSelectedUniverse() const
{
	check(SharedData.IsValid());
	return SharedData->GetSelectedUniverse();
}

void SDMXFixturePatcher::OnFixturePatchSelectionChanged()
{
	check(SharedData.IsValid());
	const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>& SelectedPatches = SharedData->GetSelectedFixturePatches();

	// Only RefreshFromProperties if a node for a selected patch doesn't exist
	// This avoids issues when a patch gets selected and detect drag is pending.	
	for (TWeakObjectPtr<UDMXEntityFixturePatch> Patch : SelectedPatches)
	{
		if (!FindPatchNode(Patch).IsValid())
		{
			RefreshFromProperties(); 
			break;
		}
	}

	SelectUniverseThatContainsSelectedPatches();
}

void SDMXFixturePatcher::OnUniverseSelectionChanged()
{
	if (IsUniverseSelectionEnabled())
	{
		ShowSelectedUniverse();
	}
	else
	{
		// The newly selected universe is not yet shown and may contain a patch.
		// If so, show all universes anew, to include the newly selected universe.
		check(SharedData.IsValid());		
		if (!PatchedUniversesByID.Contains(SharedData->GetSelectedUniverse()))
		{
			ShowAllPatchedUniverses();
		}
	}
}

void SDMXFixturePatcher::ShowSelectedUniverse(bool bForceReconstructWidget)
{
	// Don't show universes if there's no controller
	if (!HasAnyControllers())
	{
		PatchedUniverseScrollBox->ClearChildren();
		PatchedUniversesByID.Reset();
		return;
	}

	int32 SelectedUniverseID = GetSelectedUniverse();

	// Create a new patched universe if required
	if (PatchedUniversesByID.Num() != 1 || bForceReconstructWidget)
	{
		PatchedUniverseScrollBox->ClearChildren();
		PatchedUniversesByID.Reset();

		TSharedRef<SDMXPatchedUniverse> NewPatchedUniverse =
			SNew(SDMXPatchedUniverse)
			.DMXEditor(DMXEditorPtr)
			.UniverseID(SelectedUniverseID)
			.OnDragEnterChannel(this, &SDMXFixturePatcher::OnDragEnterChannel)
			.OnDropOntoChannel(this, &SDMXFixturePatcher::OnDropOntoChannel);

		PatchedUniverseScrollBox->AddSlot()
			.Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))			
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					NewPatchedUniverse
				]
			];

		PatchedUniversesByID.Add(SelectedUniverseID, NewPatchedUniverse);
	}
	else
	{
		// Update the single, existing universe instance
		int32 OldUniverseID = -1;
		for (TPair<int32, TSharedPtr<SDMXPatchedUniverse>> UniverseByID : PatchedUniversesByID)
		{
			OldUniverseID = UniverseByID.Key;
			break;			
		}
		check(OldUniverseID != -1);
				
		TSharedPtr<SDMXPatchedUniverse> Universe = PatchedUniversesByID.FindAndRemoveChecked(OldUniverseID);
		PatchedUniversesByID.Add(SelectedUniverseID, Universe);
		Universe->SetUniverseID(SelectedUniverseID);
	}	
}

void SDMXFixturePatcher::ShowAllPatchedUniverses(bool bForceReconstructWidget)
{
	check(PatchedUniverseScrollBox.IsValid());

	// Don't show universes if there's no controller
	if (!HasAnyControllers())
	{
		PatchedUniverseScrollBox->ClearChildren();
		PatchedUniversesByID.Reset();
		return;
	}

	if (bForceReconstructWidget)
	{
		PatchedUniverseScrollBox->ClearChildren();
		PatchedUniversesByID.Reset();
	}

	UDMXLibrary* Library = GetDMXLibrary();
	if (Library)
	{
		TArray<UDMXEntityFixturePatch*> FixturePatches = Library->GetEntitiesTypeCast<UDMXEntityFixturePatch>();

		// Sort by universe ID
		FixturePatches.Sort([](const UDMXEntityFixturePatch& Patch, const UDMXEntityFixturePatch& Other) {
			return Patch.UniverseID < Other.UniverseID;
			});

		// Create widgets for all universe with patches		
		for(UDMXEntityFixturePatch* Patch : FixturePatches)
		{
			check(Patch);

			// Ignore patches that are not residing in a universe
			if (Patch->UniverseID < 0)
			{
				continue;
			}

			if (!PatchedUniversesByID.Contains(Patch->UniverseID))
			{
				AddUniverse(Patch->UniverseID);
			}
		}
				
		TMap<int32, TSharedPtr<SDMXPatchedUniverse>> CachedPatchedUniversesByID = PatchedUniversesByID;
		for (const TPair<int32, TSharedPtr<SDMXPatchedUniverse>>& UniverseByIDKvp : CachedPatchedUniversesByID)
		{
			check(UniverseByIDKvp.Value.IsValid());

			if (UniverseByIDKvp.Value->GetPatchedNodes().Num() == 0)
			{
				// Remove universe widgets without patches
				PatchedUniversesByID.Remove(UniverseByIDKvp.Key);
				PatchedUniverseScrollBox->RemoveSlot(UniverseByIDKvp.Value.ToSharedRef());
			}
			else
			{
				// Update universe widgets with patches
				UniverseByIDKvp.Value->SetUniverseID(UniverseByIDKvp.Key);
			}
		}

		// Show last patched universe +1 for convenience of adding patches to a new universe
		TArray<int32> UniverseIDs;
		PatchedUniversesByID.GetKeys(UniverseIDs);
		UniverseIDs.Sort([](int32 FirstID, int32 SecondID) {
			return FirstID > SecondID;
			});

		int32 LastPatchedUniverseID = 0;
		if (UniverseIDs.Num() > 0)
		{
			LastPatchedUniverseID = UniverseIDs[0];
		}
	
		int32 FirstEmptyUniverse = LastPatchedUniverseID + 1;
		AddUniverse(FirstEmptyUniverse);
	}
}

void SDMXFixturePatcher::AddUniverse(int32 UniverseID)
{
	TSharedRef<SDMXPatchedUniverse> PatchedUniverse =
		SNew(SDMXPatchedUniverse)
		.DMXEditor(DMXEditorPtr)
		.UniverseID(UniverseID)
		.OnDragEnterChannel(this, &SDMXFixturePatcher::OnDragEnterChannel)
		.OnDropOntoChannel(this, &SDMXFixturePatcher::OnDropOntoChannel);

	PatchedUniverseScrollBox->AddSlot()
		.Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
		[
			PatchedUniverse
		];

	PatchedUniversesByID.Add(UniverseID, PatchedUniverse);
}

void SDMXFixturePatcher::OnToggleDisplayAllUniverses(ECheckBoxState CheckboxState)
{
	bool bForceReconstructWidget = true;

	switch (ShowAllUniversesCheckBox->GetCheckedState())
	{
	case ECheckBoxState::Checked:
		ShowAllPatchedUniverses(bForceReconstructWidget);
		return;

	case ECheckBoxState::Unchecked:
		ShowSelectedUniverse(bForceReconstructWidget);
		return;

	case ECheckBoxState::Undetermined:
	default:
		checkNoEntry();
	}
}

bool SDMXFixturePatcher::IsUniverseSelectionEnabled() const
{
	check(ShowAllUniversesCheckBox.IsValid());

	switch (ShowAllUniversesCheckBox->GetCheckedState())
	{
	case ECheckBoxState::Checked:
		return false;

	case ECheckBoxState::Unchecked:
		if (HasAnyControllers())
		{
			return true;
		}
		return false;

	case ECheckBoxState::Undetermined:
	default:
		checkNoEntry();
	}

	return false;
}

bool SDMXFixturePatcher::HasAnyControllers() const
{
	UDMXLibrary* Library = GetDMXLibrary();
	if (Library)
	{
		TArray<UDMXEntityController*> Controllers = Library->GetEntitiesTypeCast<UDMXEntityController>();
		if (Controllers.Num() > 0)
		{
			return true;
		}
	}
	return false;
}

bool SDMXFixturePatcher::AreUniversesInControllersRange() const
{
	UDMXLibrary* Library = GetDMXLibrary();
	if (Library)
	{
		TArray<UDMXEntityController*> Controllers = Library->GetEntitiesTypeCast<UDMXEntityController>();
		
		TArray<int32> UniverseIDs;
		PatchedUniversesByID.GetKeys(UniverseIDs);

		for (int32 UniverseID : UniverseIDs)
		{
			int32 IdxControllerInRange =
				Controllers.IndexOfByPredicate([UniverseID](UDMXEntityController* Controller) {
					return
						UniverseID >= Controller->UniverseLocalStart &&
						UniverseID <= Controller->UniverseLocalEnd;
					});

			if (IdxControllerInRange == INDEX_NONE)
			{
				return false;
			}
		}
	}
	return true;
}

FText SDMXFixturePatcher::GetTooltipText() const
{
	if (!HasAnyControllers())
	{
		return LOCTEXT("NoControllers", "No controllers available. Please create one in the 'Controllers' tab.");
	}

	return FText::GetEmpty();
}

void SDMXFixturePatcher::DisableAutoAssignAdress(TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch)
{
	if (FixturePatch.IsValid())
	{
		const FScopedTransaction Transaction = FScopedTransaction(
			FText::Format(LOCTEXT("AutoAssignAdressChanged", "Disabled Auto Assign Adress for {0}"), 
				FText::FromString(FixturePatch->GetDisplayName()))
		);

		FixturePatch->Modify();

		FixturePatch->bAutoAssignAddress = false;
	}
}

UDMXLibrary* SDMXFixturePatcher::GetDMXLibrary() const
{
	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		return DMXEditor->GetDMXLibrary();
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
