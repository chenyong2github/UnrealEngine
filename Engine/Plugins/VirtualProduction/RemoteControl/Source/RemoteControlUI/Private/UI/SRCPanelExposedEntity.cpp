// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelExposedEntity.h"

#include "ActorTreeItem.h"
#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "Engine/Selection.h"
#include "Engine/Classes/Components/ActorComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "RemoteControlBinding.h"
#include "RemoteControlEntity.h"
#include "RemoteControlPreset.h"
#include "RemoteControlField.h"
#include "RemoteControlSettings.h"
#include "RemoteControlPanelStyle.h"
#include "SRCPanelDragHandle.h"
#include "SceneOutlinerFilters.h"
#include "SceneOutlinerModule.h"
#include "ScopedTransaction.h"
#include "Styling/SlateIconFinder.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanel"

void SRCPanelExposedEntity::Tick(const FGeometry&, const double, const float)
{
	if (bNeedsRename)
	{
		if (NameTextBox)
		{
			NameTextBox->EnterEditingMode();
		}
		bNeedsRename = false;
	}
}

TSharedPtr<FRemoteControlEntity> SRCPanelExposedEntity::GetEntity() const
{
	if (Preset.IsValid())
	{
		return Preset->GetExposedEntity(EntityId).Pin();
	}
	return nullptr;
}

TSharedPtr<SWidget> SRCPanelExposedEntity::GetContextMenu()
{
	FMenuBuilder MenuBuilder(true, TSharedPtr<const FUICommandList>());

	constexpr bool bNoIndent = true;
	MenuBuilder.AddWidget(CreateUseContextCheckbox(), LOCTEXT("UseContextLabel", "Use Context"), bNoIndent);

	MenuBuilder.AddSubMenu(
		LOCTEXT("EntityRebindSubmenuLabel", "Rebind"),
		LOCTEXT("EntityRebindSubmenuToolTip", "Pick an object to rebind this exposed entity."),
		FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenuBuilder)
		{
			constexpr bool bNoIndent = true;
			SubMenuBuilder.AddWidget(CreateRebindMenuContent(), FText::GetEmpty(), bNoIndent);
		}));

	MenuBuilder.AddSubMenu(
		LOCTEXT("EntityRebindComponentSubmenuLabel", "Rebind Component"),
		LOCTEXT("EntityRebindComponentSubmenuToolTip", "Pick a component to rebind this exposed entity."),
		FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenuBuilder)
			{
				CreateRebindComponentMenuContent(SubMenuBuilder);
			}));

	MenuBuilder.AddSubMenu(
		LOCTEXT("EntityRebindAllUnderActorSubmenuLabel", "Rebind all properties for this actor"),
		LOCTEXT("EntityRebindAllUnderActorSubmenuToolTip", "Pick an actor to rebind."),
		FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenuBuilder)
			{
				constexpr bool bNoIndent = true;
				SubMenuBuilder.AddWidget(CreateRebindAllPropertiesForActorMenuContent(), FText::GetEmpty(), bNoIndent);
			}));

	return MenuBuilder.MakeWidget();
}

void SRCPanelExposedEntity::Initialize(const FGuid& InEntityId, URemoteControlPreset* InPreset, const TAttribute<bool>& InbEditMode)
{
	EntityId = InEntityId;
	Preset = InPreset;
	bEditMode = InbEditMode;

	if (ensure(InPreset))
	{
		if (const TSharedPtr<FRemoteControlEntity> RCEntity = InPreset->GetExposedEntity(InEntityId).Pin())
		{
			CachedLabel = RCEntity->GetLabel();
		}
	}
}

void SRCPanelExposedEntity::CreateRebindComponentMenuContent(FMenuBuilder& SubMenuBuilder)
{
	TInlineComponentArray<UActorComponent*> ComponentArray;

	if (TSharedPtr<FRemoteControlEntity> Entity = GetEntity())
	{
		TArray<UObject*> BoundObjects = Entity->GetBoundObjects();
		if (BoundObjects.Num() && BoundObjects[0])
		{
			if (BoundObjects[0]->IsA<UActorComponent>())
			{
				if (AActor* OwnerActor = BoundObjects[0]->GetTypedOuter<AActor>())
				{
					OwnerActor->GetComponents(Entity->GetSupportedBindingClass(), ComponentArray);
				}
			}
		}

		for (UActorComponent* Component : ComponentArray)
		{
			SubMenuBuilder.AddMenuEntry(
				FText::FromString(Component->GetName()),
				FText::GetEmpty(),
				FSlateIconFinder::FindIconForClass(Component->GetClass(), TEXT("SCS.Component")),
				FUIAction(
					FExecuteAction::CreateLambda([Entity, Component]
						{
							if (Entity)
							{
								Entity->BindObject(Component);
							}
						}),
					FCanExecuteAction())
			);
		}
	}
}

