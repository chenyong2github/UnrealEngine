// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRemoteControlPanel.h"

#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "EditorFontGlyphs.h"
#include "Engine/Selection.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
#include "GameFramework/Actor.h"
#include "RemoteControlPreset.h"
#include "RemoteControlPanelStyle.h"
#include "PropertyHandle.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "RemoteControlActor.h"
#include "RemoteControlField.h"
#include "RemoteControlUIModule.h"
#include "SRCPanelExposedEntitiesList.h"
#include "SRCPanelFunctionPicker.h"
#include "SRCPanelFieldGroup.h"
#include "SRCPanelTreeNode.h"
#include "Subsystems/Subsystem.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"

#include "UObject/SoftObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanel"

void SRemoteControlPanel::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset)
{
	OnEditModeChange = InArgs._OnEditModeChange;
	Preset = TStrongObjectPtr<URemoteControlPreset>(InPreset);

	TArray<TSharedRef<SWidget>> ExtensionWidgets;
	FRemoteControlUIModule::Get().GetExtensionGenerators().Broadcast(ExtensionWidgets);

	TSharedPtr<SHorizontalBox> TopExtensions;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
			// Top tool bar
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				CreateCPUThrottleButton()
			]

			+ SHorizontalBox::Slot()
			.Padding(FMargin(5.0f, 0.0f))
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.Visibility_Lambda([this]() { return bIsInEditMode ? EVisibility::Visible : EVisibility::Collapsed; })
				.ButtonStyle(FEditorStyle::Get(), "FlatButton")
				.OnClicked(this, &SRemoteControlPanel::OnCreateGroup)
				[
					SNew(STextBlock)
					.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(FText::FromString(FString(TEXT("\xf07b"))) /*fa-plus-square-o*/)
				]
			]
			// Function library picker
			+ SHorizontalBox::Slot()
			.Padding(FMargin(5.0f, 0.0f))
			.AutoWidth()
			[
				SAssignNew(BlueprintPicker, SRCPanelFunctionPicker)
				.AllowDefaultObjects(true)
				.Label(LOCTEXT("FunctionLibrariesLabel", "Function Libraries"))
				.ObjectClass(UBlueprintFunctionLibrary::StaticClass())
				.OnSelectFunction_Raw(this, &SRemoteControlPanel::ExposeFunction)
			]
			// Actor function picker
			+ SHorizontalBox::Slot()
			.Padding(FMargin(5.0f, 0.0f))
			.AutoWidth()
			[
				SAssignNew(ActorFunctionPicker, SRCPanelFunctionPicker)
				.Label(LOCTEXT("ActorFunctionsLabel", "Actor Functions"))
				.ObjectClass(AActor::StaticClass())
				.OnSelectFunction_Raw(this, &SRemoteControlPanel::ExposeFunction)
			]
			// Subsystem function picker
			+ SHorizontalBox::Slot()
			.Padding(FMargin(5.0f, 0.0f))
			.AutoWidth()
			[
				SAssignNew(SubsystemFunctionPicker, SRCPanelFunctionPicker)
				.Label(LOCTEXT("SubsystemFunctionLabel", "Subsystem Functions"))
				.ObjectClass(USubsystem::StaticClass())
				.OnSelectFunction_Raw(this, &SRemoteControlPanel::ExposeFunction)
			]
			
			// Expose actor
			+ SHorizontalBox::Slot()
			.Padding(FMargin(5.0f, 0.0f))
			.AutoWidth()
			[
				SNew(SObjectPropertyEntryBox)
					.AllowedClass(AActor::StaticClass())
					.OnObjectChanged(this, &SRemoteControlPanel::OnExposeActor)
					.AllowClear(false)
					.DisplayUseSelected(true)
					.DisplayBrowse(true)
					.NewAssetFactories(TArray<UFactory*>())
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.FillWidth(1.0f)
			.Padding(0, 7.0f)
			[
				SAssignNew(TopExtensions, SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EditModeLabel", "Edit Mode: "))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return this->bIsInEditMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged(this, &SRemoteControlPanel::OnEditModeCheckboxToggle)
				]
			]
		]
		+ SVerticalBox::Slot()
		[
			SAssignNew(EntityList, SRCPanelExposedEntitiesList, Preset.Get())
			.EditMode_Lambda([this](){ return bIsInEditMode; })
		]
	];

	for (const TSharedRef<SWidget>& Widget : ExtensionWidgets)
	{
		// We want to insert the widgets before the edit mode buttons.
		TopExtensions->InsertSlot(TopExtensions->NumSlots()-2)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			Widget
		];
	}

	RegisterEvents();
	Refresh();
}

