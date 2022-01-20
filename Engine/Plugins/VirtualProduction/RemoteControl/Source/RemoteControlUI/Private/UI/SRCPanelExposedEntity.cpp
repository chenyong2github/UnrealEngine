// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelExposedEntity.h"

#include "ActorTreeItem.h"
#include "Components/ActorComponent.h"
#include "EditorFontGlyphs.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "RemoteControlEntity.h"
#include "RemoteControlField.h"
#include "RemoteControlPanelStyle.h"
#include "RemoteControlPreset.h"
#include "SceneOutlinerFilters.h"
#include "SceneOutlinerModule.h"
#include "ScopedTransaction.h"
#include "SRCPanelDragHandle.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
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
		return Entity->CanBindObject(Actor);
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
		Preset->RebindAllEntitiesUnderSameActor(Entity->GetId(), InActor);
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


#undef LOCTEXT_NAMESPACE
