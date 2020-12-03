// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Results/SLevelSnapshotsEditorResults.h"

#include "LevelSnapshot.h"
#include "PropertySnapshot.h"
#include "LevelSnapshotsEditorStyle.h"
#include "ILevelSnapshotsEditorView.h"
#include "LevelSnapshotsLog.h"
#include "Views/Results/LevelSnapshotsEditorResults.h"
#include "LevelSnapshotsEditorData.h"
#include "PropertyWidgetGenerator.h"

#include "DiffUtils.h"
#include "EditorStyleSet.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditor/Private/PropertyEditorHelpers.h"
#include "Types/ISlateMetaData.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

SLevelSnapshotsEditorResults::~SLevelSnapshotsEditorResults()
{
}

void SLevelSnapshotsEditorResults::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorResults>& InEditorResults, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
{
	EditorResultsPtr = InEditorResults;
	BuilderPtr = InBuilder;

	InBuilder->OnSnapshotSelected.Add(FLevelSnapshotsEditorViewBuilder::FOnSnapshotSelectedDelegate::CreateSP(this, &SLevelSnapshotsEditorResults::OnSnapshotSelected));

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5.f, 10.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(this, &SLevelSnapshotsEditorResults::SetAllGroupsCollapsed)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CollapseAll", "Collapse All"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(this, &SLevelSnapshotsEditorResults::SetAllGroupsUnselected)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("UnselectAll", "Unselect All"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(this, &SLevelSnapshotsEditorResults::SetAllGroupsSelected)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SelectAll", "Select All"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew( SComboButton )
					.ComboButtonStyle( FEditorStyle::Get(), "GenericFilters.ComboButtonStyle" )
					.ForegroundColor(FLinearColor::White)
					.ContentPadding(0)
					.ToolTipText( LOCTEXT( "AddFilterToolTip", "Result Filters" ) )
					.OnGetMenuContent( this, &SLevelSnapshotsEditorResults::MakeAddFilterMenu )
					.HasDownArrow( true )
					.ContentPadding( FMargin( 1, 0 ) )
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ContentBrowserFiltersCombo")))
					.Visibility(EVisibility::Visible)
					.ButtonContent()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
							.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2,0,0,0)
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
							.Text(LOCTEXT("Filters", "Filters"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.Padding(2.f, 0.f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.Padding(2.f, 0.f)
					.AutoWidth()
					[
						SAssignNew(ShowUnchangedCheckboxPtr, SCheckBox)
						.IsChecked(false)
						.OnCheckStateChanged(this, &SLevelSnapshotsEditorResults::OnCheckedStateChange_ShowUnchangedSnapshotActors)
					]

					+SHorizontalBox::Slot()
					.Padding(2.f, 0.f)
					.AutoWidth()
					[
						SNew(SBorder)
						.Padding(FMargin(15.0f, 5.0f))
						.BorderImage(FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.BrightBorder"))
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("LevelSnapshotsEditorResults_ShowUnchanged", "Show Unchanged"))
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			[
				SAssignNew(ResultsBoxPtr, SBox)
			]
		];
}

void SLevelSnapshotsEditorResults::OnColumnResized(const float InWidth, const int32 SlotIndex) const
{
	SyncLevelSnapshotsResultsSplittersDelegate.Broadcast(InWidth, SlotIndex);
}

void SLevelSnapshotsEditorResults::OnSnapshotSelected(ULevelSnapshot* InLevelSnapshot)
{
	if (InLevelSnapshot != nullptr)
	{
		// Saving a reference to the selected LevelSnapshot for diffing
		SelectedLevelSnapshotPtr = InLevelSnapshot;

		// Clear out all events broadcasted to for splitter sync
		SyncLevelSnapshotsResultsSplittersDelegate.Clear();

		if (ResultsBoxPtr.IsValid())
		{
			ResultsBoxPtr->SetContent(LoopSnapshotActors(InLevelSnapshot));
		}

		if (ShowUnchangedCheckboxPtr.IsValid())
		{
			SetShowUnchangedSnapshotGroups(ShowUnchangedCheckboxPtr.Get()->IsChecked());
		}
	}
}

TSharedRef<SWidget> SLevelSnapshotsEditorResults::LoopSnapshotActors(ULevelSnapshot* InLevelSnapshot)
{
	TSharedRef<SScrollBox> EncapsulatingWidget = SNew(SScrollBox);

	ActorGroups.Empty();

	for (const TPair<FString, FLevelSnapshot_Actor>& ActorSnapshotPair : InLevelSnapshot->ActorSnapshots)
	{
		FString ActorPath = ActorSnapshotPair.Key;
		FSoftObjectPath ActorSoftPath = FSoftObjectPath(ActorPath);

		TStrongObjectPtr<AActor> ActorObjectPtr = TStrongObjectPtr<AActor>(ActorSnapshotPair.Value.GetDeserializedActor());

		if (!ActorObjectPtr.IsValid())
		{
			continue; // Skip this line if ActorObject is not available
		}

		AActor* WorldCounterpartActor = nullptr;

		if (ActorSoftPath.IsValid())
		{
			WorldCounterpartActor = Cast<AActor>(ActorSoftPath.ResolveObject());
		}

		// Create group widget
		TSharedRef<SLevelSnapshotsEditorResultsActorGroup> Group = 
			SNew(SLevelSnapshotsEditorResultsActorGroup)
			.ActorPath(ActorSoftPath)
			.ActorName(ActorSnapshotPair.Value.Base.ObjectName);

		ActorGroups.Add(Group);

		for (const TPair<FName, FLevelSnapshot_Property>& PropertyPair : ActorSnapshotPair.Value.Base.Properties)
		{
			const FName PropertyName = PropertyPair.Key;
			
			FProperty* Property = FindFProperty<FProperty>(ActorObjectPtr.Get()->GetClass(), PropertyName);

			// Skip inaccessible properties
			if (!Property)
			{
				continue;
			}

			// Skip deprecated properties
			const bool bIsDeprecated = (Property->PropertyFlags & CPF_Deprecated) == 1;
			if (bIsDeprecated)
			{
				continue;
			}

			TSharedPtr<SWidget> PropertyWidget;

			if (FPropertyWidgetGenerator::IsPropertyContainer(Property))
			{
				TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup> ContainerWidget =
					GenerateResultsContainerGroupWidget(
						ActorObjectPtr.Get(), WorldCounterpartActor, Property);

				Group->DirectGroupProperties.Add(ContainerWidget);

				PropertyWidget = ContainerWidget;
			}
			else
			{
				TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty> SinglePropertyWidget =
					GenerateResultsSinglePropertyWidget(
						ActorObjectPtr.Get(), WorldCounterpartActor, Property);

				Group->DirectSingleProperties.Add(SinglePropertyWidget);

				PropertyWidget = SinglePropertyWidget;
			}

			if (PropertyWidget.IsValid())
			{
				Group.Get().AddToContents(PropertyWidget.ToSharedRef());
			}
		}

		EncapsulatingWidget.Get().AddSlot()
		[
			Group
		];
	}

	return EncapsulatingWidget;
}

TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup> SLevelSnapshotsEditorResults::GenerateResultsContainerGroupWidget(
	UObject* SnapshotObject, UObject* LevelCounterpartObject, FProperty* SnapshotProperty, const uint8 ParentIndentationDepth,
	void* SpecifiedSnapshotOuter, void* SpecifiedCounterpartOuter)
{
	if (!ensure(SnapshotObject) || !ensure(SnapshotProperty))
	{
		return nullptr;
	}

	const uint8 NewIndentationDepth = ParentIndentationDepth + 1;

	TSharedRef<SLevelSnapshotsEditorResultsContainerPropertyGroup> ContainerGroup =
		SNew(SLevelSnapshotsEditorResultsContainerPropertyGroup)
		.IndentationDepth(NewIndentationDepth)
		.ContainerName(FText::FromName(SnapshotProperty->GetFName()));


	// This is the actual struct instance data. This is for a struct within a UObject or another struct.
	void* SnapshotContainerPtr =
		SnapshotProperty->ContainerPtrToValuePtr<void>(SpecifiedSnapshotOuter ? SpecifiedSnapshotOuter : SnapshotObject);

	// Declare struct value ptr for Counterpart object, but we'll only define it if the object exists
	void* CounterpartContainerPtr =
		SnapshotProperty->ContainerPtrToValuePtr<void>(SpecifiedCounterpartOuter ? SpecifiedCounterpartOuter : LevelCounterpartObject);
	
	if (FStructProperty* StructProperty = CastField<FStructProperty>(SnapshotProperty))
	{

		for (TFieldIterator<FProperty> FieldItr(StructProperty->Struct); FieldItr; ++FieldItr)
		{
			FProperty* InnerStructProperty = *FieldItr;

			TSharedPtr<SWidget> PropertyWidget = nullptr;
			
			if (FPropertyWidgetGenerator::IsPropertyContainer(InnerStructProperty))
			{
				TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup> ContainerWidget =
					GenerateResultsContainerGroupWidget(
						SnapshotObject, LevelCounterpartObject, InnerStructProperty, NewIndentationDepth,
						SnapshotContainerPtr, CounterpartContainerPtr);

				if (ContainerWidget.IsValid())
				{
					ContainerGroup->DirectGroupProperties.Add(ContainerWidget);

					PropertyWidget = ContainerWidget;
				}
			}
			else
			{
				TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty> SinglePropertyWidget =
					GenerateResultsSinglePropertyWidget(
						SnapshotObject, LevelCounterpartObject, InnerStructProperty, 0, NewIndentationDepth,
						SnapshotContainerPtr, CounterpartContainerPtr);

				if (SinglePropertyWidget.IsValid())
				{					
					ContainerGroup->DirectSingleProperties.Add(SinglePropertyWidget);

					PropertyWidget = SinglePropertyWidget;
				}
			}

			if (PropertyWidget.IsValid())
			{
				ContainerGroup->AddToContents(PropertyWidget.ToSharedRef());
			}
		}

		return ContainerGroup;
	}
	else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(SnapshotProperty))
	{
		FProperty* ArrayInnerProperty = ArrayProperty->Inner;

		const int32 NumberOfArrayMembers = ArrayProperty->ArrayDim;
		for (int32 ArrayIndex = 0; ArrayIndex < NumberOfArrayMembers; ArrayIndex++)
		{
			TSharedPtr<SWidget> PropertyWidget = nullptr;
			
			if (FPropertyWidgetGenerator::IsPropertyContainer(ArrayInnerProperty))
			{
				TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup> ContainerWidget =
					GenerateResultsContainerGroupWidget(
						SnapshotObject, LevelCounterpartObject, ArrayInnerProperty, NewIndentationDepth,
						SnapshotContainerPtr, CounterpartContainerPtr);

				if (ContainerWidget.IsValid())
				{
					ContainerGroup->DirectGroupProperties.Add(ContainerWidget);

					PropertyWidget = ContainerWidget;
				}
			}
			else
			{
				TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty> SinglePropertyWidget =
					GenerateResultsSinglePropertyWidget(SnapshotObject, LevelCounterpartObject, ArrayInnerProperty, ArrayIndex,
						NewIndentationDepth, SnapshotContainerPtr, CounterpartContainerPtr);

				if (SinglePropertyWidget.IsValid())
				{
					ContainerGroup->DirectSingleProperties.Add(SinglePropertyWidget);

					PropertyWidget = SinglePropertyWidget;
				}
			}

			if (PropertyWidget.IsValid())
			{
				ContainerGroup->AddToContents(PropertyWidget.ToSharedRef());
			}
		}

		return ContainerGroup;
	}
	else if (FSetProperty* SetProperty = CastField<FSetProperty>(SnapshotProperty))
	{
		return  nullptr;
	}
	else if (FMapProperty* MapProperty = CastField<FMapProperty>(SnapshotProperty))
	{
		return nullptr;
	}

	return nullptr;
}

TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty> SLevelSnapshotsEditorResults::GenerateResultsSinglePropertyWidget(
	UObject* SnapshotObject, UObject* LevelCounterpartObject, FProperty* SnapshotProperty, const int32 ArrayDimIndex, 
	const uint8 ParentIndentationDepth, void* SpecifiedSnapshotOuter, void* SpecifiedCounterpartOuter)
{
	if (!ensure(SnapshotObject) || !ensure(SnapshotProperty))
	{
		return nullptr;
	}

	// If this property is within a struct, map, array, set, etc.
	bool bIsPropertyInContainer = SpecifiedSnapshotOuter != nullptr;
	
	TSharedPtr<SWidget> SnapshotActorWidget = nullptr;

	if (bIsPropertyInContainer)
	{
		SnapshotActorWidget = FPropertyWidgetGenerator::GenerateWidgetForPropertyInContainer(
			SnapshotProperty, SnapshotObject, SpecifiedSnapshotOuter, ArrayDimIndex);
	}
	else
	{
		SnapshotActorWidget = FPropertyWidgetGenerator::GenerateWidgetForUClassProperty(SnapshotProperty, SnapshotObject);
	}

	if (SnapshotActorWidget.IsValid())
	{
		const uint8 NewIndentationDepth = ParentIndentationDepth + 1;
		
		// Set disabled to avoid user changes
		SnapshotActorWidget.Get()->SetEnabled(false);

		// Declare a widget container for the counterpart actor and a bool that describes whether or not the values are different
		// between the snapshot actor and the counterpart actor
		TSharedPtr<SWidget> CounterpartActorWidget = nullptr;
		bool bIsPropertyDifferentInLevel = false;

		if (LevelCounterpartObject)
		{
			bIsPropertyInContainer = SpecifiedCounterpartOuter != nullptr;
			// We don't need to create this widget if the SnapshotActorWidget isn't valid
			if (bIsPropertyInContainer)
			{
				CounterpartActorWidget = FPropertyWidgetGenerator::GenerateWidgetForPropertyInContainer(
					SnapshotProperty, LevelCounterpartObject, SpecifiedCounterpartOuter, ArrayDimIndex);
			}
			else
			{
				CounterpartActorWidget = FPropertyWidgetGenerator::GenerateWidgetForUClassProperty(SnapshotProperty, LevelCounterpartObject);					
			}

			if (CounterpartActorWidget.IsValid())
			{
				// Set disabled to avoid user changes
				CounterpartActorWidget.Get()->SetEnabled(false);

				// We collected the values as string to be able to compare them here
				// If the strings don't match up, we know the values are different
				if (bIsPropertyInContainer)
				{
					bIsPropertyDifferentInLevel =
						!FPropertyWidgetGenerator::ArePropertyValuesInContainerEqual(
							SnapshotProperty, SpecifiedSnapshotOuter, SpecifiedCounterpartOuter, ArrayDimIndex);
				}
				else
				{
					bIsPropertyDifferentInLevel =
						!FPropertyWidgetGenerator::AreUClassPropertyValuesEqual(
							SnapshotProperty, SnapshotObject,LevelCounterpartObject);
				}
			}
		}

		return SNew(SLevelSnapshotsEditorResultsSingleProperty,
			SharedThis(this), SnapshotProperty, SnapshotActorWidget, CounterpartActorWidget)
			.bIsPropertyDifferentInLevel(bIsPropertyDifferentInLevel)
			.IndentationDepth(NewIndentationDepth);
		
	}

	return nullptr;
}

FReply SLevelSnapshotsEditorResults::SetAllGroupsSelected()
{
	for (TSharedPtr<SLevelSnapshotsEditorResultsActorGroup>& Group : ActorGroups)
	{
		if (Group.IsValid())
		{
			Group.Get()->SetIsNodeChecked(true);
		}
	}

	return FReply::Handled();
}

FReply SLevelSnapshotsEditorResults::SetAllGroupsUnselected()
{
	for (TSharedPtr<SLevelSnapshotsEditorResultsActorGroup>& Group : ActorGroups)
	{
		if (Group.IsValid())
		{
			Group.Get()->SetIsNodeChecked(false);
		}
	}

	return FReply::Handled();
}

FReply SLevelSnapshotsEditorResults::SetAllGroupsCollapsed()
{
	for (TSharedPtr<SLevelSnapshotsEditorResultsActorGroup>& Group : ActorGroups)
	{
		if (Group.IsValid())
		{
			Group.Get()->SetIsItemExpanded(false);
		}
	}

	return FReply::Handled();
}

void SLevelSnapshotsEditorResults::OnCheckedStateChange_ShowUnchangedSnapshotActors(ECheckBoxState NewState)
{
	if (NewState == ECheckBoxState::Checked)
	{
		SetShowUnchangedSnapshotGroups(true);
	}
	else
	{
		SetShowUnchangedSnapshotGroups(false);
	}
}

void SLevelSnapshotsEditorResults::SetShowUnchangedSnapshotGroups(const bool bShowGroups)
{
	const EVisibility NewVisibility = bShowGroups ? EVisibility::Visible : EVisibility::Collapsed;
	
	for (TSharedPtr<SLevelSnapshotsEditorResultsActorGroup>& ActorGroup : ActorGroups)
	{
		if (ActorGroup.IsValid())
		{
			// Get all single property widgets for this actor group
			TArray<TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty>> SingleProperties;
			ActorGroup.Get()->GetAllSinglePropertyChildrenRecursively(SingleProperties);

			for (TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty>& Row : SingleProperties)
			{
				if (Row.IsValid())
				{
					if (!Row.Get()->GetIsPropertyChangedInLevel())
					{
						Row.Get()->SetVisibility(NewVisibility);
					}
				}
			}

			ActorGroup.Get()->SetAllChildContainerGroupsVisibilityBasedOnTheirChildren();
		}
	}
}

TSharedRef<SWidget> SLevelSnapshotsEditorResults::MakeAddFilterMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, nullptr, /*bCloseSelfOnly=*/true);

	MenuBuilder.BeginSection("BasicFilters");
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Filter1", "Filter1"),
			LOCTEXT("FilterTooltipPrefix", "FilterTooltipPrefix"),
			FSlateIcon(),
			FUIAction()
			);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Filter2", "Filter2"),
			LOCTEXT("FilterTooltipPrefix", "FilterTooltipPrefix"),
			FSlateIcon(),
			FUIAction()
			);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SLevelSnapshotsEditorResultsExpanderArrow::Construct(const FArguments& InArgs)
{
	OnArrowClickedDelegate = InArgs._OnArrowClickedDelegate;
	
	ChildSlot
	[
		SAssignNew(ExpanderArrowPtr, SButton)
		.ButtonStyle(FCoreStyle::Get(), "NoBorder")
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.ClickMethod(EButtonClickMethod::MouseDown)
		.ForegroundColor(FSlateColor::UseForeground())
		.IsFocusable(false)
		.OnClicked_Lambda([this]()
		{
			ToggleItemExpanded();
			
			OnArrowClickedDelegate.ExecuteIfBound(IsItemExpanded());

			return FReply::Handled();
		})
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image_Lambda([this]()
			{
				// Copied from SExpanderArrow::GetExpanderImage
				// Recreated SExpanderArrow because there is no exposed delegate for clicking the arrow button on SExpanderArrow
				FName ResourceName;
				if (bIsItemExpanded)
				{
					if (this->IsHovered())
					{
						ResourceName = "TreeArrow_Expanded_Hovered";
					}
					else
					{
						ResourceName = "TreeArrow_Expanded";
					}
				}
				else
				{
					if (this->IsHovered())
					{
						ResourceName = "TreeArrow_Collapsed_Hovered";
					}
					else
					{
						ResourceName = "TreeArrow_Collapsed";
					}
				}

				return FCoreStyle::Get().GetBrush(ResourceName);
			})
		]
	];
}

