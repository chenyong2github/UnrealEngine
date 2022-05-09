// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDetailsCustomization.h"

#include "OptimusEditorStyle.h"
#include "OptimusHLSLSyntaxHighlighter.h"
#include "SOptimusDataTypeSelector.h"

#include "OptimusDataTypeRegistry.h"
#include "OptimusValueContainer.h"
#include "OptimusShaderText.h"
#include "OptimusBindingTypes.h"
#include "IOptimusParameterBindingProvider.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "DetailWidgetRow.h"
#include "IPropertyTypeCustomization.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyEditor/Private/PropertyNode.h"
#include "PropertyEditor/Public/IPropertyUtilities.h"
#include "OptimusComputeDataInterface.h"
#include "OptimusResourceDescription.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"


#define LOCTEXT_NAMESPACE "OptimusDetailCustomization"


TSharedRef<IPropertyTypeCustomization> FOptimusDataTypeRefCustomization::MakeInstance()
{
	return MakeShared<FOptimusDataTypeRefCustomization>();
}


void FOptimusDataTypeRefCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle, 
	FDetailWidgetRow& InHeaderRow, 
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
{
	EOptimusDataTypeUsageFlags UsageMask = EOptimusDataTypeUsageFlags::None;
	
	if (InPropertyHandle->HasMetaData(FName(TEXT("UseInResource"))))
	{
		UsageMask |= EOptimusDataTypeUsageFlags::Resource;
	}
	if (InPropertyHandle->HasMetaData(FName(TEXT("UseInVariable"))))
	{
		UsageMask |= EOptimusDataTypeUsageFlags::Variable;
	}
	if (InPropertyHandle->HasMetaData(FName(TEXT("UseInAnimAttribute"))))
	{
		UsageMask |= EOptimusDataTypeUsageFlags::AnimAttributes;
	}

	TypeNameProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusDataTypeRef, TypeName));

	InHeaderRow
	.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SOptimusDataTypeSelector)
		.CurrentDataType(this, &FOptimusDataTypeRefCustomization::GetCurrentDataType)
		.UsageMask(UsageMask)
		.Font(InCustomizationUtils.GetRegularFont())
		.OnDataTypeChanged(this, &FOptimusDataTypeRefCustomization::OnDataTypeChanged)
	];
}


void FOptimusDataTypeRefCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	// FIXME: This doesn't update quite properly. Need a better approach.
	FDetailWidgetRow& DeclarationRow = InChildBuilder.AddCustomRow(LOCTEXT("Declaration", "Declaration"));

	DeclarationRow
	.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget(LOCTEXT("Declaration", "Declaration"))
	]
	.ValueContent()
	.HAlign(HAlign_Fill)
	[
		SNew(SBox)
		.MinDesiredWidth(180.0f)
		[
			SNew(SMultiLineEditableTextBox)
			.Text(this, &FOptimusDataTypeRefCustomization::GetDeclarationText)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", InCustomizationUtils.GetRegularFont().Size))
			.IsReadOnly(true)
		]
	];
}


FOptimusDataTypeHandle FOptimusDataTypeRefCustomization::GetCurrentDataType() const
{
	FName TypeName;
	TypeNameProperty->GetValue(TypeName);
	return FOptimusDataTypeRegistry::Get().FindType(TypeName);
}


void FOptimusDataTypeRefCustomization::OnDataTypeChanged(FOptimusDataTypeHandle InDataType)
{
	FScopedTransaction Transaction(LOCTEXT("SetDataType", "Set Data Type"));
	CurrentDataType = InDataType;
	TypeNameProperty->SetValue(InDataType.IsValid() ? InDataType->TypeName : NAME_None);
}


