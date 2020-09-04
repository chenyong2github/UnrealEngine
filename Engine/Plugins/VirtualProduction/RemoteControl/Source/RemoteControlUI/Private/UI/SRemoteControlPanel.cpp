// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRemoteControlPanel.h"

#include "IPropertyRowGenerator.h"
#include "Algo/Transform.h"
#include "RemoteControlPanelStyle.h"
#include "PropertyCustomizationHelpers.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Application/SlateApplicationBase.h"
#include "Misc/StringBuilder.h"
#include "SSearchableItemList.h"
#include "UObject/StructOnScope.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IHotReload.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanel"

namespace RemoteControlPanelUtil
{
	/** Find a node by its name in a detail tree node hierarchy. */
	TSharedPtr<IDetailTreeNode> FindNode(const TArray<TSharedRef<IDetailTreeNode>>& RootNodes, FName PropertyName)
	{
		for (const TSharedRef<IDetailTreeNode>& CategoryNode : RootNodes)
		{
			TArray<TSharedRef<IDetailTreeNode>> Children;
			CategoryNode->GetChildren(Children);
			for (const TSharedRef<IDetailTreeNode>& ChildNode : Children)
			{
				TSharedPtr<IPropertyHandle> Handle = ChildNode->CreatePropertyHandle();
				
				if (Handle && Handle->IsValidHandle())
				{
					if (Handle->GetProperty()->GetFName() == PropertyName)
					{
						return ChildNode;
					}
				}
			}
		}
		return nullptr;
	}

	/** Recursively create a property widget. */
	TSharedRef<SWidget> CreatePropertyWidget(const TSharedPtr<IDetailTreeNode>& Node)
	{
		FNodeWidgets NodeWidgets = Node->CreateNodeWidgets();

		TSharedRef<SVerticalBox> VerticalWrapper = SNew(SVerticalBox);
		TSharedRef<SHorizontalBox> FieldWidget = SNew(SHorizontalBox);

		if (NodeWidgets.NameWidget && NodeWidgets.ValueWidget)
		{
			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.AutoWidth()
				[
					NodeWidgets.NameWidget.ToSharedRef()
				];

			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.AutoWidth()
				[
					NodeWidgets.ValueWidget.ToSharedRef()
				];
		}
		else if (NodeWidgets.WholeRowWidget)
		{
			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.AutoWidth()
				[
					NodeWidgets.WholeRowWidget.ToSharedRef()
				];
		}

		VerticalWrapper->AddSlot()
			.AutoHeight()
			[
				FieldWidget
			];

		TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
		Node->GetChildren(ChildNodes);
		
		for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
		{
			VerticalWrapper->AddSlot()
				.AutoHeight()
				.Padding(5.0f, 0.0f)
				[
					CreatePropertyWidget(ChildNode)
				];
		}

		return VerticalWrapper;
	}
}

/** Wrapper around a weak object pointer and the object's name. */
struct FListEntry
{
	FString Name;
	FSoftObjectPtr ObjectPtr;
};

/**
 * Widget that displays an exposed field.
 */