bool SLevelSnapshotsEditorResultsExpanderArrow::IsItemExpanded() const
{
	return bIsItemExpanded;
}

void SLevelSnapshotsEditorResultsExpanderArrow::SetItemExpanded(bool bNewExpanded)
{
	bIsItemExpanded = bNewExpanded;

	OnArrowClickedDelegate.ExecuteIfBound(bNewExpanded);
}

void SLevelSnapshotsEditorResultsExpanderArrow::ToggleItemExpanded()
{
	SetItemExpanded(!IsItemExpanded());
}

FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType FLevelSnapshotsEditorResultsRow::GetType() const
{
	return NodeType;
}

ECheckBoxState FLevelSnapshotsEditorResultsRow::GetIsNodeChecked()
{
	ECheckBoxState State = ECheckBoxState::Undetermined;

	if (CheckboxPtr.IsValid())
	{
		State = CheckboxPtr.Get()->GetCheckedState();
	}

	return State;
}

void FLevelSnapshotsEditorResultsRow::SetIsNodeChecked(bool bNewChecked)
{
	if (CheckboxPtr.IsValid())
	{
		CheckboxPtr.Get()->SetIsChecked(bNewChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	}
}

bool FLevelSnapshotsEditorResultsRow::GetIsItemExpanded()
{
	bool bIsExpanded = false;

	if (ExpanderArrowPtr.IsValid())
	{
		bIsExpanded = ExpanderArrowPtr.Get()->IsItemExpanded();
	}

	return bIsExpanded;
}

void FLevelSnapshotsEditorResultsRow::SetIsItemExpanded(bool bNewExpanded)
{
	if (ExpanderArrowPtr.IsValid())
	{
		ExpanderArrowPtr.Get()->SetItemExpanded(bNewExpanded);
	}
}

uint8 FLevelSnapshotsEditorResultsRow::GetIndentationDepth()
{
	return IndentationDepth;
}

void FLevelSnapshotsEditorResultsRow::SetIndentationDepth(const uint8 InDepth)
{
	IndentationDepth = InDepth;
}

float FLevelSnapshotsEditorResultsRow::GetNodeIndentationWidth() const
{
	return ChildIndentationWidth * IndentationDepth;
}

bool FLevelSnapshotsEditorResultsRow::HasVisibleChildren()
{
	TArray<TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty>> OutSinglePropertyNodeArray;
	GetAllSinglePropertyChildrenRecursively(OutSinglePropertyNodeArray);

	for (TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty>& Child : OutSinglePropertyNodeArray)
	{
		if (Child.IsValid() && Child.Get()->GetVisibility() != EVisibility::Collapsed && Child.Get()->GetVisibility() != EVisibility::Hidden)
		{
			return true;
		}
	}

	return false;
}

void FLevelSnapshotsEditorResultsRow::SetAllChildContainerGroupsVisibilityBasedOnTheirChildren()
{
	TArray<TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup>> Containers;
	GetAllContainerGroupChildrenRecursively(Containers);

	// Set visibility for each struct, iterating in reverse
	for (int32 ContainerCount = Containers.Num() - 1; ContainerCount >= 0; ContainerCount--)
	{
		TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup> Container = Containers[ContainerCount];

		if (Container.IsValid())
		{
			Container.Get()->SetVisibility(
				Container.Get()->HasVisibleChildren() ? EVisibility::Visible : EVisibility::Collapsed);
		}
	}
}

void FLevelSnapshotsEditorResultsRow::GetAllSinglePropertyChildrenRecursively(
	TArray<TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty>>& OutSinglePropertyNodeArray)
{
	OutSinglePropertyNodeArray.Append(DirectSingleProperties);

	for (TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup>& Group : DirectGroupProperties)
	{
		if (!Group.IsValid())
		{
			continue;
		}

		Group.Get()->GetAllSinglePropertyChildrenRecursively(OutSinglePropertyNodeArray);
	}
}

void FLevelSnapshotsEditorResultsRow::GetAllContainerGroupChildrenRecursively(
	TArray<TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup>>& OutContainerGroupNodeArray)
{
	for (TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup>& Group : DirectGroupProperties)
	{
		if (!Group.IsValid())
		{
			continue;
		}

		OutContainerGroupNodeArray.Add(Group);

		Group.Get()->GetAllContainerGroupChildrenRecursively(OutContainerGroupNodeArray);
	}
}

void FLevelSnapshotsEditorResultsRow::GetAllChildRowsRecursively(
	TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>>& OutRowNodeArray)
{
	OutRowNodeArray.Append(DirectSingleProperties);

	for (TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup>& Group : DirectGroupProperties)
	{
		if (!Group.IsValid())
		{
			continue;
		}

		OutRowNodeArray.Add(Group);

		Group.Get()->GetAllChildRowsRecursively(OutRowNodeArray);
	}
}

void SLevelSnapshotsEditorResultsActorGroup::Construct(const FArguments& InArgs)
{
	ActorPath = InArgs._ActorPath;
	ActorName = InArgs._ActorName;

	check(ActorPath.IsSet());
	check(ActorName.IsSet());

	NodeType = ELevelSnapshotsEditorResultsRowType::ActorGroup;
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f))
		.VAlign(VAlign_Top)
		.AutoHeight()
		[
			SNew(SBorder)
			.Padding(FMargin(5.0f, 8.0f))
			.BorderImage(FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.GroupBorder"))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Center)
				.Padding(FMargin(4.0f, 0.0f))
				.AutoWidth()
				[
					SAssignNew(ExpanderArrowPtr, SLevelSnapshotsEditorResultsExpanderArrow)
					.OnArrowClickedDelegate_Lambda([this](bool bIsExpanded)
					{
						if (ScrollBoxPtr.IsValid())
						{
							const EVisibility Visibility = bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
							ScrollBoxPtr.Get()->SetVisibility(Visibility);
						}
					})
				]

				+ SHorizontalBox::Slot()
				.Padding(FMargin(4.0f, 0.0f))
				.AutoWidth()
				[
					SAssignNew(CheckboxPtr, SCheckBox)
					.IsChecked(true)
					.OnCheckStateChanged_Raw(this, &SLevelSnapshotsEditorResultsActorGroup::OnNodeCheckStateChanged)
				]

				+ SHorizontalBox::Slot()
				.Padding(FMargin(10.0f, 0.0f))
				[
					SNew(STextBlock).Text(FText::FromName(ActorName.Get()))
				]
			]
		]

		+SVerticalBox::Slot()
		.Padding(FMargin(15.0f, 2.0f))
		.VAlign(VAlign_Top)
		.MaxHeight(500.0f)
		[
			SAssignNew(ScrollBoxPtr,SScrollBox)
			.Visibility(EVisibility::Collapsed)
		]
	];
}