FText FOptimusDataTypeRefCustomization::GetDeclarationText() const
{
	FOptimusDataTypeHandle DataType = GetCurrentDataType();

	if (DataType.IsValid() && DataType->ShaderValueType.IsValid())
	{
		const FShaderValueType* ValueType = DataType->ShaderValueType.ValueTypePtr;
		FText Declaration;
		if (ValueType->Type == EShaderFundamentalType::Struct)
		{
			return FText::FromString(ValueType->GetTypeDeclaration());
		}
		else
		{
			return FText::FromString(ValueType->ToString());
		}
	}
	else
	{
		return FText::GetEmpty();
	}
}

// =============================================================================================

TSharedRef<IPropertyTypeCustomization> FOptimusDataDomainCustomization::MakeInstance()
{
	return MakeShared<FOptimusDataDomainCustomization>();
}


FOptimusDataDomainCustomization::FOptimusDataDomainCustomization()
{
	for (FName Name: UOptimusComputeDataInterface::GetUniqueAllTopLevelContexts())
	{
		ContextNames.Add(Name);
	}
	ContextNames.Sort(FNameLexicalLess());
}


void FOptimusDataDomainCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow,
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
{
	TSharedPtr<IPropertyHandle> ContextNameProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusDataDomain, Name));
	
	InHeaderRow.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SComboBox<FName>)
			.ToolTipText(LOCTEXT("ContextListerToolTip", "Select a resource context from the list of available contexts."))
			.OptionsSource(&ContextNames)
			.IsEnabled_Lambda([InPropertyHandle]() -> bool
			{
				return InPropertyHandle->IsEditable();
			})
			.OnGenerateWidget_Lambda([](FName InName)
			{
				return SNew(STextBlock)
					.Text(FText::FromName(InName))
					.Font(IPropertyTypeCustomizationUtils::GetRegularFont());
			})
			.OnSelectionChanged_Lambda([ContextNameProperty](FName InName, ESelectInfo::Type)
			{
				ContextNameProperty->SetValue(InName);
			})
			[
				SNew(STextBlock)
				.Font(IPropertyTypeCustomizationUtils::GetRegularFont())
				.Text_Lambda([ContextNameProperty]()
				{
					FName Value;
					ContextNameProperty->GetValue(Value);
					return FText::FromName(Value);
				})
			]
	];
}


TSharedRef<IPropertyTypeCustomization> FOptimusMultiLevelDataDomainCustomization::MakeInstance()
{
	return MakeShared<FOptimusMultiLevelDataDomainCustomization>();
}


FOptimusMultiLevelDataDomainCustomization::FOptimusMultiLevelDataDomainCustomization()
{
	for (TArray<FName> Names: UOptimusComputeDataInterface::GetUniqueAllNestedContexts())
	{
		NestedContextNames.Add(MakeShared<TArray<FName>>(Names));
	}
	NestedContextNames.Sort([](const TSharedRef<TArray<FName>>& A, const TSharedRef<TArray<FName>> &B)
	{
		// Compare up to the point that we have same number of members to compare.
		for (int32 Index = 0; Index < FMath::Min(A->Num(), B->Num()); Index++)
		{
			if ((*A)[Index] != (*B)[Index])
			{
				return FNameLexicalLess()((*A)[Index], (*B)[Index]);
			}
		}
		// Otherwise the entry with fewer members goes first.
		return A->Num() < B->Num();
	});
}


void FOptimusMultiLevelDataDomainCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow,
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
{
	auto FormatNames = [](const TArray<FName>& InNames) -> FText
	{
		TArray<FText> NameParts;
		for (FName Name: InNames)
		{
			NameParts.Add(FText::FromName(Name));
		}
		return FText::Join(FText::FromString(UTF8TEXT(" › ")), NameParts);
	};
	
	InHeaderRow.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SComboBox<TSharedRef<TArray<FName>>>)
			.ToolTipText(LOCTEXT("NestedContextListerToolTip", "Select a nested resource context from the list of available contexts."))
			.OptionsSource(&NestedContextNames)
			.IsEnabled_Lambda([InPropertyHandle]() -> bool
			{
				return InPropertyHandle->IsEditable();
			})
			.OnGenerateWidget_Lambda([FormatNames](TSharedRef<TArray<FName>> InNames)
			{
				return SNew(STextBlock)
					.Text(FormatNames(*InNames))
					.Font(IPropertyTypeCustomizationUtils::GetRegularFont());
			})
			.OnSelectionChanged_Lambda([InPropertyHandle](TSharedPtr<TArray<FName>> InNames, ESelectInfo::Type)
			{
				FScopedTransaction Transaction(LOCTEXT("SetResourceContexts", "Set Resource Contexts"));
				// Ideally we'd like to match up the raw data with the outers, but I'm not
				// convinced that there's always 1-to-1 relation.
				TArray<UObject *> OuterObjects;
				InPropertyHandle->GetOuterObjects(OuterObjects);
				for (UObject *OuterObject: OuterObjects)
				{
					// Notify the object that is has been modified so that undo/redo works.
					OuterObject->Modify();
				}
				
				InPropertyHandle->NotifyPreChange();
				TArray<void*> RawDataPtrs;
				InPropertyHandle->AccessRawData(RawDataPtrs);

				for (void* RawPtr: RawDataPtrs)
				{
					static_cast<FOptimusMultiLevelDataDomain*>(RawPtr)->LevelNames = *InNames; 
				}

				InPropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
			})
			[
				SNew(STextBlock)
				.Font(IPropertyTypeCustomizationUtils::GetRegularFont())
				.Text_Lambda([InPropertyHandle, FormatNames]()
				{
					TArray<const void *> RawDataPtrs;
					InPropertyHandle->AccessRawData(RawDataPtrs);

					bool bItemsDiffer = false;
					TArray<FName> Names;
					for (const void* RawPtr: RawDataPtrs)
					{
						const FOptimusMultiLevelDataDomain* DataDomain = static_cast<const FOptimusMultiLevelDataDomain*>(RawPtr);
						// During drag & reorder, invalid binding can be created temporarily
						if (DataDomain)
						{
							if (Names.IsEmpty())
							{
								Names = DataDomain->LevelNames;
							}
							else if (Names != DataDomain->LevelNames)
							{
								bItemsDiffer = true;
								break;
							}
						}
					}

					if (bItemsDiffer)
					{
						return FText::FromString(TEXT("---"));
					}
					else
					{
						return FormatNames(Names);
					}
				})
			]
	];
}


// =============================================================================================

TSharedRef<IPropertyTypeCustomization> FOptimusShaderTextCustomization::MakeInstance()
{
	return MakeShared<FOptimusShaderTextCustomization>();
}


FOptimusShaderTextCustomization::FOptimusShaderTextCustomization() :
	SyntaxHighlighter(FOptimusHLSLSyntaxHighlighter::Create()),
	SyntaxHighlighterMain(FOptimusHLSLSyntaxHighlighter::Create())
{
	
}


void FOptimusShaderTextCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle, 
	FDetailWidgetRow& InHeaderRow, 
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
{
	DeclarationsProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusShaderText, Declarations));
	ShaderTextProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusShaderText, ShaderText));
	DiagnosticsProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusShaderText, Diagnostics));

	// Make sure the diagnostics are updated to reflect the error highlighting.
	UpdateDiagnostics();

	// Watch any changes to the diagnostics array and act on it. It's a giant hammer, but
	// it's the best we have.
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FOptimusShaderTextCustomization::OnPropertyChanged);
	InPropertyHandle->GetOuterObjects(InspectedObjects);
	
	HorizontalScrollbar =
	    SNew(SScrollBar)
	        .AlwaysShowScrollbar(true)
	        .Orientation(Orient_Horizontal);

	VerticalScrollbar =
	    SNew(SScrollBar)
			.AlwaysShowScrollbar(true)
	        .Orientation(Orient_Vertical);

	const FTextBlockStyle &TextStyle = FOptimusEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("TextEditor.NormalText");
	const FSlateFontInfo &Font = TextStyle.Font;

	InHeaderRow
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FOptimusEditorStyle::Get().GetBrush("TextEditor.Border"))
			.BorderBackgroundColor(FLinearColor::Black)
			[
				SNew(SGridPanel)
				.FillColumn(0, 1.0f)
				.FillRow(0, 1.0f)
				+SGridPanel::Slot(0, 0)
				[
					SAssignNew(ShaderEditor, SMultiLineEditableText)
					.Font(Font)
					.TextStyle(&TextStyle)
					.Text(this, &FOptimusShaderTextCustomization::GetShaderText)
					.AutoWrapText(false)
					.IsReadOnly(true)
					.Marshaller(SyntaxHighlighterMain)
					.HScrollBar(HorizontalScrollbar)
					.VScrollBar(VerticalScrollbar)
				]
				+SGridPanel::Slot(1, 0)
				[
					VerticalScrollbar.ToSharedRef()
				]
				+SGridPanel::Slot(0, 1)
				[
					HorizontalScrollbar.ToSharedRef()
				]
			]
		]
	];
}

FText FOptimusShaderTextCustomization::GetShaderText() const
{
	FString ShaderText;
	ShaderTextProperty->GetValue(ShaderText);
	return FText::FromString(ShaderText);
}

void FOptimusShaderTextCustomization::UpdateDiagnostics()
{
	TArray<const void *> RawData;
	DiagnosticsProperty->AccessRawData(RawData);
	if (ensure(RawData.Num() > 0))
	{
		const TArray<FOptimusCompilerDiagnostic>* DiagnosticsPtr = static_cast<const TArray<FOptimusCompilerDiagnostic>*>(RawData[0]);
		SyntaxHighlighterMain->SetCompilerMessages(*DiagnosticsPtr);

		if (ShaderEditor)
		{
			ShaderEditor->Refresh();
		}
	}	
}


void FOptimusShaderTextCustomization::OnPropertyChanged(UObject* InObject, FPropertyChangedEvent& InChangedEvent)
{
	if (InspectedObjects.Contains(InObject) && InChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FOptimusShaderText, Diagnostics))
	{
		UpdateDiagnostics();
	}
}

class SOptimusParameterBindingValueWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOptimusParameterBindingValueWidget) {}
	SLATE_END_ARGS()

	SOptimusParameterBindingValueWidget()
		: CustomizationUtils (nullptr)
	{
		
	}
	virtual void Construct(const FArguments& InArgs, TSharedRef<IPropertyHandle> InBindingPropertyHandle,  IPropertyTypeCustomizationUtils& InCustomizationUtils)
	{
		BindingPropertyHandle = InBindingPropertyHandle;
		CustomizationUtils = &InCustomizationUtils;
		
		const TSharedPtr<IPropertyHandle> DataTypeProperty = BindingPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusParameterBinding, DataType));
		const TSharedPtr<IPropertyHandle> DataDomainProperty = BindingPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusParameterBinding, DataDomain));	
	
		FDetailWidgetRow DataTypeHeaderRow;
		DataTypeRefCustomizationInstance = FOptimusDataTypeRefCustomization::MakeInstance();
		DataTypeRefCustomizationInstance->CustomizeHeader(DataTypeProperty.ToSharedRef(), DataTypeHeaderRow, InCustomizationUtils);

		FDetailWidgetRow DataDomainHeaderRow;
		DataDomainCustomizationInstance = FOptimusMultiLevelDataDomainCustomization::MakeInstance();
		DataDomainCustomizationInstance->CustomizeHeader(DataDomainProperty.ToSharedRef(), DataDomainHeaderRow, InCustomizationUtils);
		
		ColumnSizeData = MakeShared<FOptimusParameterBindingCustomization::FColumnSizeData>();

		ChildSlot
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			[
				SNew(SSplitter)
				.Style(FAppStyle::Get(), "DetailsView.Splitter")
				.PhysicalSplitterHandleSize(1.0f)
				.HitDetectionSplitterHandleSize(5.0f)
				+SSplitter::Slot()
				.Value(this, &SOptimusParameterBindingValueWidget::GetDataTypeColumnSize)
				.OnSlotResized(this, &SOptimusParameterBindingValueWidget::OnDataTypeColumnResized)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					// padding values grabbed from DetailWidgetConstants
					.Padding(0,0,10,0)
					[
						DataTypeHeaderRow.ValueContent().Widget
					]
				]
				+SSplitter::Slot()
				.Value(this, &SOptimusParameterBindingValueWidget::GetDataDomainColumnSize)
				.OnSlotResized(this, &SOptimusParameterBindingValueWidget::OnDataDomainColumnResized)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					// padding values grabbed from DetailWidgetConstants
					.Padding(12,0,10,0)
					[
						DataDomainHeaderRow.ValueContent().Widget
					]
				]
			]
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(2.0f,0.f)
			[
				PropertyCustomizationHelpers::MakeEmptyButton(
					FSimpleDelegate::CreateLambda([this]()
					{
						// This is copied from FPropertyEditor::DeleteItem()
						// This action must be deferred until next tick so that we avoid accessing invalid data before we have a chance to tick
						CustomizationUtils->GetPropertyUtilities()->EnqueueDeferredAction(
									FSimpleDelegate::CreateSP(this, &SOptimusParameterBindingValueWidget::OnDeleteItem)
						);
					}),
					LOCTEXT("OptimusParameterBindingRemoveButton", "Remove this Binding"))
			]
		];
	}

	void SetColumnSizeData(TSharedPtr<FOptimusParameterBindingCustomization::FColumnSizeData> InData)
	{
		ColumnSizeData = InData;
	};
	

private:
	void OnDeleteItem() const
	{
		TSharedPtr<IPropertyHandleArray> ArrayHandle = BindingPropertyHandle->GetParentHandle()->AsArray();
		TSharedPtr<FPropertyNode> PropertyNode = BindingPropertyHandle->GetPropertyNode(); 

		check(ArrayHandle.IsValid());

		int32 Index = PropertyNode->GetArrayIndex();

		if (ArrayHandle.IsValid())
		{
			ArrayHandle->DeleteItem(Index);
		}

		//In case the property is show in the favorite category refresh the whole tree
		if (PropertyNode->IsFavorite() || (PropertyNode->GetParentNode() != nullptr && PropertyNode->GetParentNode()->IsFavorite()))
		{
			CustomizationUtils->GetPropertyUtilities()->ForceRefresh();
		}
	};

	float GetDataTypeColumnSize() const {return ColumnSizeData->GetDataTypeColumnSize();}
	void OnDataTypeColumnResized(float InSize) const {ColumnSizeData->OnDataDomainColumnResized(InSize);}
	float GetDataDomainColumnSize() const {return ColumnSizeData->GetDataDomainColumnSize();}
	void OnDataDomainColumnResized(float InSize) const {ColumnSizeData ->OnDataDomainColumnResized(InSize);}
	
	TSharedPtr<IPropertyHandle> BindingPropertyHandle;
	IPropertyTypeCustomizationUtils* CustomizationUtils;

	TSharedPtr<IPropertyTypeCustomization> DataTypeRefCustomizationInstance;
	TSharedPtr<IPropertyTypeCustomization> DataDomainCustomizationInstance;
	
	TSharedPtr<FOptimusParameterBindingCustomization::FColumnSizeData> ColumnSizeData;
};


TSharedRef<IPropertyTypeCustomization> FOptimusParameterBindingCustomization::MakeInstance()
{
	return MakeShared<FOptimusParameterBindingCustomization>();
}

FOptimusParameterBindingCustomization::FOptimusParameterBindingCustomization()
{
	
}

void FOptimusParameterBindingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	TSharedRef<IPropertyHandle>& BindingPropertyHandle = InPropertyHandle;
	const TSharedPtr<IPropertyHandle> NameProperty = BindingPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusParameterBinding, Name));
	
	InHeaderRow
	.NameContent()
	.HAlign(HAlign_Fill)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.Padding(0,0,10,0)
		[
			NameProperty->CreatePropertyValueWidget()
		]
	]
	.ValueContent()
	.HAlign(HAlign_Fill)
	[
		SNew(SOptimusParameterBindingValueWidget, BindingPropertyHandle, InCustomizationUtils)
	];	
}

void FOptimusParameterBindingCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle,
	IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	FString Declaration;
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = InCustomizationUtils.GetPropertyUtilities()->GetSelectedObjects();
	for (TWeakObjectPtr<UObject> Object : SelectedObjects)
	{
		if (IOptimusParameterBindingProvider* BindingProvider = Cast<IOptimusParameterBindingProvider>(Object))
		{
			TArray<const void *> RawData;

			InPropertyHandle->AccessRawData(RawData);
			if (ensure(RawData.Num() > 0))
			{
				const FOptimusParameterBinding* Binding = static_cast<const FOptimusParameterBinding*>(RawData[0]);
				// During drag & reorder, we can have invalid bindings in the property
				if (Binding->Name != NAME_None)
				{
					Declaration = BindingProvider->GetBindingDeclaration(Binding->Name);
				}
			}
			break;
		}
	}

	if (!Declaration.IsEmpty())
	{
		FDetailWidgetRow& DeclarationRow = InChildBuilder.AddCustomRow(FText::GetEmpty());
		DeclarationRow
		.NameContent()
		[
			InPropertyHandle->CreatePropertyNameWidget(LOCTEXT("Declaration", "Declaration"))
		]
		.ValueContent()
		.HAlign(HAlign_Fill)
		[
			SNew(SBox)
			.MinDesiredWidth(180.0f)
			[
				SNew(SMultiLineEditableTextBox)
				.Text(FText::FromString(Declaration))
				.Font(FCoreStyle::GetDefaultFontStyle("Mono",InCustomizationUtils.GetRegularFont().Size))
				.IsReadOnly(true)
			]
		];		
	}
}

TSharedRef<FOptimusParameterBindingArrayBuilder> FOptimusParameterBindingArrayBuilder::MakeInstance(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	TSharedPtr<FOptimusParameterBindingCustomization::FColumnSizeData> InColumnSizeData)
{
	TSharedRef<FOptimusParameterBindingArrayBuilder> Builder = MakeShared<FOptimusParameterBindingArrayBuilder>(InPropertyHandle, InColumnSizeData);
	
	Builder->OnGenerateArrayElementWidget(
		FOnGenerateArrayElementWidget::CreateSP(Builder, &FOptimusParameterBindingArrayBuilder::OnGenerateEntry));
	return Builder;
}

FOptimusParameterBindingArrayBuilder::FOptimusParameterBindingArrayBuilder(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	TSharedPtr<FOptimusParameterBindingCustomization::FColumnSizeData> InColumnSizeData)
	: FDetailArrayBuilder(InPropertyHandle, true, false, true)
	, ArrayProperty(InPropertyHandle->AsArray())
	, ColumnSizeData(InColumnSizeData)
{
	if (!ColumnSizeData.IsValid())
	{
		ColumnSizeData = MakeShared<FOptimusParameterBindingCustomization::FColumnSizeData>();
	}
}

void FOptimusParameterBindingArrayBuilder::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
{
	// Do nothing since we don't want to show the "InnerArray" row, see FOptimusParameterBindingArrayCustomization::CustomizeHeader
}