struct SExposedFieldWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SExposedFieldWidget)
		: _Content()
		, _EditMode(true)
		{}
	SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_NAMED_SLOT(FArguments, OptionsContent)
	SLATE_ATTRIBUTE(bool, EditMode)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const FRemoteControlField& Field, TSharedRef<IPropertyRowGenerator> InRowGenerator)
	{
		FieldId = Field.FieldId;
		FieldType = Field.FieldType;
		FieldName = Field.FieldName;
		RowGenerator = MoveTemp(InRowGenerator);
		OptionsWidget = InArgs._OptionsContent.Widget;
		bEditMode = InArgs._EditMode;

		ChildSlot
		[
			MakeFieldWidget(InArgs._Content.Widget)
		];
	}

	TSharedRef<SWidget> MakeFieldWidget(const TSharedRef<SWidget>& InWidget)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						InWidget

					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 1.0f)
					[
						SNew(SButton)
						.Visibility_Raw(this, &SExposedFieldWidget::GetVisibilityAccordingToEditMode)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton")
						.OnClicked_Lambda([this]() { bShowOptions = !bShowOptions; return FReply::Handled(); })
						[
							SNew(STextBlock)
							.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
							.Text(FText::FromString(FString(TEXT("\xf067"))) /*fa-plus*/)
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.Padding(FMargin(0.0f, 0.0f))
					.Visibility_Lambda([this]() {return bShowOptions ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						OptionsWidget.ToSharedRef()
					]
				]
			];
	}

	void BindObjects(const TArray<UObject*>& InObjects)
	{
		RowGenerator->OnFinishedChangingProperties().AddLambda([this, InObjects](const FPropertyChangedEvent& Event) { CreateWidget(InObjects); });
		CreateWidget(InObjects);
	}

	void CreateWidget(const TArray<UObject*>& InObjects)
	{
		RowGenerator->SetObjects(InObjects);
		TSharedPtr<SWidget> NewWidget;
		if (TSharedPtr<IDetailTreeNode> Node = RemoteControlPanelUtil::FindNode(RowGenerator->GetRootTreeNodes(), FieldName))
		{
			NewWidget = RemoteControlPanelUtil::CreatePropertyWidget(MoveTemp(Node));
		}
		else
		{
			NewWidget = SNullWidget::NullWidget;
		}
		ChildSlot.AttachWidget(MakeFieldWidget(NewWidget.ToSharedRef()));
	}

	EVisibility GetVisibilityAccordingToEditMode() const
	{
		return bEditMode.Get() ? EVisibility::Visible : EVisibility::Collapsed;
	}

	/** ID of the exposed field. */
	FGuid FieldId;
	/** Type of the exposed field. */
	EExposedFieldType FieldType;
	/** Name of the field. */
	FName FieldName;
	/** Whether the row should display its options. */
	bool bShowOptions = false;
	/** The widget that displays the field's options ie. Function arguments or metadata. */
	TSharedPtr<SWidget> OptionsWidget;
	/** Holds the generator that creates the widgets. */
	TSharedPtr<IPropertyRowGenerator> RowGenerator;
	/** Whether the panel is in edit mode or not. */
	TAttribute<bool> bEditMode;
};

/**
 * Represents a section that represents one or more grouped objects.
 * Displays a list of exposed fields.
 */
class FPanelSection : public STableRow<TSharedPtr<SExposedFieldWidget>>
{
public:
	FPanelSection(const FString& Alias, TSharedRef<SRemoteControlPanel>& InOwnerPanel)
		: bSelected(false)
		, SectionAlias(Alias)
		, WeakPanel(InOwnerPanel)
	{
		DisplayName = GenerateDisplayName(Alias);
		GenerateExposedPropertyWidgets();
		GenerateExposedFunctionWidgets();
		BindPropertyWidgets();
	}

	/** Get the underlying section */
	FRemoteControlSection& GetUnderlyingSection()
	{
		TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin();
		check(Panel);
		return Panel->GetPreset()->GetRemoteControlSections().FindChecked(SectionAlias);
	}

	/** Get the underlying section */
	const FRemoteControlSection& GetUnderlyingSection() const
	{
		const TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin();
		check(Panel);
		return Panel->GetPreset()->GetRemoteControlSections().FindChecked(SectionAlias);
	}

	/** Get the common class of the section's top level objects */
	UClass* GetSectionClass() { return GetUnderlyingSection().SectionClass; }

	/** Returns whether or not the section is selected. */
	bool IsSelected() const { return bSelected; }

	/** Selects or unselects the section. */
	void SetIsSelected(bool InSelected) { bSelected = InSelected; }

	/** Returns the section's display name. */
	const FText& GetDisplayName() const { return DisplayName; }

	/** Returns the alias of the section. */
	const FString& GetSectionAlias() const { return SectionAlias; }

