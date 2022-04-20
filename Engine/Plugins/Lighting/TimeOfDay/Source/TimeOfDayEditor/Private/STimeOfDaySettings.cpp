// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimeOfDaySettings.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "SSubobjectInstanceEditor.h"
#include "EngineUtils.h"
#include "TimeOfDayActor.h"
#include "TimeOfDaySubsystem.h"
#include "ISCSEditorUICustomization.h"
#include "SPositiveActionButton.h"

#define LOCTEXT_NAMESPACE "TimeOfDayEditor"

class FTimeOfDaySCSEditorUICustomization : public ISCSEditorUICustomization
{
public:
	virtual bool HideComponentsFilterBox() const override { return true; }
	virtual bool HideBlueprintButtons() const override { return true; }
};

enum class ESettingsSection : uint8
{
	Environment,
	TimeOfDay,
};

void STimeOfDaySettings::Construct(const FArguments& InArgs)
{
	FEditorDelegates::MapChange.AddSP(this, &STimeOfDaySettings::OnMapChanged);

	UpdateTimeOfDayActor();

	TSharedRef<SSubobjectInstanceEditor> SubObjectEditorRef
		= SNew(SSubobjectInstanceEditor)
		.ObjectContext(this, &STimeOfDaySettings::GetObjectContext)
		//.AllowEditing(this, &SActorDetails::GetAllowComponentTreeEditing)
		.OnSelectionUpdated(this, &STimeOfDaySettings::OnSubobjectEditorTreeViewSelectionChanged);
		//.OnItemDoubleClicked(this, &SActorDetails::OnSubobjectEditorTreeViewItemDoubleClicked)
		//.OnObjectReplaced(this, &SActorDetails::OnSubobjectEditorTreeViewObjectReplaced);
	
	SubObjectEditorRef->SetUICustomization(MakeShared<FTimeOfDaySCSEditorUICustomization>());

	SubobjectEditor = SubObjectEditorRef;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bShowObjectLabel = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	ComponentDetailsView = DetailsView;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(10, 4)
		.AutoHeight()
		[
			SNew(SSegmentedControl<ESettingsSection>)
			.OnValueChanged(this, &STimeOfDaySettings::OnSettingsSectionChanged)
			+ SSegmentedControl<ESettingsSection>::Slot(ESettingsSection::Environment)
			.Text(LOCTEXT("EnvironmentSettings", "Environment"))
			.ToolTip(LOCTEXT("EnvironmentSettings_ToolTip", "Set up the time of day environment"))
			+ SSegmentedControl<ESettingsSection>::Slot(ESettingsSection::TimeOfDay)
			.Text(LOCTEXT("TimeOfDaySettings", "Time of Day"))
			.ToolTip(LOCTEXT("TimeOfDaySettings_ToolTip", "Specify time of day settings"))
		]
		+ SVerticalBox::Slot()
		[
			SAssignNew(SettingsSwitcher, SWidgetSwitcher)
			+ SWidgetSwitcher::Slot()
			[
				MakeEnvironmentPanel()
			]
			+ SWidgetSwitcher::Slot()
			[
				MakeEditDaySequencePanel()
			]
		]
	];

	SettingsSwitcher->SetActiveWidgetIndex(0);
}

void STimeOfDaySettings::OnSettingsSectionChanged(ESettingsSection NewSection)
{
	SettingsSwitcher->SetActiveWidgetIndex(NewSection == ESettingsSection::Environment ? 0 : 1);
}

void STimeOfDaySettings::OnMapChanged(uint32 Flags)
{
	if (Flags == MapChangeEventFlags::NewMap)
	{
		UpdateTimeOfDayActor();
	}
}

UObject* STimeOfDaySettings::GetObjectContext() const
{
	return EditorTimeOfDayActor.Get();
}

void STimeOfDaySettings::UpdateTimeOfDayActor()
{
	EditorTimeOfDayActor = nullptr;

	if (GEditor)
	{
		UTimeOfDaySubsystem* TODSubsystem = GEditor->GetEditorWorldContext().World()->GetSubsystem<UTimeOfDaySubsystem>();
		EditorTimeOfDayActor = TODSubsystem->GetTimeOfDayActor();
	}
}

FReply STimeOfDaySettings::OnEditDaySequenceClicked()
{
	return FReply::Handled();
}

void STimeOfDaySettings::OnSubobjectEditorTreeViewSelectionChanged(const TArray<TSharedPtr<FSubobjectEditorTreeNode> >& SelectedNodes)
{
	TArray<UObject*> Objects;
	Objects.Reserve(SelectedNodes.Num());

	for (const TSharedPtr<FSubobjectEditorTreeNode>& Node : SelectedNodes)
	{
		if (const UObject* Object = Node->GetObject())
		{
			Objects.Add(const_cast<UObject*>(Object));
		}
	}

	ComponentDetailsView->SetObjects(Objects);
}

TSharedRef<SWidget> STimeOfDaySettings::MakeEnvironmentPanel()
{
	check(SubobjectEditor.IsValid() && ComponentDetailsView.IsValid());
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(10, 4, 4, 4)
			[
				SubobjectEditor->GetToolButtonsBox().ToSharedRef()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4)
			.VAlign(VAlign_Center)
			[
				SNew(SPositiveActionButton)
				.Text(LOCTEXT("EditDaySequence", "Edit Day Sequence"))
				.OnClicked(this, &STimeOfDaySettings::OnEditDaySequenceClicked)
			]
		]
		+ SVerticalBox::Slot()
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+ SSplitter::Slot()
			[
				SubobjectEditor.ToSharedRef()
			]
			+ SSplitter::Slot()
			[
				ComponentDetailsView.ToSharedRef()
			]		
		];
}

TSharedRef<SWidget> STimeOfDaySettings::MakeEditDaySequencePanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight();
}


#undef LOCTEXT_NAMESPACE