TSharedRef<SWidget> SRCPanelExposedEntity::CreateRebindAllPropertiesForActorMenuContent()
{
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
	FSceneOutlinerInitializationOptions Options;
	Options.bFocusSearchBoxWhenOpened = true;
	Options.Filters = MakeShared<FSceneOutlinerFilters>();

	if (TSharedPtr<FRemoteControlEntity> Entity = GetEntity())
	{
		Options.Filters->AddFilterPredicate<FActorTreeItem>(FActorTreeItem::FFilterPredicate::CreateRaw(this, &SRCPanelExposedEntity::IsActorSelectable));
	}

	return SNew(SBox)
		.MaxDesiredHeight(400.0f)
		.WidthOverride(300.0f)
		[
			SceneOutlinerModule.CreateActorPicker(Options, FOnActorPicked::CreateRaw(this, &SRCPanelExposedEntity::OnActorSelectedForRebindAllProperties))
		];
}

TSharedRef<SWidget> SRCPanelExposedEntity::CreateInvalidWidget()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SComboButton)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RebindLabel", "Rebind"))
			]
			.MenuContent()
			[
				SNew(SBox)
				.MaxDesiredHeight(400.0f)
				.WidthOverride(300.0f)
				[
					CreateRebindMenuContent()
				]
			]
		];
}

EVisibility SRCPanelExposedEntity::GetVisibilityAccordingToEditMode(EVisibility NonEditModeVisibility) const
{
	return bEditMode.Get() ? EVisibility::Visible : NonEditModeVisibility;
}

TSharedRef<SWidget> SRCPanelExposedEntity::CreateRebindMenuContent()
{
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
	FSceneOutlinerInitializationOptions Options;
	Options.Filters = MakeShared<FSceneOutlinerFilters>();

	Options.Filters->AddFilterPredicate<FActorTreeItem>(FActorTreeItem::FFilterPredicate::CreateRaw(this, &SRCPanelExposedEntity::IsActorSelectable));

	return SNew(SBox)
	.MaxDesiredHeight(400.0f)
	.WidthOverride(300.0f)
	[
		SceneOutlinerModule.CreateActorPicker(Options, FOnActorPicked::CreateRaw(this, &SRCPanelExposedEntity::OnActorSelected), nullptr)
	];
}

bool SRCPanelExposedEntity::OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
{
	if (URemoteControlPreset* RCPreset = Preset.Get())
	{
		if (InLabel.ToString() != CachedLabel.ToString() && RCPreset->GetExposedEntityId(*InLabel.ToString()).IsValid())
		{
			OutErrorMessage = LOCTEXT("NameAlreadyExists", "This name already exists.");
			return false;
		}
	}

	return true;
}

void SRCPanelExposedEntity::OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
{
	if (URemoteControlPreset* RCPreset = Preset.Get())
	{
		FScopedTransaction Transaction(LOCTEXT("ModifyEntityLabel", "Modify exposed entity's label."));
		RCPreset->Modify();
		CachedLabel = RCPreset->RenameExposedEntity(EntityId, *InLabel.ToString());
		NameTextBox->SetText(FText::FromName(CachedLabel));
	}
}

void SRCPanelExposedEntity::OnActorSelected(AActor* InActor) const
{
	if (TSharedPtr<FRemoteControlEntity> Entity = GetEntity())
	{
		Entity->BindObject(InActor);
	}

	FSlateApplication::Get().DismissAllMenus();
}

const FSlateBrush* SRCPanelExposedEntity::GetBorderImage() const
{
	return FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.ExposedFieldBorder");
}

bool SRCPanelExposedEntity::IsActorSelectable(const AActor* Actor) const
{
	if (TSharedPtr<FRemoteControlEntity> Entity = GetEntity())
	{
		if (Entity->GetBindings().Num())
		{
			// Don't show what it's already bound to.
			if (UObject* Component = Entity->GetBindings()[0]->Resolve())
			{
				if (Component == Actor || Component->GetTypedOuter<AActor>() == Actor)
				{
					return false;
				}
			}

			if (ShouldUseRebindingContext())
			{
				if (URemoteControlLevelDependantBinding* Binding = Cast<URemoteControlLevelDependantBinding>(Entity->GetBindings()[0].Get()))
				{
					if (UClass* SupportedClass = Binding->GetSupportedOwnerClass())
					{
						return Actor->GetClass()->IsChildOf(SupportedClass);
					}
				}
			}

			return Entity->CanBindObject(Actor);
		}
	}
	return false;
}