	/** Generates the widget that wraps the exposed field widgets. */
	TSharedRef<ITableRow> GenerateWidget(const TSharedRef<STableViewBase>& OwnerTable)
	{
		// This is to disable the light background on hover.
		class SObjectRow : public STableRow<TSharedPtr<SExposedFieldWidget>>
		{
			virtual const FSlateBrush* GetBorder() const override
			{
				return FEditorStyle::GetBrush("DetailsView.CollapsedCategory");
			}
		};

		TSharedRef<SVerticalBox> FieldsSection = SNew(SVerticalBox);
		TSharedRef<SVerticalBox> SectionBox = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage_Lambda([this]() {return bSelected ? FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.Selection") : FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.HeaderSectionBorder"); })
			.BorderBackgroundColor_Lambda([this]() {return bSelected ? FLinearColor(1, 0.664f, 0.303f, 0.6f) : FLinearColor(.6, .6, .6, 1.0f); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SButton)
					.Visibility_Raw(this, &FPanelSection::GetVisibilityAccordingToEditMode)
					.OnClicked(this, &FPanelSection::HandleRemoveSection)
					.ButtonStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.UnexposeButton")
					[
						SNew(STextBlock)
						.ColorAndOpacity_Lambda([this]() { return bSelected ? FLinearColor(0, 0, 0, 0.3) : FLinearColor(1, 1, 1, 0.5); })
						.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
						.Text(FText::FromString(FString(TEXT("\xf00d"))) /*fa-times*/)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				 .Padding(0, 5.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.ForegroundColor_Lambda([this]() { return bSelected ? FLinearColor(0, 0, 0, 1) : FLinearColor(1, 1, 1, 1); })
					.Text(DisplayName)
					.IsReadOnly_Lambda([]() { /*@todo: Add renaming sections*/ return true; })
					.Style(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.SectionNameTextBox")
					.BackgroundColor(FLinearColor::Transparent)
				]
		
				+ SHorizontalBox::Slot()
				.Padding(2.5f, 0.0f)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SAssignNew(ExposeFunctionButton, SComboButton)
					.Visibility_Raw(this, &FPanelSection::GetVisibilityAccordingToEditMode)
					.ButtonStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.ExposeFunctionButton")
					.ForegroundColor(FSlateColor::UseForeground())
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							SNew(SImage)
							.Image(FEditorStyle::Get().GetBrush("GraphEditor.Function_16x"))
						]
					]
					.OnGetMenuContent(this, &FPanelSection::GetExposeFunctionButtonContent)
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5, 0)
		[
			SNew(SBorder)
			.Padding(FMargin(0.0f, 0.0f))
			.BorderImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.FieldsSectionBorder"))
			.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
			[
				FieldsSection
			]
		];

		for (const TSharedRef<SExposedFieldWidget>& ExposedFieldWidget : ExposedFieldWidgets)
		{
			FieldsSection->AddSlot()
				.Padding(5.0f)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Top)
					.Padding(0, 1.0f)
					.AutoWidth()
					[
						SNew(SButton)
						.Visibility_Raw(this, &FPanelSection::GetVisibilityAccordingToEditMode)
						.OnClicked(this, &FPanelSection::HandleUnexposeField, ExposedFieldWidget->FieldId)
						.ButtonStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.UnexposeButton")
						[
							SNew(STextBlock)
							.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
							.Text(FText::FromString(FString(TEXT("\xf00d"))) /*fa-times*/)
						]
					]
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Top)
					.AutoWidth()
					[
						ExposedFieldWidget
					]
				];
		}

		return SNew(SObjectRow, OwnerTable)
			.ShowSelection(false)
			[
				SNew(SBox)
				.Padding(FMargin(2.0f, 0.0f, 2.0f, 10.0f))
				[
					SectionBox
				]
			];
	}