void SLevelSnapshotsEditorResultsActorGroup::AddToContents(TSharedRef<SWidget> InContent) const
{
	if (ensure(ScrollBoxPtr.IsValid()))
	{
		ScrollBoxPtr.Get()->AddSlot()
			[
				InContent
			];
	}
}

void SLevelSnapshotsEditorResultsContainerPropertyGroup::Construct(const FArguments& InArgs)
{
	ContainerName = InArgs._ContainerName;
	IndentationDepth = InArgs._IndentationDepth;

	NodeType = ELevelSnapshotsEditorResultsRowType::ContainerGroup;
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f))
		.VAlign(VAlign_Top)
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SNew(SSpacer).Size(FVector2D(GetNodeIndentationWidth(), 1.0f)) // For indenting
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(CheckboxPtr, SCheckBox)
				.IsChecked(true)
				.OnCheckStateChanged_Raw(this, &SLevelSnapshotsEditorResultsContainerPropertyGroup::OnNodeCheckStateChanged)
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.AutoWidth()
			[
				SAssignNew(ExpanderArrowPtr, SLevelSnapshotsEditorResultsExpanderArrow)
				.OnArrowClickedDelegate_Lambda([this](bool bIsExpanded)
				{
					if (ContentVBoxPtr.IsValid())
					{
						const EVisibility Visibility = bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
						ContentVBoxPtr.Get()->SetVisibility(Visibility);
					}
				})
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 5.0f)
			[
				SNew(STextBlock).Text(ContainerName)
			]
		]

		+SVerticalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f))
		.VAlign(VAlign_Top)
		.AutoHeight()
		[
			SAssignNew(ContentVBoxPtr,SVerticalBox)
			.Visibility(EVisibility::Collapsed)
		]
	];
}

void SLevelSnapshotsEditorResultsContainerPropertyGroup::AddToContents(TSharedRef<SWidget> InContent) const
{
	if (ensure(ContentVBoxPtr.IsValid()))
	{
		ContentVBoxPtr.Get()->AddSlot().AutoHeight()
		[
			InContent
		];
	}
}

void SLevelSnapshotsEditorResultsContainerPropertyGroup::OnNodeCheckStateChanged(const ECheckBoxState NewCheckState)
{
	bool bIsChecked = false;

	if (NewCheckState == ECheckBoxState::Checked)
	{
		bIsChecked = true;
	}
	
	TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>> ChildProperties;
	GetAllChildRowsRecursively(ChildProperties);

	for (TSharedPtr<FLevelSnapshotsEditorResultsRow>& Child : ChildProperties)
	{
		if (Child.IsValid())
		{
			Child.Get()->SetIsNodeChecked(bIsChecked);
		}
	}
	
}

void SLevelSnapshotsEditorResultsSingleProperty::Construct(const FArguments& InArgs, TSharedRef<SLevelSnapshotsEditorResults> ResultsWidget,
                                                           const FProperty* InProperty, const TSharedPtr<SWidget>& SnapshotActorWidget, const TSharedPtr<SWidget>& CurrentLevelActorWidget)
{
	bIsPropertyChangedInLevel = InArgs._bIsPropertyDifferentInLevel;
	IndentationDepth = InArgs._IndentationDepth;
	
	PropertyName = InProperty->GetFName();

	ChildSlot
	[
		SAssignNew(SplitterPtr, SSplitter)
		.Style(FEditorStyle::Get(), "DetailsView.Splitter")
		.PhysicalSplitterHandleSize(1.0f)
		.HitDetectionSplitterHandleSize(5.0f)

		+SSplitter::Slot()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SNew(SSpacer).Size(FVector2D(GetNodeIndentationWidth(), 1.0f)) // For indenting
			]
			
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SAssignNew(CheckboxPtr, SCheckBox)
				.IsChecked(true)
				.OnCheckStateChanged_Raw(this, &SLevelSnapshotsEditorResultsSingleProperty::OnNodeCheckStateChanged)
				.ToolTipText(FText::Format(INVTEXT("Is property different in current level: {0}"), bIsPropertyChangedInLevel))
			]
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(2.0f, 5.0f)
			[
				SNew(STextBlock)
				.Text(InProperty->GetDisplayNameText())
				.ToolTipText(FText::Format(INVTEXT("Is property different in current level: {0}"), bIsPropertyChangedInLevel))
			]
		]
	];

	if (SnapshotActorWidget.IsValid())
	{
		SplitterPtr.Get()->AddSlot()
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				.Padding(FMargin(2.0f, 5.0f))
				[
					SnapshotActorWidget.Get()->AsShared()
				]
			];
	}
	else
	{
		SplitterPtr.Get()->AddSlot()
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				.Padding(FMargin(2.0f, 5.0f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SLevelSnapshotsEditorResultsSingleProperty_NoValidSnapshotActor", "No valid snapshot actor given"))
				]			
			];
	}

	if (CurrentLevelActorWidget.IsValid())
	{
		SplitterPtr.Get()->AddSlot()
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				.Padding(FMargin(2.0f, 5.0f))
				[
					CurrentLevelActorWidget.Get()->AsShared()
				]
			];
	}
	else
	{
		SplitterPtr.Get()->AddSlot()
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				.Padding(FMargin(2.0f, 5.0f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SLevelSnapshotsEditorResultsSingleProperty_NoCounterpartFound", "No level property found"))
				]
			];
	}
	
	// Bind event to the splitter being resized first
	for (int32 SplitterSlotCount = 0; SplitterSlotCount < SplitterPtr.Get()->GetChildren()->Num(); SplitterSlotCount++)
	{
		if (ensure(SplitterPtr.IsValid()))
		{
			SplitterPtr.Get()->SlotAt(SplitterSlotCount)
				.OnSlotResized_Handler.BindSP(ResultsWidget, &SLevelSnapshotsEditorResults::OnColumnResized, SplitterSlotCount);
		}
	}

	// Then that event broadcasts this event
	ResultsWidget.Get().SyncLevelSnapshotsResultsSplittersDelegate.AddRaw(this, &SLevelSnapshotsEditorResultsSingleProperty::SyncSplitter);
}

void SLevelSnapshotsEditorResultsSingleProperty::SyncSplitter(const float InWidth, const int32 SlotIndex) const
{
	if (ensure(SplitterPtr.IsValid()))
	{
		SplitterPtr.Get()->SlotAt(SlotIndex).SizeValue = InWidth;
	}
}

#undef LOCTEXT_NAMESPACE