SRemoteControlPanel::~SRemoteControlPanel()
{
	UnregisterEvents();
}

void SRemoteControlPanel::PostUndo(bool bSuccess)
{
	Refresh();
}

void SRemoteControlPanel::PostRedo(bool bSuccess)
{
	Refresh();
}

bool SRemoteControlPanel::IsExposed(const TSharedPtr<IPropertyHandle>& PropertyHandle)
{
	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);

	FString Path = PropertyHandle->GeneratePathToProperty();

	TArray<TSharedPtr<FRemoteControlProperty>, TInlineAllocator<1>> PotentialMatches;
	for (const TWeakPtr<FRemoteControlProperty>& WeakProperty : Preset->GetExposedEntities<FRemoteControlProperty>())
	{
		if (TSharedPtr<FRemoteControlProperty> Property = WeakProperty.Pin())
		{
			if (Property->FieldPathInfo.ToPathPropertyString() == Path)
			{
				PotentialMatches.Add(Property);
			}
		}
	}

	bool bAllObjectsExposed = true;

	for (UObject* OuterObject : OuterObjects)
	{
		bool bFoundPropForObject = false;

		for (const TSharedPtr<FRemoteControlProperty>& Property : PotentialMatches)
		{
			if (Property->ResolveFieldOwners().Contains(OuterObject))
			{
				bFoundPropForObject = true;
				break;
			}
		}

		bAllObjectsExposed &= bFoundPropForObject;
	}

	return bAllObjectsExposed;
}

void SRemoteControlPanel::ToggleProperty(const TSharedPtr<IPropertyHandle>& PropertyHandle)
{
	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);

	if (IsExposed(PropertyHandle))
	{
		FScopedTransaction Transaction(LOCTEXT("UnexposeProperty", "Unexpose Property"));
		Preset->Modify();
		Unexpose(PropertyHandle);
		return;
	}

	if (OuterObjects.Num())
	{
		FScopedTransaction Transaction(LOCTEXT("ExposeProperty", "Expose Property"));
		Preset->Modify();

		for (UObject* Object : OuterObjects)
		{
			if (Object)
			{
				constexpr bool bCleanDuplicates = true; //GeneratePathToProperty duplicates container name (Array.Array[1], Set.Set[1], etc...)
				ExposeProperty(Object, FRCFieldPathInfo{PropertyHandle->GeneratePathToProperty(), bCleanDuplicates});
			}
		}
	}
}

FGuid SRemoteControlPanel::GetSelectedGroup() const
{
	if (TSharedPtr<SRCPanelTreeNode> Node = EntityList->GetSelection())
	{
		if (Node->AsGroup())
		{
			return Node->GetId();		
		}
	}
	return FGuid();
}

FReply SRemoteControlPanel::OnClickDisableUseLessCPU() const
{
	UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
	Settings->bThrottleCPUWhenNotForeground = false;
	Settings->PostEditChange();
	Settings->SaveConfig();
	return FReply::Handled();
}

TSharedRef<SWidget> SRemoteControlPanel::CreateCPUThrottleButton() const
{
	FProperty* PerformanceThrottlingProperty = FindFieldChecked<FProperty>(UEditorPerformanceSettings::StaticClass(), GET_MEMBER_NAME_CHECKED(UEditorPerformanceSettings, bThrottleCPUWhenNotForeground));
	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("PropertyName"), PerformanceThrottlingProperty->GetDisplayNameText());
	FText PerformanceWarningText = FText::Format(LOCTEXT("RemoteControlPerformanceWarning", "Warning: The editor setting '{PropertyName}' is currently enabled\nThis will stop editor windows from updating in realtime while the editor is not in focus"), Arguments);

	return SNew(SButton)
		.ButtonStyle(FEditorStyle::Get(), "FlatButton")
		.Visibility_Lambda([]() {return GetDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground ? EVisibility::Visible : EVisibility::Collapsed; } )
		.OnClicked_Raw(this, &SRemoteControlPanel::OnClickDisableUseLessCPU)
		[
			SNew(STextBlock)
			.ToolTipText(MoveTemp(PerformanceWarningText))
			.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
			.Text(FEditorFontGlyphs::Exclamation_Triangle)
		];
}