private:
	/** Generates a display name for the section based on the alias. */
	FText GenerateDisplayName(const FString& Alias) const
	{
		const FRemoteControlSection& Section = GetUnderlyingSection();
		if (Section.SectionClass->IsChildOf<UBlueprintFunctionLibrary>())
		{
			return FText::FromString(Section.SectionClass->GetName());
		}

		TStringBuilder<100> SectionDisplayName;
		SectionDisplayName << TEXT("{0} (");
		const int32 MAX_NAMES_DISPLAYED = 3;

		const TArray<UObject*> SectionObjects = Section.ResolveSectionObjects();

		for (int32 i = 0; i < FMath::Min(SectionObjects.Num(), MAX_NAMES_DISPLAYED); ++i)
		{
			UObject* SectionObject = SectionObjects[i];
			if (SectionObject->IsA<AActor>())
			{
				SectionDisplayName << Cast<AActor>(SectionObject)->GetActorLabel();
			}
			else
			{
				SectionDisplayName << SectionObject->GetName();
			}

			if (i != MAX_NAMES_DISPLAYED - 1 && i != SectionObjects.Num() - 1)
			{
				SectionDisplayName << ", ";
			}
			else
			{
				SectionDisplayName << ")";
			}
		}

		return FText::FromString(FString::Format(*SectionDisplayName, { Alias }));
	}

	/** Generate widgets for the exposed properties. */
	void GenerateExposedPropertyWidgets()
	{
		// @Todo: Disable modifying if it's bound to a CDO
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FPropertyRowGeneratorArgs GeneratorArgs;

		for (const FRemoteControlProperty& Property: GetUnderlyingSection().ExposedProperties)
		{
			TSharedRef<IPropertyRowGenerator> RowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(GeneratorArgs);
			RowGenerator->SetObjects({ Property.FieldOwnerClass->GetDefaultObject() });
			if (TSharedPtr<IDetailTreeNode> Node = RemoteControlPanelUtil::FindNode(RowGenerator->GetRootTreeNodes(), Property.FieldName))
			{
				ExposedFieldWidgets.Add(SNew(SExposedFieldWidget, Property, RowGenerator)
				.EditMode_Raw(this, &FPanelSection::GetPanelEditMode)
				[
					RemoteControlPanelUtil::CreatePropertyWidget(Node)
				]);
			}
		}
	}

	/** Generate widgets for the exposed functions. */
	void GenerateExposedFunctionWidgets()
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
		for (const FRemoteControlFunction& RCFunction : GetUnderlyingSection().ExposedFunctions)
		{
			TSharedRef<SVerticalBox> ArgsSection = SNew(SVerticalBox);

			FPropertyRowGeneratorArgs GeneratorArgs;
			GeneratorArgs.bShouldShowHiddenProperties = true;

			// @todo: Generate only necessary properties instead of creating multiple generators
			TSharedRef<IPropertyRowGenerator> RowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(GeneratorArgs);
			RowGenerator->SetStructure(RCFunction.FunctionArguments);
			for (TFieldIterator<FProperty> It(RCFunction.Function); It && It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm); ++It)
			{
				checkSlow(*It);

				if (TSharedPtr<IDetailTreeNode> PropertyNode = RemoteControlPanelUtil::FindNode(RowGenerator->GetRootTreeNodes(), It->GetFName()))
				{
					FNodeWidgets Widget = PropertyNode->CreateNodeWidgets();

					if (Widget.NameWidget && Widget.ValueWidget)
					{
						TSharedPtr<SBox> ValueBox;
						TSharedRef<SHorizontalBox> FieldWidget =
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.Padding(FMargin(3.0f, 2.0f))
							.AutoWidth()
							[
								Widget.NameWidget.ToSharedRef()
							]
						+ SHorizontalBox::Slot()
							.Padding(FMargin(3.0f, 2.0f))
							.AutoWidth()
							[
								Widget.ValueWidget.ToSharedRef()
							];

						ArgsSection->AddSlot()
							.AutoHeight()
							.Padding(FMargin(0.0f, 0.0f))
							[
								FieldWidget
							];
					}
					else if (Widget.WholeRowWidget)
					{
						ArgsSection->AddSlot()
							.AutoHeight()
							[
								Widget.WholeRowWidget.ToSharedRef()
							];
					}
				}
			}

			TSharedRef<SWidget> ButtonWidget = SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Left)
				.AutoHeight()
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked_Raw(this, &FPanelSection::OnClickFunctionButton, RCFunction)
					[
						SNew(STextBlock)
						.Text(FText::FromName(RCFunction.FieldName))
					]
				];

			TSharedRef<SWidget> ArgsWidget =
				SNew(SBorder)
				.BorderImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.FieldsSectionBorder"))
				.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
				.Padding(FMargin(5.0f, 3.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ArgumentsLabel", "Arguments"))
					]
					+ SVerticalBox::Slot()
					.Padding(5.0f, 2.0f, 5.0f, 0.0f)
					.AutoHeight()
					[
						ArgsSection
					]
				];

				ExposedFieldWidgets.Add(
					SNew(SExposedFieldWidget, RCFunction, RowGenerator)
					.EditMode_Raw(this, &FPanelSection::GetPanelEditMode)
					.Content()
					[
						MoveTemp(ButtonWidget)
					]
					.OptionsContent()
					[
						MoveTemp(ArgsWidget)
					]
				);
			}
	}

	/** Bind the property widgets to the section's top level objects. */
	void BindPropertyWidgets()
	{
		FRemoteControlSection& Section = GetUnderlyingSection();
		TArray<UObject*> SectionObjects = Section.ResolveSectionObjects();
		for (const TSharedRef<SExposedFieldWidget>& FieldWidget : ExposedFieldWidgets)
		{
			if (FieldWidget->FieldType == EExposedFieldType::Property)
			{
				TOptional<FRemoteControlProperty> Field = Section.GetProperty(FieldWidget->FieldId);
				if (Field.IsSet())
				{
					FieldWidget->BindObjects(Field->ResolveFieldOwners(SectionObjects));
				}
				else
				{
					ensureAlwaysMsgf(false, TEXT("Field %s was not found."), *FieldWidget->FieldName.ToString());
				}
			}
		}
	}
	
	/** Handle clicking on an exposed function's button. */
	FReply OnClickFunctionButton(FRemoteControlFunction FunctionField)
	{
		// @todo Wrap in transaction
		FEditorScriptExecutionGuard ScriptGuard;
		for (UObject* Object : GetUnderlyingSection().ResolveSectionObjects())
		{
			if (FunctionField.FunctionArguments)
			{
				Object->ProcessEvent(FunctionField.Function, FunctionField.FunctionArguments->GetStructMemory());
			}
		}

		return FReply::Handled();
	}

	/** Handle removing a section. */
	FReply HandleRemoveSection()
	{
		if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
		{
			Panel->GetPreset()->DeleteSection(SectionAlias);
			Panel->ClearSelection();
			Panel->Refresh();
		}
		return FReply::Handled();
	}

	/** Handle creating the widget to select a function.  */
	TSharedRef<SWidget> GetExposeFunctionButtonContent()
	{
		auto FunctionFilter = [](const UFunction* TestFunction)
		{
			return TestFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_Public);
		};

		TArray<UFunction*> ExposableFunctions;
		TSet<FName> BaseActorFunctionNames;

		for (TFieldIterator<UFunction> FunctionIter(AActor::StaticClass(), EFieldIteratorFlags::IncludeSuper); FunctionIter; ++FunctionIter)
		{
			if (FunctionFilter(*FunctionIter))
			{
				BaseActorFunctionNames.Add(FunctionIter->GetFName());
			}
		}

		FRemoteControlSection& Section = GetUnderlyingSection();
		if (Section.SectionClass)
		{
			for (TFieldIterator<UFunction> FunctionIter(Section.SectionClass, EFieldIteratorFlags::IncludeSuper); FunctionIter; ++FunctionIter)
			{
				UFunction* TestFunction = *FunctionIter;

				if (FunctionFilter(TestFunction)
					&& !BaseActorFunctionNames.Contains(TestFunction->GetFName()))
				{
					if (!ExposableFunctions.FindByPredicate([FunctionName = TestFunction->GetFName()](const UObject* Func) { return Func->GetFName() == FunctionName; }))
					{
						ExposableFunctions.Add(*FunctionIter);
					}
				}
			}
		}

		return SNew(SBox)
			.WidthOverride(300.0f)
			.MaxDesiredHeight(200.0f)
			[
				SNew(SSearchableItemList<UFunction*>)
				.Items(ExposableFunctions)
				.OnGetDisplayName_Lambda([](UFunction* InFunction) { return InFunction->GetName(); })
				.OnItemSelected(this, &FPanelSection::OnExposeFunction)
			];
	}

	/** Handle exposing a function. */
	void OnExposeFunction(UFunction* InFunction)
	{
		if (InFunction)
		{
			TArray<UObject*> SectionObjects = GetUnderlyingSection().ResolveSectionObjects();
			if (SectionObjects.Num() > 0)
			{
				GetUnderlyingSection().Expose(FRemoteControlFunction(SectionObjects[0], InFunction));
				if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
				{
					Panel->SelectSection(GetUnderlyingSection().Alias);
					Panel->Refresh();
				}
			}
		}
		ExposeFunctionButton->SetIsOpen(false);
	}

	/** Handle unexposing a field. */
	FReply HandleUnexposeField(FGuid FieldId)
	{
		GetUnderlyingSection().Unexpose(FieldId);
		if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
		{
			Panel->Refresh();
		}
		return FReply::Handled();
	}

	/** Handle getting the visibility of certain widgets according to the panel's mode. */
	EVisibility GetVisibilityAccordingToEditMode() const
	{
		if (const TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
		{
			return Panel->IsInEditMode() ? EVisibility::Visible : EVisibility::Collapsed;
		}
		return EVisibility::All;
	}

	/** Handle displaying/hiding a field's options when prompted. */
	FReply OnClickExpandButton(TSharedRef<SExposedFieldWidget> ExposedFieldWidget)
	{
		ExposedFieldWidget->bShowOptions = !ExposedFieldWidget->bShowOptions;
		RefreshPanelLayout();
		return FReply::Handled();
	}

	void RefreshPanelLayout()
	{
		if(TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
		{
			Panel->RefreshLayout();
		}
	}

	bool GetPanelEditMode() const
	{
		if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
		{
			return Panel->IsInEditMode();
		}
		return false;
	}
	
private:
	/** Whether the section is selected or not. */
	bool bSelected;
	/** The name displayed in the section header. */
	FText DisplayName;
	/** The section's underlying alias. */
	FString SectionAlias;
	/** Weak pointer to the parent panel. */
	TWeakPtr<SRemoteControlPanel> WeakPanel;
	/** Holds the exposed fields. */
	TArray<TSharedRef<SExposedFieldWidget>> ExposedFieldWidgets;
	/** Holds the expose function button */
	TSharedPtr<SMenuAnchor> ExposeFunctionButton;
};

void SRemoteControlPanel::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset)
{
	OnEditModeChange = InArgs._OnEditModeChange;
	Preset = TStrongObjectPtr<URemoteControlPreset>(InPreset);
	bIsInEditMode = true;

	ReloadBlueprintLibraries();
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
 				SNew(SComboButton)
				.Visibility_Lambda([this]()
					{
						return bIsInEditMode ? EVisibility::Visible : EVisibility::Collapsed;
					})
				.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
				.ButtonContent()
				[
					SNew(STextBlock)
					.Margin(FMargin(5.0f, 0.0f))
					.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
					.Text(INVTEXT("Pick actor"))
				]
				.OnGetMenuContent(this, &SRemoteControlPanel::OnGetActorPickerMenuContent)
			]
			+ SHorizontalBox::Slot()
			.Padding(FMargin(5.0f, 0.0f))
			.AutoWidth()
			[
				CreateBlueprintLibraryPicker()
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.FillWidth(1.0f)
			.Padding(0, 7.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0)
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
			SAssignNew(ListView, SListView<TSharedRef<FPanelSection>>)
			.ListItemsSource(&SectionList)
			.OnSelectionChanged(this, &SRemoteControlPanel::HandleSelectionChanged)
			.OnGenerateRow(this, &SRemoteControlPanel::OnGenerateRow)
		]
	];

	Refresh();
}

bool SRemoteControlPanel::IsExposed(const TSharedPtr<IPropertyHandle>& Handle)
{
	return FindPropertyId(Handle).IsValid();
}

void SRemoteControlPanel::ToggleProperty(const TSharedPtr<IPropertyHandle>& Handle)
{
	FGuid PropertyId = FindPropertyId(Handle);
	if (PropertyId.IsValid())
	{
		Unexpose(PropertyId);
	}
	else
	{
		Expose(Handle);
	}
}

FReply SRemoteControlPanel::OnMouseButtonUp(const FGeometry&, const FPointerEvent&)
{
	ListView->ClearSelection();
	return FReply::Handled();
}


void SRemoteControlPanel::RegisterEvents()
{
	if (GEditor)
	{
		FEditorDelegates::OnAssetsDeleted.AddLambda([this](const TArray<UClass*>&) { Refresh(); });
		GEditor->OnBlueprintReinstanced().AddLambda(
			[this]()
			{
				ReloadBlueprintLibraries();
				Refresh();
			});
		IHotReloadModule::Get().OnHotReload().AddLambda(
			[this](bool)
			{
				ReloadBlueprintLibraries();
				Refresh();
			});
	}
}

void SRemoteControlPanel::Refresh()
{
	if (Preset)
	{
		TSharedPtr<FPanelSection> SelectedSection;
		if (ListView->GetNumItemsSelected() > 0)
		{
			SelectedSection = ListView->GetSelectedItems()[0];
		}
		SectionList = CreateObjectSections();
		ListView->RebuildList();
		if (SelectedSection)
		{
			SelectSection(SelectedSection->GetSectionAlias());
		}
	}
}

void SRemoteControlPanel::RefreshLayout()
{
	ListView->RequestListRefresh();

	TArray<TSharedRef<FPanelSection>> SelectedSection;
	if (ListView->GetSelectedItems(SelectedSection) == 1)
	{
		ListView->RequestScrollIntoView(SelectedSection[0]);
	}
}

void SRemoteControlPanel::ClearSelection()
{
	ListView->ClearSelection();
}

void SRemoteControlPanel::Expose(const TSharedPtr<IPropertyHandle>& Handle)
{
	TArray<UObject*> OuterObjects;
	Handle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() == 0 || !Handle->GetProperty())
	{
		ensureAlwaysMsgf(false, TEXT("Could not expose property."));
		return;
	}

	FRemoteControlProperty Property{ OuterObjects[0], Handle->GetProperty()->GetFName() };

	// Replace the outer objects with the section objects so that a component is never a section by itself.
	for (UObject*& Object : OuterObjects)
	{
		if (Object->IsA<UActorComponent>())
		{
			Object = Object->GetTypedOuter<AActor>();
		}
	}

	// Figure out if the field can be exposed under an existing section.
	if (TSharedPtr<FPanelSection> SelectedSection = GetSelectedSection())
	{
		if (SelectedSection->GetUnderlyingSection().HasSameTopLevelObjects(OuterObjects))
		{
			SelectedSection->GetUnderlyingSection().Expose(MoveTemp(Property));
			Refresh();
			SelectSection(SelectedSection->GetSectionAlias());
			return;
		}
	}

	if (ensureAlwaysMsgf(Preset->CanCreateSection(OuterObjects), TEXT("Section could not be created with the selected objects.")))
	{
		FRemoteControlSection& Section = Preset->CreateSection(OuterObjects);
		Section.Expose(MoveTemp(Property));
		Refresh();
		SelectSection(Section.Alias);
		ListView->ScrollToBottom();
	}
}

void SRemoteControlPanel::Unexpose(const FGuid& FieldId)
{
	if (TSharedPtr<FPanelSection> SelectedSection = GetSelectedSection())
	{
		SelectedSection->GetUnderlyingSection().Unexpose(FieldId);
	}

	Refresh();
}

FGuid SRemoteControlPanel::FindPropertyId(const TSharedPtr<IPropertyHandle>& Handle)
{
	if (TSharedPtr<FPanelSection> SelectedSection = GetSelectedSection())
	{
		TArray<UObject*> OuterObjects;
		Handle->GetOuterObjects(OuterObjects);

		if (!ensure(OuterObjects.Num() != 0))
		{
			return FGuid();
		}

		if (!SelectedSection->GetUnderlyingSection().HasSameTopLevelObjects(OuterObjects))
		{
			return FGuid();
		}

		return SelectedSection->GetUnderlyingSection().FindPropertyId(Handle->GetProperty()->GetFName());
	}

	return FGuid();
}

TSharedRef<SWidget> SRemoteControlPanel::CreateBlueprintLibraryPicker()
{
	return SAssignNew(BlueprintLibraryPicker, SComboButton)
		.Visibility_Lambda([this]() { return bIsInEditMode ? EVisibility::Visible : EVisibility::Collapsed; })
		.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
		.ForegroundColor(FSlateColor::UseForeground())
		.CollapseMenuOnParentFocus(true)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(2.0f, 0.0f))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
				.Text(LOCTEXT("FunctionLibrariesLabel", "Function Libraries"))
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FEditorStyle::Get().GetBrush("GraphEditor.Function_16x"))
			]
		]
		.MenuContent()
		[
			SNew(SBox)
			.WidthOverride(300.0f)
			.MaxDesiredHeight(200.0f)
			[
				SNew(SSearchableItemList<TSharedPtr<FListEntry>>)
				.Items(BlueprintLibraries)
				.OnGetDisplayName_Lambda([](TSharedPtr<FListEntry> InEntry) { return InEntry->Name; })
				.OnItemSelected(this, &SRemoteControlPanel::OnExposeBlueprintLibrary)
			]
		];
}

TSharedRef<ITableRow> SRemoteControlPanel::OnGenerateRow(TSharedRef<FPanelSection> Section, const TSharedRef<STableViewBase>& OwnerTable)
{
	return Section->GenerateWidget(OwnerTable);
}

void SRemoteControlPanel::SelectActorsInlevel(const TArray<UObject*>& Objects)
{
	if (GEditor)
	{
		// Don't change selection if the target's component is already selected
		USelection* Selection = GEditor->GetSelectedComponents();
		if (Selection->Num() == 1 && Objects.Num() == 1 && Cast<UActorComponent>(Selection->GetSelectedObject(0))->GetOwner() == Objects[0])
		{
			return;
		}

		GEditor->SelectNone(false, true, false);

		for (UObject* Object : Objects)
		{
			if (AActor* Actor = Cast<AActor>(Object))
			{
				GEditor->SelectActor(Actor, true, true, true);
			}
		}
	}
}

TArray<TSharedRef<FPanelSection>> SRemoteControlPanel::CreateObjectSections()
{
	TArray<TSharedRef<FPanelSection>> UISections;

	TMap<FString, FRemoteControlSection>& RemoteControlSections = Preset->GetRemoteControlSections();

	UISections.Reserve(RemoteControlSections.Num());

	TSharedRef<SRemoteControlPanel> PanelPtr = SharedThis<SRemoteControlPanel>(this);
	for (TTuple<FString, FRemoteControlSection>& MapEntry : RemoteControlSections)
	{
		UISections.Add(MakeShared<FPanelSection>(MapEntry.Key, PanelPtr));
	}

	return UISections;
}

TSharedRef<SWidget> SRemoteControlPanel::OnGetActorPickerMenuContent()
{
	return PropertyCustomizationHelpers::MakeActorPickerWithMenu(nullptr,
		true,
		FOnShouldFilterActor::CreateLambda([](const AActor*) { return true; }),
		FOnActorSelected::CreateRaw(this, &SRemoteControlPanel::OnSelectActorToExpose),
		FSimpleDelegate::CreateRaw(this, &SRemoteControlPanel::OnCloseActorPicker),
		FSimpleDelegate::CreateRaw(this, &SRemoteControlPanel::OnExposeSelectedActor));
}

void SRemoteControlPanel::OnSelectActorToExpose(AActor* Actor)
{
	if (Actor)
	{
		ExposeObjects({ Actor });
	}
}

void SRemoteControlPanel::OnCloseActorPicker()
{
	FSlateApplication::Get().SetUserFocus(0, SharedThis(this));
}

void SRemoteControlPanel::OnExposeSelectedActor()
{
	TArray<UObject*> SelectedObjects;
	GEditor&& GEditor->GetSelectedActors()->GetSelectedObjects(AActor::StaticClass(), SelectedObjects);
	ExposeObjects(SelectedObjects);
}


void SRemoteControlPanel::ExposeObjects(const TArray<UObject*>& Objects)
{
	if (Objects.Num() > 0)
	{
		FRemoteControlSection& Section = Preset->CreateSection(Objects);
		Refresh();
		SelectSection(Section.Alias);
		ListView->ScrollToBottom();
	}
}

FReply SRemoteControlPanel::HandleRemoveSection(const FString& Alias)
{
	Preset->DeleteSection(Alias);
	ListView->ClearSelection();
	Refresh();
	return FReply::Handled();
}

void SRemoteControlPanel::HandleSelectionChanged(TSharedPtr<FPanelSection> InSection, ESelectInfo::Type InSelectInfo)
{
	if (!bIsInEditMode)
	{
		return;
	}

	for (const TSharedPtr<FPanelSection>& Section : SectionList)
	{
		Section->SetIsSelected(false);
	}

	if (InSection)
	{
		SelectActorsInlevel(InSection->GetUnderlyingSection().ResolveSectionObjects());
		InSection->SetIsSelected(true);
	}
}

void SRemoteControlPanel::SelectSection(const FString& SectionName)
{
	if (TSharedRef<FPanelSection>* SectionToSelect = SectionList.FindByPredicate([&SectionName](const TSharedRef<FPanelSection>& Section) { return Section->GetSectionAlias() == SectionName; }))
	{
		ListView->SetSelection(*SectionToSelect);
		ListView->RequestScrollIntoView(*SectionToSelect);
		// @todo minor: Scroll by an additional amount that corresponds to the space the section content takes
	}
}

void SRemoteControlPanel::OnEditModeCheckboxToggle(ECheckBoxState State)
{
	bIsInEditMode = State == ECheckBoxState::Checked ? true : false;
	if (!bIsInEditMode)
	{
		if (TSharedPtr<FPanelSection> SelectedSection = GetSelectedSection())
		{
			SelectedSection->SetIsSelected(false);
		}
		ListView->ClearSelection();
	}
	OnEditModeChange.ExecuteIfBound(SharedThis(this), bIsInEditMode);
}

void SRemoteControlPanel::OnExposeBlueprintLibrary(TSharedPtr<FListEntry> InEntry)
{
	if (ensure(InEntry))
	{
		UObject* Library = InEntry->ObjectPtr.Get();
		if (!ensure(Library))
		{
			return;
		}
		BlueprintLibraryPicker->SetIsOpen(false);
		ExposeObjects({ Library });
	}
}

TSharedPtr<FPanelSection> SRemoteControlPanel::GetSelectedSection()
{
	if (ListView->GetNumItemsSelected() != 0)
	{
		return ListView->GetSelectedItems()[0];
	}
	return nullptr;
}

void SRemoteControlPanel::ReloadBlueprintLibraries()
{
	BlueprintLibraries.Reset();
	for (auto It = TObjectIterator<UBlueprintFunctionLibrary>(EObjectFlags::RF_NoFlags); It; ++It)
	{
		if (It->GetClass() != UBlueprintFunctionLibrary::StaticClass())
		{
			FListEntry ListEntry{ It->GetClass()->GetName(), FSoftObjectPtr(*It) };
			BlueprintLibraries.Add(MakeShared<FListEntry>(MoveTemp(ListEntry)));
		}
	}
}

#undef LOCTEXT_NAMESPACE /* RemoteControlPanel */