TSharedRef<SWidget> SRCPanelExposedEntity::CreateEntityWidget(TSharedPtr<SWidget> ValueWidget, const FText& OptionalWarningMessage)
{
	FMakeNodeWidgetArgs Args;

	TSharedRef<SBorder> Widget = SNew(SBorder)
		.Padding(0.0f)
		.BorderImage_Raw(this, &SRCPanelExposedEntity::GetBorderImage);
	
	Args.DragHandle = SNew(SBox)
		.Visibility_Raw(this, &SRCPanelExposedEntity::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
		[
			SNew(SRCPanelDragHandle<FExposedEntityDragDrop>, GetRCId())
			.Widget(Widget)
		];

	Args.NameWidget = SNew(SHorizontalBox)
		.Clipping(EWidgetClipping::OnDemand)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 2.0f, 0.f)
		.AutoWidth()
		[
			SNew(STextBlock)
			.Visibility(!OptionalWarningMessage.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
            .TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
            .Font(FAppStyle::Get().GetFontStyle("FontAwesome.10"))
            .ToolTipText(OptionalWarningMessage)
            .Text(FEditorFontGlyphs::Exclamation_Triangle)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SAssignNew(NameTextBox, SInlineEditableTextBlock)
			.Text(FText::FromName(CachedLabel))
			.OnTextCommitted_Raw(this, &SRCPanelExposedEntity::OnLabelCommitted)
			.OnVerifyTextChanged_Raw(this, &SRCPanelExposedEntity::OnVerifyItemLabelChanged)
			.IsReadOnly_Lambda([this]() { return !bEditMode.Get(); })
		];

	Args.RenameButton = SNew(SButton)
		.Visibility_Raw(this, &SRCPanelExposedEntity::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
		.ButtonStyle(FAppStyle::Get(), "FlatButton")
		.OnClicked_Lambda([this]() {
			bNeedsRename = true;
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
				.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
				.Font(FAppStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FText::FromString(FString(TEXT("\xf044"))) /*fa-edit*/)
		];

	Args.ValueWidget = ValueWidget;

	Args.UnexposeButton = SNew(SButton)
		.Visibility_Raw(this, &SRCPanelExposedEntity::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
		.OnPressed_Raw(this, &SRCPanelExposedEntity::HandleUnexposeEntity)
		.ButtonStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.UnexposeButton")
		[
			SNew(STextBlock)
			.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
			.Font(FAppStyle::Get().GetFontStyle("FontAwesome.10"))
			.Text(FText::FromString(FString(TEXT("\xf00d"))) /*fa-times*/)
		];

	Widget->SetContent(MakeNodeWidget(Args));
	return Widget;
}

void SRCPanelExposedEntity::OnActorSelectedForRebindAllProperties(AActor* InActor) const
{
	if (TSharedPtr<FRemoteControlEntity> Entity = GetEntity())
	{
		const bool bShouldUseRebindingContext = ShouldUseRebindingContext();

		Preset->RebindAllEntitiesUnderSameActor(Entity->GetId(), InActor, bShouldUseRebindingContext);
		SelectActor(InActor);
	}

	FSlateApplication::Get().DismissAllMenus();
}

void SRCPanelExposedEntity::HandleUnexposeEntity()
{
	if (URemoteControlPreset* RCPreset = Preset.Get())
	{
		FScopedTransaction Transaction(LOCTEXT("UnexposeFunction", "Unexpose remote control entity"));
		RCPreset->Modify();
		RCPreset->Unexpose(EntityId);
	}
}

void SRCPanelExposedEntity::SelectActor(AActor* InActor) const
{
	if (GEditor)
	{
		// Don't change selection if the target's component is already selected
		USelection* Selection = GEditor->GetSelectedComponents();

		const bool bComponentSelected = Selection->Num() == 1
			&& Selection->GetSelectedObject(0) != nullptr
			&& Selection->GetSelectedObject(0)->GetTypedOuter<AActor>() == InActor;

		if (!bComponentSelected)
		{
			constexpr bool bNoteSelectionChange = false;
			constexpr bool bDeselectBSPSurfs = true;
			constexpr bool WarnAboutManyActors = false;
			GEditor->SelectNone(bNoteSelectionChange, bDeselectBSPSurfs, WarnAboutManyActors);

			constexpr bool bInSelected = true;
			constexpr bool bNotify = true;
			constexpr bool bSelectEvenIfHidden = true;
			GEditor->SelectActor(InActor, bInSelected, bNotify, bSelectEvenIfHidden);
		}
	}
}

TSharedRef<SWidget> SRCPanelExposedEntity::CreateUseContextCheckbox()
{
	return SNew(SCheckBox)
		.ToolTipText(LOCTEXT("UseRebindingContextTooltip", "Unchecking this will allow you to rebind this property to any object regardless of the underlying supported class."))

		// Bind the button's "on checked" event to our object's method for this
		.OnCheckStateChanged_Raw(this, &SRCPanelExposedEntity::OnUseContextChanged)

		// Bind the check box's "checked" state to our user interface action
		.IsChecked_Raw(this, &SRCPanelExposedEntity::IsUseContextEnabled);
}

void SRCPanelExposedEntity::OnUseContextChanged(ECheckBoxState State)
{
	if (URemoteControlSettings* Settings = GetMutableDefault<URemoteControlSettings>())
	{
		if (State == ECheckBoxState::Unchecked)
		{
			Settings->bUseRebindingContext = false;
		}
		else if (State == ECheckBoxState::Checked)
		{
			Settings->bUseRebindingContext = true;
		}
	}
}

ECheckBoxState SRCPanelExposedEntity::IsUseContextEnabled() const
{
	return ShouldUseRebindingContext() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool SRCPanelExposedEntity::ShouldUseRebindingContext() const
{
	if (const URemoteControlSettings* Settings = GetDefault<URemoteControlSettings>())
	{
		return Settings->bUseRebindingContext;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