void SRemoteControlPanel::RegisterEvents()
{
	if (GEditor)
	{
		GEditor->OnBlueprintReinstanced().AddSP(this, &SRemoteControlPanel::OnBlueprintReinstanced);
	}
}

void SRemoteControlPanel::UnregisterEvents()
{
	if (GEditor)
	{
		GEditor->OnBlueprintReinstanced().RemoveAll(this);
	}
}

void SRemoteControlPanel::Refresh()
{
	BlueprintPicker->Refresh();
	ActorFunctionPicker->Refresh();
	SubsystemFunctionPicker->Refresh();
}

void SRemoteControlPanel::Unexpose(const TSharedPtr<IPropertyHandle>& Handle)
{
	TArray<UObject*> OuterObjects;
	Handle->GetOuterObjects(OuterObjects);

	FString Path = Handle->GeneratePathToProperty();

	// Find an exposed property with the same path.
	TArray<TSharedPtr<FRemoteControlProperty>, TInlineAllocator<1>> PotentialMatches;
	for (const TWeakPtr<FRemoteControlProperty>& WeakProperty : Preset->GetExposedEntities<FRemoteControlProperty>())
	{
		if (TSharedPtr<FRemoteControlProperty> Property = WeakProperty.Pin())
		{
			if (Property->FieldPathInfo.ToPathPropertyString() == Path)
			{
				PotentialMatches.Add(Property);
			}
		}
	}

	for (const TSharedPtr<FRemoteControlProperty>& Property : PotentialMatches)
	{
		TArray<UObject*> PropertyOwners = Property->ResolveFieldOwners();
		if (PropertyOwners.Num() != OuterObjects.Num())
		{
			continue;
		}

		bool bHasSameObjects = true;
		for (UObject* Owner : PropertyOwners)
		{
			if (!OuterObjects.Contains(Owner))
			{
				bHasSameObjects = false;
				break;
			}
		}

		if (bHasSameObjects && OuterObjects.Num())
		{
			Preset->Unexpose(Property->GetId());
			break;
		}
	}
}

void SRemoteControlPanel::OnEditModeCheckboxToggle(ECheckBoxState State)
{
	bIsInEditMode = State == ECheckBoxState::Checked ? true : false;
	OnEditModeChange.ExecuteIfBound(SharedThis(this), bIsInEditMode);
}
 
void SRemoteControlPanel::OnBlueprintReinstanced()
{
	Refresh();
}

FReply SRemoteControlPanel::OnCreateGroup()
{
	FScopedTransaction Transaction(LOCTEXT("CreateGroup", "Create Group"));
	Preset->Modify();
	Preset->Layout.CreateGroup();
	return FReply::Handled();
}

void SRemoteControlPanel::ExposeProperty(UObject* Object, FRCFieldPathInfo Path)
{
	if (Path.Resolve(Object))
	{
		FRemoteControlPresetExposeArgs Args;
		Args.GroupId = GetSelectedGroup();
		Preset->ExposeProperty(Object, MoveTemp(Path), MoveTemp(Args));
	}
}

void SRemoteControlPanel::ExposeFunction(UObject* Object, UFunction* Function)
{
	FScopedTransaction Transaction(LOCTEXT("ExposeFunction", "ExposeFunction"));
	Preset->Modify();

	FRemoteControlPresetExposeArgs Args;
	Args.GroupId = GetSelectedGroup();
	Preset->ExposeFunction(Object, Function, MoveTemp(Args));
}

void SRemoteControlPanel::OnExposeActor(const FAssetData& AssetData)
{
	if (AActor* Actor = Cast<AActor>(AssetData.GetAsset()))
	{
		FScopedTransaction Transaction(LOCTEXT("ExposeActor", "Expose Actor"));
		Preset->Modify();
		Preset->Expose(Actor);
	}
}

#undef LOCTEXT_NAMESPACE /*RemoteControlPanel*/