void FOptimusParameterBindingArrayBuilder::GenerateWrapperStructHeaderRowContent(FDetailWidgetRow& NodeRow, TSharedRef<SWidget> NameContent)
{
	FDetailArrayBuilder::GenerateHeaderRowContent(NodeRow);
	NodeRow.ValueContent()
	.HAlign( HAlign_Left )
	.VAlign( VAlign_Center )
	// Value grabbed from SPropertyEditorArray::GetDesiredWidth
	.MinDesiredWidth(170.f)
	.MaxDesiredWidth(170.f);

	NodeRow.NameContent()
	[
		NameContent
	];
}


void FOptimusParameterBindingArrayBuilder::OnGenerateEntry(TSharedRef<IPropertyHandle> ElementProperty,
	int32 ElementIndex, IDetailChildrenBuilder& ChildrenBuilder) const
{
	IDetailPropertyRow& PropertyRow = ChildrenBuilder.AddProperty(ElementProperty);
	PropertyRow.ShowPropertyButtons(false);
	PropertyRow.ShouldAutoExpand(false);

	// Hide the reset to default button since it provides little value
	const FResetToDefaultOverride	ResetDefaultOverride = FResetToDefaultOverride::Create(TAttribute<bool>(false));
	PropertyRow.OverrideResetToDefault(ResetDefaultOverride);
	
	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<SWidget> ValueWidget;
	PropertyRow.GetDefaultWidgets( NameWidget, ValueWidget);
	PropertyRow.CustomWidget(true)
	.NameContent()
	.HAlign(HAlign_Fill)
	[
		NameWidget.ToSharedRef()
	]
	.ValueContent()
	.HAlign(HAlign_Fill)
	[
		ValueWidget.ToSharedRef()
	];
	
	const TSharedPtr<SHorizontalBox> HBox = StaticCastSharedPtr<SHorizontalBox>(ValueWidget);
	const TSharedPtr<SWidget> InnerValueWidget = HBox->GetSlot(0).GetWidget();
	const TSharedPtr<SOptimusParameterBindingValueWidget> OptimusValueWidget = StaticCastSharedPtr<SOptimusParameterBindingValueWidget>(InnerValueWidget);
	OptimusValueWidget->SetColumnSizeData(ColumnSizeData);
}

FOptimusParameterBindingArrayCustomization::FOptimusParameterBindingArrayCustomization()
	: ColumnSizeData(MakeShared<FOptimusParameterBindingCustomization::FColumnSizeData>())
{
}

void FOptimusParameterBindingArrayCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle,
                                                                 FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> ArrayHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusParameterBindingArray, InnerArray), false);
	
	ArrayBuilder = FOptimusParameterBindingArrayBuilder::MakeInstance(ArrayHandle.ToSharedRef(), ColumnSizeData);
	// use the top level property instead of "InnerArray"
	ArrayBuilder->GenerateWrapperStructHeaderRowContent(InHeaderRow,InPropertyHandle->CreatePropertyNameWidget());
}

void FOptimusParameterBindingArrayCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle,
                                                                   IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	InChildBuilder.AddCustomBuilder(ArrayBuilder.ToSharedRef());
}

FOptimusValueContainerCustomization::FOptimusValueContainerCustomization()
{
}

void FOptimusValueContainerCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	uint32 NumChildren = 0;
	InPropertyHandle->GetNumChildren(NumChildren);
	
	// During reordering, we may have zero children temporarily
	if (NumChildren > 0)
	{
		InnerPropertyHandle = InPropertyHandle->GetChildHandle(UOptimusValueContainerGeneratorClass::ValuePropertyName, true);

		if (ensure(InnerPropertyHandle.IsValid()))
		{
			InHeaderRow.NameContent()
			[
				InPropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				InnerPropertyHandle->CreatePropertyValueWidget()
			];
		}
	}
}

void FOptimusValueContainerCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle,
	IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	if (InnerPropertyHandle)
	{
		uint32 NumChildren = 0;
		InnerPropertyHandle->GetNumChildren(NumChildren)	;
		for (uint32 Index = 0; Index < NumChildren; Index++)
		{
			InChildBuilder.AddProperty(InnerPropertyHandle->GetChildHandle(Index).ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE
