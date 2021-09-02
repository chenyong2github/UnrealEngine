// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRigSpacePickerWidget.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "SRigHierarchyTreeView.h"
#include "ControlRigEditorStyle.h"
#include "PropertyCustomizationHelpers.h"

#define LOCTEXT_NAMESPACE "SRigSpacePickerWidget"

//////////////////////////////////////////////////////////////
/// SRigSpacePickerWidget
///////////////////////////////////////////////////////////

FRigElementKey SRigSpacePickerWidget::InValidKey;

void SRigSpacePickerWidget::Construct(const FArguments& InArgs)
{
	bShowDefaultSpaces = InArgs._ShowDefaultSpaces;
	bShowFavoriteSpaces = InArgs._ShowFavoriteSpaces;
	bShowAdditionalSpaces = InArgs._ShowAdditionalSpaces;
	bAllowReorder = InArgs._AllowReorder;
	bAllowDelete = InArgs._AllowDelete;
	bAllowAdd = InArgs._AllowAdd;
	bShowBakeButton = InArgs._ShowBakeButton;
	GetAdditionalSpacesDelegate = InArgs._GetAdditionalSpacesDelegate;
	bRepopulateRequired = false;
	bLaunchingContextMenu = false;

	if(!GetAdditionalSpacesDelegate.IsBound())
	{
		GetAdditionalSpacesDelegate = FRigSpacePickerGetAdditionalSpaces::CreateRaw(this, &SRigSpacePickerWidget::GetCurrentParents);
	}

	Hierarchy = nullptr;
	ControlKey = DefaultParentKey = WorldSocketKey = FRigElementKey();
	Customization = nullptr;

	ChildSlot
	[
		SNew(SBorder)
		.Visibility(EVisibility::Visible)
		.BorderImage(InArgs._BackgroundBrush)
		[
			SAssignNew(TopLevelListBox, SVerticalBox)
		]
	];

	if(!InArgs._Title.IsEmpty())
	{
		TopLevelListBox->AddSlot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		.Padding(4.0, 0.0, 4.0, 12.0)
		[
			SNew( STextBlock )
			.Text( InArgs._Title )
			.Font( IDetailLayoutBuilder::GetDetailFontBold() )
		];
	}

	if(InArgs._ShowDefaultSpaces)
	{
		AddSpacePickerRow(
			TopLevelListBox,
			ESpacePickerType_Parent,
			DefaultParentKey,
			FEditorStyle::GetBrush("EditorViewport.RelativeCoordinateSystem_Local"),
			LOCTEXT("Parent", "Parent"),
			FOnClicked::CreateSP(this, &SRigSpacePickerWidget::HandleParentSpaceClicked)
		);
		
		AddSpacePickerRow(
			TopLevelListBox,
			ESpacePickerType_World,
			WorldSocketKey,
			FEditorStyle::GetBrush("EditorViewport.RelativeCoordinateSystem_World"),
			LOCTEXT("World", "World"),
			FOnClicked::CreateSP(this, &SRigSpacePickerWidget::HandleWorldSpaceClicked)
		);
	}

	TopLevelListBox->AddSlot()
	.AutoHeight()
	.VAlign(VAlign_Top)
	.HAlign(HAlign_Fill)
	.Padding(0.0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Fill)
		.Padding(0)
		[
			SAssignNew(ItemSpacesListBox, SVerticalBox)
		]
	];

	if(bAllowAdd || bShowBakeButton)
	{
		TopLevelListBox->AddSlot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Fill)
		.Padding(11.f, 8.f, 4.f, 4.f)
		[
			SAssignNew(BottomButtonsListBox, SHorizontalBox)
		];

		if(bAllowAdd)
		{
			BottomButtonsListBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0.f)
			[
				SNew(SButton)
				.ContentPadding(0.0f)
				.ButtonStyle(FEditorStyle::Get(), "NoBorder")
				.OnClicked(this, &SRigSpacePickerWidget::HandleAddElementClicked)
				.Cursor(EMouseCursor::Default)
				.ToolTipText(LOCTEXT("AddSpace", "Add Space"))
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush(TEXT("Icons.PlusCircle")))
				]
			];
		}

		BottomButtonsListBox->AddSlot()
		.FillWidth(1.f)
		.HAlign(HAlign_Fill)
		[
			SNew(SSpacer)
		];

		if(bShowBakeButton)
		{
			BottomButtonsListBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.Padding(0.f)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("BakeButton", "Bake"))
				.OnClicked(InArgs._OnBakeButtonClicked)
				.ToolTipText(LOCTEXT("BakeButtonToolTip", "Allows to bake the animation of one or more controls to a single space."))
			];
		}
	}

	SetControl(InArgs._Hierarchy, InArgs._Control, InArgs._Customization);
	SetCanTick(true);
	
}

SRigSpacePickerWidget::~SRigSpacePickerWidget()
{
	if(HierarchyModifiedHandle.IsValid())
	{
		if(Hierarchy)
		{
			Hierarchy->OnModified().Remove(HierarchyModifiedHandle);
			HierarchyModifiedHandle.Reset();
		}
	}
}

void SRigSpacePickerWidget::SetControl(URigHierarchy* InHierarchy, const FRigElementKey& InControl,
	FRigControlElementCustomization* InCustomization)
{
	if(!InControl.IsValid() || InControl.Type != ERigElementType::Control)
	{
		return;
	}

	check(InHierarchy);

	if(Hierarchy && Hierarchy != InHierarchy)
	{
		if(HierarchyModifiedHandle.IsValid())
		{
			Hierarchy->OnModified().Remove(HierarchyModifiedHandle);
			HierarchyModifiedHandle.Reset();
		}
	}
	
	Hierarchy = InHierarchy;
	ControlKey = InControl;
	DefaultParentKey = Hierarchy->GetFirstParent(ControlKey);
	WorldSocketKey = Hierarchy->GetWorldSpaceSocketKey();
	Customization = InCustomization;

	HierarchyModifiedHandle = Hierarchy->OnModified().AddSP(this, &SRigSpacePickerWidget::OnHierarchyModified);

	// if the customization is not provided
	if(Customization == nullptr)
	{
		if(FRigControlElement* ControlElement = Hierarchy->Find<FRigControlElement>(ControlKey))
		{
			Customization = &ControlElement->Settings.Customization;
		}
	}

	UpdateActiveSpace();
	RepopulateItemSpaces();
}

class SRigSpaceDialogWindow : public SWindow
{
public:

	virtual bool OnIsActiveChanged(const FWindowActivateEvent& ActivateEvent) override
	{
		if(ActivateEvent.GetActivationType() == FWindowActivateEvent::EA_Deactivate)
		{
			DeactivatedDelegate.ExecuteIfBound();
		}
		return SWindow::OnIsActiveChanged(ActivateEvent);
	}

	FSimpleDelegate& OnDeactivated() { return DeactivatedDelegate; }

private:

	FSimpleDelegate DeactivatedDelegate;
}; 

FReply SRigSpacePickerWidget::OpenDialog(bool bModal)
{
	check(!DialogWindow.IsValid());
		
	const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();

	TSharedRef<SRigSpaceDialogWindow> Window = SNew(SRigSpaceDialogWindow)
	.Title( LOCTEXT("SRigSpacePickerWidgetPickSpace", "Pick a new space") )
	.CreateTitleBar(false)
	.Type(EWindowType::Menu)
	.SizingRule( ESizingRule::Autosized )
	.ScreenPosition(CursorPos)
	.FocusWhenFirstShown(true)
	.ActivationPolicy(EWindowActivationPolicy::FirstShown)
	[
		AsShared()
	];
	
	Window->SetWidgetToFocusOnActivate(AsShared());
	Window->OnDeactivated().BindSP(this, &SRigSpacePickerWidget::CloseDialog);
	
	DialogWindow = Window;

	Window->MoveWindowTo(CursorPos);

	if(bModal)
	{
		GEditor->EditorAddModalWindow(Window);
	}
	else
	{
		FSlateApplication::Get().AddWindow( Window );
	}

	return FReply::Handled();
}

void SRigSpacePickerWidget::CloseDialog()
{
	if(bLaunchingContextMenu)
	{
		return;
	}
	
	if(ContextMenu.IsValid())
	{
		return;
	}
	
	if ( DialogWindow.IsValid() )
	{
		DialogWindow.Pin()->RequestDestroyWindow();
		DialogWindow.Reset();
	}
}

FReply SRigSpacePickerWidget::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if(DialogWindow.IsValid())
		{
			CloseDialog();
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

bool SRigSpacePickerWidget::SupportsKeyboardFocus() const
{
	return true;
}

void SRigSpacePickerWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if(bRepopulateRequired)
	{
		UpdateActiveSpace();
		RepopulateItemSpaces();
		bRepopulateRequired = false;
	}
	else if(GetAdditionalSpacesDelegate.IsBound())
	{
		const TArray<FRigElementKey> CurrentAdditionalSpaces = GetAdditionalSpacesDelegate.Execute(Hierarchy, ControlKey);
		if(CurrentAdditionalSpaces != AdditionalSpaces)
		{
			RepopulateItemSpaces();
		}
	}
}

const FRigElementKey& SRigSpacePickerWidget::GetActiveSpace() const
{
	return ActiveSpace;
}

TArray<FRigElementKey> SRigSpacePickerWidget::GetDefaultSpaces() const
{
	TArray<FRigElementKey> DefaultSpaces;
	DefaultSpaces.Add(DefaultParentKey);
	DefaultSpaces.Add(WorldSocketKey);
	return DefaultSpaces;
}

TArray<FRigElementKey> SRigSpacePickerWidget::GetSpaceList(bool bIncludeDefaultSpaces) const
{
	if(bIncludeDefaultSpaces && bShowDefaultSpaces)
	{
		TArray<FRigElementKey> Spaces;
		Spaces.Append(GetDefaultSpaces());
		Spaces.Append(CurrentSpaceKeys);
		return Spaces;
	}
	return CurrentSpaceKeys;
}

void SRigSpacePickerWidget::AddSpacePickerRow(
	TSharedPtr<SVerticalBox> InListBox,
	ESpacePickerType InType,
	const FRigElementKey& InKey,
	const FSlateBrush* InBush,
	const FText& InTitle,
    FOnClicked OnClickedDelegate)
{
	static const FSlateBrush* RoundedBoxBrush = FControlRigEditorStyle::Get().GetBrush(TEXT("ControlRig.SpacePicker.RoundedRect"));

	TSharedPtr<SHorizontalBox> RowBox, ButtonBox;
	InListBox->AddSlot()
	.AutoHeight()
	.VAlign(VAlign_Top)
	.HAlign(HAlign_Fill)
	.Padding(4.0, 0.0, 4.0, 0.0)
	[
		SNew( SButton )
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(0.0))
		.OnClicked(OnClickedDelegate)
		[
			SAssignNew(RowBox, SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			.Padding(0)
			[
				SNew(SBorder)
				.Padding(FMargin(5.0, 2.0, 5.0, 2.0))
				.BorderImage(RoundedBoxBrush)
				.BorderBackgroundColor(this, &SRigSpacePickerWidget::GetButtonColor, InType, InKey)
				.Content()
				[
					SAssignNew(ButtonBox, SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					.Padding(0)
					[
						SNew(SImage)
						.Image(InBush)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					.Padding(0)
					[
						SNew( STextBlock )
						.Text( InTitle )
						.Font( IDetailLayoutBuilder::GetDetailFont() )
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SSpacer)
					]
				]
			]
		]
	];

	if(!IsDefaultSpace(InKey))
	{
		if(bAllowDelete || bAllowReorder)
		{
			RowBox->AddSlot()
			.FillWidth(1.f)
			[
				SNew(SSpacer)
			];
		}
		
		if(bAllowReorder)
		{
			RowBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ContentPadding(0)
				.OnClicked(this, &SRigSpacePickerWidget::HandleSpaceMoveUp, InKey)
				.IsEnabled(this, &SRigSpacePickerWidget::IsSpaceMoveUpEnabled, InKey)
				.ToolTipText(LOCTEXT("MoveSpaceDown", "Move this space down in the list."))
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Icons.ChevronUp"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];

			RowBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ContentPadding(0)
				.OnClicked(this, &SRigSpacePickerWidget::HandleSpaceMoveDown, InKey)
				.IsEnabled(this, &SRigSpacePickerWidget::IsSpaceMoveDownEnabled, InKey)
				.ToolTipText(LOCTEXT("MoveSpaceUp", "Move this space up in the list."))
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Icons.ChevronDown"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}

		if(bAllowDelete)
		{
			RowBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0)
			[
				PropertyCustomizationHelpers::MakeClearButton(FSimpleDelegate::CreateSP(this, &SRigSpacePickerWidget::HandleSpaceDelete, InKey), LOCTEXT("DeleteSpace", "Remove this space."), true)
			];
		}
	}
}

FReply SRigSpacePickerWidget::HandleParentSpaceClicked()
{
	return HandleElementSpaceClicked(DefaultParentKey);
}

FReply SRigSpacePickerWidget::HandleWorldSpaceClicked()
{
	return HandleElementSpaceClicked(WorldSocketKey);
}

FReply SRigSpacePickerWidget::HandleElementSpaceClicked(FRigElementKey InKey)
{
	ActiveSpaceChangedEvent.Broadcast(Hierarchy, ControlKey, InKey);

	if(DialogWindow.IsValid())
	{
		CloseDialog();
	}
	
	return FReply::Handled();
}

FReply SRigSpacePickerWidget::HandleSpaceMoveUp(FRigElementKey InKey)
{
	if(CurrentSpaceKeys.Num() > 1)
	{
		const int32 Index = CurrentSpaceKeys.Find(InKey);
		if(CurrentSpaceKeys.IsValidIndex(Index))
		{
			if(Index > 0)
			{
				TArray<FRigElementKey> ChangedSpaceKeys = CurrentSpaceKeys;
				ChangedSpaceKeys.Swap(Index, Index - 1);
				SpaceListChangedEvent.Broadcast(Hierarchy, ControlKey, ChangedSpaceKeys);
				return FReply::Handled();
			}
		}
	}
	return FReply::Unhandled();
}

FReply SRigSpacePickerWidget::HandleSpaceMoveDown(FRigElementKey InKey)
{
	if(CurrentSpaceKeys.Num() > 1)
	{
		const int32 Index = CurrentSpaceKeys.Find(InKey);
		if(CurrentSpaceKeys.IsValidIndex(Index))
		{
			if(Index < CurrentSpaceKeys.Num() - 1)
			{
				TArray<FRigElementKey> ChangedSpaceKeys = CurrentSpaceKeys;
				ChangedSpaceKeys.Swap(Index, Index + 1);
				SpaceListChangedEvent.Broadcast(Hierarchy, ControlKey, ChangedSpaceKeys);
				return FReply::Handled();
			}
		}
	}
	return FReply::Unhandled();
}

void SRigSpacePickerWidget::HandleSpaceDelete(FRigElementKey InKey)
{
	TArray<FRigElementKey> ChangedSpaceKeys = CurrentSpaceKeys;
	if(ChangedSpaceKeys.Remove(InKey) > 0)
	{
		SpaceListChangedEvent.Broadcast(Hierarchy, ControlKey, ChangedSpaceKeys);
	}
}

FReply SRigSpacePickerWidget::HandleAddElementClicked()
{
	FRigTreeDelegates TreeDelegates;
	TreeDelegates.OnGetHierarchy = FOnGetRigTreeHierarchy::CreateSP(this, &SRigSpacePickerWidget::GetHierarchy);
	TreeDelegates.OnMouseButtonClick = FOnRigTreeMouseButtonClick::CreateLambda([this](TSharedPtr<FRigTreeElement> InItem)
	{
		if(InItem.IsValid())
		{
			const FRigElementKey Key = InItem->Key;
			if(!IsDefaultSpace(Key) && IsValidKey(Key))
			{
				TArray<FRigElementKey> ChangedSpaceKeys = CurrentSpaceKeys;
				ChangedSpaceKeys.AddUnique(Key);
				SpaceListChangedEvent.Broadcast(Hierarchy, ControlKey, ChangedSpaceKeys);
			}
		}

		if(ContextMenu.IsValid())
		{
			ContextMenu.Pin()->Dismiss();
			ContextMenu.Reset();
		}
	});

	TSharedPtr<SSearchableRigHierarchyTreeView> SearchableTreeView = SNew(SSearchableRigHierarchyTreeView)
	.RigTreeDelegates(TreeDelegates);
	SearchableTreeView->GetTreeView()->RefreshTreeView(true);

	// Create as context menu
	TGuardValue<bool> AboutToShowMenu(bLaunchingContextMenu, true);
	ContextMenu = FSlateApplication::Get().PushMenu(
		AsShared(),
		FWidgetPath(),
		SearchableTreeView.ToSharedRef(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
	);

	ContextMenu.Pin()->GetOnMenuDismissed().AddLambda([this](TSharedRef<IMenu> InMenu)
	{
		ContextMenu.Reset();

		if(DialogWindow.IsValid())
		{
			DialogWindow.Pin()->BringToFront(true);

			TSharedRef<SWidget> ThisRef = AsShared();
			FSlateApplication::Get().ForEachUser([&ThisRef](FSlateUser& User) {
				User.SetFocus(ThisRef, EFocusCause::SetDirectly);
			});
		}
	});

	return FReply::Handled();
}

bool SRigSpacePickerWidget::IsSpaceMoveUpEnabled(FRigElementKey InKey) const
{
	if(CurrentSpaceKeys.IsEmpty())
	{
		return false;
	}
	return CurrentSpaceKeys[0] != InKey;
}

bool SRigSpacePickerWidget::IsSpaceMoveDownEnabled(FRigElementKey InKey) const
{
	if(CurrentSpaceKeys.IsEmpty())
	{
		return false;
	}
	return CurrentSpaceKeys.Last( )!= InKey;
}

void SRigSpacePickerWidget::OnHierarchyModified(ERigHierarchyNotification InNotif, URigHierarchy* InHierarchy,
                                                const FRigBaseElement* InElement)
{
	if(InElement == nullptr)
	{
		return;
	}

	if(InElement->GetKey() != ControlKey)
	{
		return;
	}
	
	switch(InNotif)
	{
		case ERigHierarchyNotification::ParentChanged:
		case ERigHierarchyNotification::ParentWeightsChanged:
		case ERigHierarchyNotification::ControlSettingChanged:
		{
			bRepopulateRequired = true;
			break;
		}
		default:
		{
			break;
		}
	}
}

FSlateColor SRigSpacePickerWidget::GetButtonColor(ESpacePickerType InType, FRigElementKey InKey) const
{
	static const FSlateColor ActiveColor = FControlRigEditorStyle::Get().SpacePickerSelectColor;
	
	switch(InType)
	{
		case ESpacePickerType_Parent:
		{
			// this is also true if the object has no parent
			if(GetActiveSpace() == DefaultParentKey)
			{
				return ActiveColor;
			}
			break;
		}
		case ESpacePickerType_World:
		{
			if(GetActiveSpace() == WorldSocketKey && WorldSocketKey.IsValid())
			{
				return ActiveColor;
			}
			break;
		}
		case ESpacePickerType_Item:
		default:
		{
			if(GetActiveSpace() == InKey && InKey.IsValid())
			{
				return ActiveColor;
			}
			break;
		}
	}
	
	return FStyleColors::Transparent;
}

TArray<FRigElementKey> SRigSpacePickerWidget::GetCurrentParents(URigHierarchy* InHierarchy,
	const FRigElementKey& InControlKey) const
{
	if(!InControlKey.IsValid() || InHierarchy == nullptr)
	{
		return TArray<FRigElementKey>();
	}

	check(InControlKey == ControlKey);
	return InHierarchy->GetParents(ControlKey);
}

void SRigSpacePickerWidget::RepopulateItemSpaces()
{
	if(!ItemSpacesListBox.IsValid())
	{
		return;
	}
	
	TArray<FRigElementKey> FavoriteKeys, SpacesFromDelegate;

	// gather the keys coming from the input customization
	if(Customization && bShowFavoriteSpaces)
	{
		for(const FRigElementKey& Key : Customization->AvailableSpaces)
		{
			if(IsDefaultSpace(Key) || !IsValidKey(Key))
			{
				continue;
			}
			FavoriteKeys.AddUnique(Key);
		}
	}

	// check if the customization is different from the base one in the asset
	if(const FRigControlElement* ControlElement = Hierarchy->Find<FRigControlElement>(ControlKey))
	{
		if(Customization != &ControlElement->Settings.Customization)
		{
			for(const FRigElementKey& Key : ControlElement->Settings.Customization.AvailableSpaces)
			{
				if(IsDefaultSpace(Key) || !IsValidKey(Key))
				{
					continue;
				}

				if(Customization)
				{
					if(Customization->AvailableSpaces.Contains(Key))
					{
						continue;
					}
					if(Customization->RemovedSpaces.Contains(Key))
					{
						continue;
					}
				}
				FavoriteKeys.AddUnique(Key);
			}
		}
	}

	// now gather all of the spaces using the get additional spaces delegate
	if(GetAdditionalSpacesDelegate.IsBound() && bShowAdditionalSpaces)
	{
		AdditionalSpaces = GetAdditionalSpacesDelegate.Execute(Hierarchy, ControlKey);
		for(const FRigElementKey& Key : AdditionalSpaces)
		{
			if(IsDefaultSpace(Key)  || !IsValidKey(Key))
			{
				continue;
			}
			SpacesFromDelegate.AddUnique(Key);
		}
	}

	/*
	struct FKeySortPredicate
	{
		bool operator()(const FRigElementKey& A, const FRigElementKey& B) const
		{
			static TMap<ERigElementType, int32> TypeOrder;
			if(TypeOrder.IsEmpty())
			{
				TypeOrder.Add(ERigElementType::Control, 0);
				TypeOrder.Add(ERigElementType::Socket, 1);
				TypeOrder.Add(ERigElementType::Null, 2);
				TypeOrder.Add(ERigElementType::Bone, 3);
				TypeOrder.Add(ERigElementType::RigidBody, 4);
			}

			const int32 TypeIndexA = TypeOrder.FindChecked(A.Type);
			const int32 TypeIndexB = TypeOrder.FindChecked(B.Type);
			if(TypeIndexA != TypeIndexB)
			{
				return TypeIndexA < TypeIndexB;
			}

			return A.Name.Compare(B.Name) < 0; 
		}
	};
	SpacesFromDelegate.Sort(FKeySortPredicate());
	*/

	TArray<FRigElementKey> Keys = FavoriteKeys;
	for(const FRigElementKey& Key : SpacesFromDelegate)
	{
		Keys.AddUnique(Key);
	}

	if(Keys == CurrentSpaceKeys)
	{
		return;
	}

	ClearListBox(ItemSpacesListBox);

	for(const FRigElementKey& Key : Keys)
	{
		AddSpacePickerRow(
			ItemSpacesListBox,
			ESpacePickerType_Item,
			Key,
			SRigHierarchyItem::GetBrushForElementType(Hierarchy, Key),
			FText::FromName(Key.Name),
			FOnClicked::CreateSP(this, &SRigSpacePickerWidget::HandleElementSpaceClicked, Key)
		);
	}

	CurrentSpaceKeys = Keys;
}

void SRigSpacePickerWidget::ClearListBox(TSharedPtr<SVerticalBox> InListBox)
{
	InListBox->ClearChildren();
}

void SRigSpacePickerWidget::UpdateActiveSpace()
{
	ActiveSpace = FRigElementKey();
	
	const TArray<FRigElementWeight> ParentWeights = Hierarchy->GetParentWeightArray(ControlKey);
	if(ParentWeights.Num() > 0)
	{
		const TArray<FRigElementKey> ParentKeys = Hierarchy->GetParents(ControlKey);
		check(ParentKeys.Num() == ParentWeights.Num());
		for(int32 ParentIndex=0;ParentIndex<ParentKeys.Num();ParentIndex++)
		{
			if(ParentWeights[ParentIndex].IsAlmostZero())
			{
				continue;
			}
			ActiveSpace = ParentKeys[ParentIndex];
			return;
		}
	}
}

bool SRigSpacePickerWidget::IsValidKey(const FRigElementKey& InKey) const
{
	return InKey.IsValid() && Hierarchy->Contains(InKey);
}

bool SRigSpacePickerWidget::IsDefaultSpace(const FRigElementKey& InKey) const
{
	if(bShowDefaultSpaces)
	{
		return InKey == WorldSocketKey || InKey == DefaultParentKey;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
