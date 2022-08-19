// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDetailsCustomization.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IOptimusComponentBindingsProvider.h"
#include "IOptimusExecutionDomainProvider.h"
#include "IOptimusParameterBindingProvider.h"
#include "IPropertyTypeCustomization.h"
#include "OptimusBindingTypes.h"
#include "OptimusComputeDataInterface.h"
#include "OptimusDataTypeRegistry.h"
#include "OptimusDeformer.h"
#include "OptimusDeformerInstance.h"
#include "OptimusEditorStyle.h"
#include "OptimusHLSLSyntaxHighlighter.h"
#include "OptimusNode.h"
#include "OptimusResourceDescription.h"
#include "OptimusShaderText.h"
#include "OptimusSource.h"
#include "OptimusValidatedName.h"
#include "OptimusValueContainer.h"
#include "PropertyEditor/Private/PropertyNode.h"
#include "PropertyEditor/Public/IPropertyUtilities.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SOptimusDataTypeSelector.h"
#include "Widgets/SOptimusShaderTextDocumentTextBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"


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
	// Usage mask can change on a per-instance basis when the multi-level data domain field changes in a shader parameter binding
	auto GetUsageMask = [InPropertyHandle]()
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

		if (const FString* InstanceMetaData = InPropertyHandle->GetInstanceMetaData(FName(TEXT("UseInResource"))))
		{
			if (*InstanceMetaData == "True")
			{
				UsageMask |= EOptimusDataTypeUsageFlags::Resource;
			}
			else
			{
				UsageMask &= ~EOptimusDataTypeUsageFlags::Resource;
			}
		}
		if (const FString* InstanceMetaData = InPropertyHandle->GetInstanceMetaData(FName(TEXT("UseInVariable"))))
		{
			if (*InstanceMetaData == "True")
			{
				UsageMask |= EOptimusDataTypeUsageFlags::Variable;
			}
			else
			{
				UsageMask &= ~EOptimusDataTypeUsageFlags::Variable;
			}
		}
		if (const FString* InstanceMetaData = InPropertyHandle->GetInstanceMetaData(FName(TEXT("UseInAnimAttribute"))))
		{
			if (*InstanceMetaData == "True")
			{
				UsageMask |= EOptimusDataTypeUsageFlags::AnimAttributes;
			}
			else
			{
				UsageMask &= ~EOptimusDataTypeUsageFlags::AnimAttributes;
			}
		}
		
		return UsageMask;
	};


	TypeNameProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusDataTypeRef, TypeName));
	TypeObjectProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusDataTypeRef, TypeObject));

	InHeaderRow
	.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SOptimusDataTypeSelector)
		.CurrentDataType(this, &FOptimusDataTypeRefCustomization::GetCurrentDataType)
		.UsageMask_Lambda(GetUsageMask)
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
	if (!InDataType.IsValid())
	{
		// Do not accept invalid input
		return;
	}
	FScopedTransaction Transaction(LOCTEXT("SetDataType", "Set Data Type"));
	CurrentDataType = InDataType;
	
	// We have to change the object property first
	// because by the time we change the type name,
	// owner of the property might use the data type ref to construct the default value container, 
	// at which point we have to make sure the type ref is complete
	TypeObjectProperty->SetValue(InDataType.IsValid() ? InDataType->TypeObject.Get() : nullptr);
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

TSharedRef<IPropertyTypeCustomization> FOptimusExecutionDomainCustomization::MakeInstance()
{
	return MakeShared<FOptimusExecutionDomainCustomization>();
}


FOptimusExecutionDomainCustomization::FOptimusExecutionDomainCustomization()
{
}


void FOptimusExecutionDomainCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow,
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
{
	TSharedPtr<IPropertyHandle> ContextNameProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusExecutionDomain, Name));

	TArray<UObject*> OwningObjects;
	InPropertyHandle->GetOuterObjects(OwningObjects);

	// FIXME: Support multiple objects.
	const IOptimusExecutionDomainProvider* ExecutionDomainProvider = Cast<IOptimusExecutionDomainProvider>(OwningObjects[0]);
	ContextNames.Reset();
	if (ExecutionDomainProvider)
	{
		ContextNames = ExecutionDomainProvider->GetExecutionDomains();
	}
	else
	{
		ContextNames.Add(NAME_None);
	}
	
	InHeaderRow.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SComboBox<FName>)
			.ToolTipText(LOCTEXT("ExecContextListerToolTip", "Select an execution context from the list of available contexts."))
			.OptionsSource(&ContextNames)
			.IsEnabled_Lambda([InPropertyHandle]() -> bool
			{
				return InPropertyHandle->IsEditable();
			})
			.OnGenerateWidget_Lambda([](FName InName)
			{
				const FText NameText = InName.IsNone() ? LOCTEXT("NoneName", "<None>") : FText::FromName(InName);
				return SNew(STextBlock)
					.Text(NameText)
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
					FName Name;
					ContextNameProperty->GetValue(Name);
					return Name.IsNone() ? LOCTEXT("NoneName", "<None>") : FText::FromName(Name);
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
	GenerateContextNames();
}


void FOptimusMultiLevelDataDomainCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow,
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
{
	static const TArray<TSharedRef<FString>> Multipliers = {
		MakeShared<FString>(TEXT("x1")),
		MakeShared<FString>(TEXT("x2")),
		MakeShared<FString>(TEXT("x3")),
		MakeShared<FString>(TEXT("x4")),
		MakeShared<FString>(TEXT("x8")),
	};	
	
	auto FormatNames = [](const TArray<FName>& InNames) -> FText
	{
		if (InNames.IsEmpty())
		{
			return LOCTEXT("ParameterEntry", "Parameter Value");
		}
		
		TArray<FText> NameParts;
		for (FName Name: InNames)
		{
			NameParts.Add(FText::FromName(Name));
		}
		return FText::Join(FText::FromString(UTF8TEXT(" › ")), NameParts);
	};

	auto TryGetSingleValue = [InPropertyHandle](TArray<FName> &OutNames) -> bool
	{
		TArray<const void *> RawDataPtrs;
		InPropertyHandle->AccessRawData(RawDataPtrs);

		bool bItemsAreAllSame = true;
		for (const void* RawPtr: RawDataPtrs)
		{
			const FOptimusMultiLevelDataDomain* DataDomain = static_cast<const FOptimusMultiLevelDataDomain*>(RawPtr);
			// During drag & reorder, invalid binding can be created temporarily
			if (DataDomain)
			{
				if (OutNames.IsEmpty())
				{
					OutNames = DataDomain->LevelNames;
				}
				else if (OutNames != DataDomain->LevelNames)
				{
					bItemsAreAllSame = false;
					break;
				}
			}
		}
		return bItemsAreAllSame;
	};

	TArray<FName> CurrentValue;
	TryGetSingleValue(CurrentValue);
	// Broadcast for the initial value, so that outer detail customization can adjust the usage flags accordingly
	OnMultiLevelDataDomainChangedDelegate.Broadcast(CurrentValue);
	
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
					.Font(InNames->IsEmpty() ? IPropertyTypeCustomizationUtils::GetBoldFont() : IPropertyTypeCustomizationUtils::GetRegularFont());
			})
			.OnSelectionChanged_Lambda([InPropertyHandle, this](TSharedPtr<TArray<FName>> InNames, ESelectInfo::Type)
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
				OnMultiLevelDataDomainChangedDelegate.Broadcast(*InNames.Get());
			})
			[
				SNew(STextBlock)
				.Font(IPropertyTypeCustomizationUtils::GetRegularFont())
				.Text_Lambda([TryGetSingleValue, FormatNames]()
				{
					TArray<FName> Names;
					if (TryGetSingleValue(Names))
					{
						return FormatNames(Names);
					}
					else
					{
						return FText::FromString(TEXT("---"));
					}
				})
			]
	];
}


void FOptimusMultiLevelDataDomainCustomization::SetAllowParameters(const bool bInAllowParameters)
{
	if (bInAllowParameters != bAllowParameters)
	{
		bAllowParameters = bInAllowParameters;
		GenerateContextNames();
	}
}


void FOptimusMultiLevelDataDomainCustomization::GenerateContextNames()
{
	NestedContextNames.Reset();

	if (bAllowParameters)
	{
		// Add an empty set of names. We format it specifically above.
		NestedContextNames.Add(MakeShared<TArray<FName>>());
	}
	
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


// =============================================================================================

TSharedRef<IPropertyTypeCustomization> FOptimusShaderTextCustomization::MakeInstance()
{
	return MakeShared<FOptimusShaderTextCustomization>();
}

FOptimusShaderTextCustomization::FOptimusShaderTextCustomization() :
	SyntaxHighlighter(FOptimusHLSLSyntaxHighlighter::Create())
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

	const FText ShaderTextTitle = LOCTEXT("OptimusShaderTextTitle", "Shader Text");

	InHeaderRow
	.WholeRowContent()
	[
		SAssignNew(ExpandableArea, SExpandableArea)
		.AreaTitle(ShaderTextTitle)
		.InitiallyCollapsed(true)
		.AllowAnimatedTransition(false)
		.BodyContent()
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
						.Marshaller(SyntaxHighlighter)
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
		]
	];
}

FText FOptimusShaderTextCustomization::GetShaderText() const
{
	FString ShaderText;
	ShaderTextProperty->GetValue(ShaderText);
	return FText::FromString(ShaderText);
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
		
		TSharedPtr<IPropertyHandle> DataTypeProperty = BindingPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusParameterBinding, DataType));
		const TSharedPtr<IPropertyHandle> DataDomainProperty = BindingPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusParameterBinding, DataDomain));	
	
		FDetailWidgetRow DataTypeHeaderRow;
		DataTypeRefCustomizationInstance = FOptimusDataTypeRefCustomization::MakeInstance();
		DataTypeRefCustomizationInstance->CustomizeHeader(DataTypeProperty.ToSharedRef(), DataTypeHeaderRow, InCustomizationUtils);

		FDetailWidgetRow DataDomainHeaderRow;
		DataDomainCustomizationInstance = FOptimusMultiLevelDataDomainCustomization::MakeInstance();
		StaticCastSharedPtr<FOptimusMultiLevelDataDomainCustomization>(DataDomainCustomizationInstance)
			->OnMultiLevelDataDomainChangedDelegate.AddLambda([DataTypeProperty](const TArray<FName>& InDataDomain)
			{
				if (InDataDomain.IsEmpty())
				{
					DataTypeProperty->SetInstanceMetaData(FName(TEXT("UseInAnimAttribute")), TEXT("True"));
					DataTypeProperty->SetInstanceMetaData(FName(TEXT("UseInVariable")), TEXT("True"));
					DataTypeProperty->SetInstanceMetaData(FName(TEXT("UseInResource")), TEXT("False"));
				}
				else
				{
					DataTypeProperty->SetInstanceMetaData(FName(TEXT("UseInAnimAttribute")), TEXT("False"));
					DataTypeProperty->SetInstanceMetaData(FName(TEXT("UseInVariable")), TEXT("False"));
					DataTypeProperty->SetInstanceMetaData(FName(TEXT("UseInResource")), TEXT("True"));
				}
			});
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

	void SetAllowParameters(const bool bInAllowParameters)
	{
		TSharedPtr<FOptimusMultiLevelDataDomainCustomization> DataDomainCustomization = StaticCastSharedPtr<FOptimusMultiLevelDataDomainCustomization>(DataDomainCustomizationInstance);
		DataDomainCustomization->SetAllowParameters(bInAllowParameters);
	}
	

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

void FOptimusParameterBindingCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow,
	IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	TSharedRef<IPropertyHandle>& BindingPropertyHandle = InPropertyHandle;
	const TSharedPtr<IPropertyHandle> ValidatedNameProperty = BindingPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusParameterBinding, Name));
	const TSharedPtr<IPropertyHandle> NameProperty = ValidatedNameProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusValidatedName, Name));
	
	InHeaderRow
	.NameContent()
	.HAlign(HAlign_Fill)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.Padding(0,0,10,0)
		[
			SNew(SEditableTextBox)
			.Font(InCustomizationUtils.GetRegularFont())
			.Text_Lambda([NameProperty]()
			{
				FName Value;
				NameProperty->GetValue(Value);
				return FText::FromName(Value);
			})
			.OnTextCommitted_Lambda([NameProperty](const FText& InText, ETextCommit::Type InTextCommit)
			{
				NameProperty->SetValue(FName(InText.ToString()));
			})
			.OnVerifyTextChanged_Lambda([NameProperty](const FText& InNewText, FText& OutErrorMessage) -> bool
			{
				if (InNewText.IsEmpty())
				{
					OutErrorMessage = LOCTEXT("NameEmpty", "Name can't be empty.");
					return false;
				}
					
				FText FailureContext = LOCTEXT("NameFailure", "Name");
				if (!FOptimusValidatedName::IsValid(InNewText.ToString(), &OutErrorMessage, &FailureContext))
				{
					return false;
				}
					
				return true;
			})
		]
	]
	.ValueContent()
	.HAlign(HAlign_Fill)
	[
		SNew(SOptimusParameterBindingValueWidget, BindingPropertyHandle, InCustomizationUtils)
	];	
}

void FOptimusParameterBindingCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	IDetailChildrenBuilder& InChildBuilder,
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
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
					if (Binding->DataType->ShaderValueType.IsValid())
					{
						Declaration = BindingProvider->GetBindingDeclaration(Binding->Name);
					}
					else
					{
						Declaration = FString::Printf(TEXT("Type is not supported"));
					}
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
	TSharedPtr<FOptimusParameterBindingCustomization::FColumnSizeData> InColumnSizeData,
	const bool bInAllowParameters
	)
{
	TSharedRef<FOptimusParameterBindingArrayBuilder> Builder = MakeShared<FOptimusParameterBindingArrayBuilder>(InPropertyHandle, InColumnSizeData, bInAllowParameters);
	
	Builder->OnGenerateArrayElementWidget(
		FOnGenerateArrayElementWidget::CreateSP(Builder, &FOptimusParameterBindingArrayBuilder::OnGenerateEntry));
	return Builder;
}

FOptimusParameterBindingArrayBuilder::FOptimusParameterBindingArrayBuilder(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	TSharedPtr<FOptimusParameterBindingCustomization::FColumnSizeData> InColumnSizeData,
	const bool bInAllowParameters
	) : FDetailArrayBuilder(InPropertyHandle, true, false, true),
		ArrayProperty(InPropertyHandle->AsArray()),
		ColumnSizeData(InColumnSizeData),
		bAllowParameters(bInAllowParameters)
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


void FOptimusParameterBindingArrayBuilder::OnGenerateEntry(
	TSharedRef<IPropertyHandle> ElementProperty,
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
	OptimusValueWidget->SetAllowParameters(bAllowParameters);
}

FOptimusParameterBindingArrayCustomization::FOptimusParameterBindingArrayCustomization()
	: ColumnSizeData(MakeShared<FOptimusParameterBindingCustomization::FColumnSizeData>())
{
}

void FOptimusParameterBindingArrayCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle,
                                                                 FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	const bool bAllowParameters = InPropertyHandle->HasMetaData(UOptimusNode::PropertyMeta::AllowParameters);
	const TSharedPtr<IPropertyHandle> ArrayHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusParameterBindingArray, InnerArray), false);

	ArrayBuilder = FOptimusParameterBindingArrayBuilder::MakeInstance(ArrayHandle.ToSharedRef(), ColumnSizeData, bAllowParameters);
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


FOptimusValidatedNameCustomization::FOptimusValidatedNameCustomization()
{
}

TSharedRef<IPropertyTypeCustomization> FOptimusValidatedNameCustomization::MakeInstance()
{
	return MakeShared<FOptimusValidatedNameCustomization>();
}

void FOptimusValidatedNameCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow,
	IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> NameProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusValidatedName, Name));
	
	InHeaderRow
	.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SEditableTextBox)
		.Font(InCustomizationUtils.GetRegularFont())
		.Text_Lambda([NameProperty]()
		{
			FName Value;
			NameProperty->GetValue(Value);
			return FText::FromName(Value);
		})
		.OnTextCommitted_Lambda([NameProperty](const FText& InText, ETextCommit::Type InTextCommit)
		{
			NameProperty->SetValue(FName(InText.ToString()));
		})
		.OnVerifyTextChanged_Lambda([NameProperty](const FText& InNewText, FText& OutErrorMessage) -> bool
		{
			if (InNewText.IsEmpty())
			{
				OutErrorMessage = LOCTEXT("NameEmpty", "Name can't be empty.");
				return false;
			}
				
			FText FailureContext = LOCTEXT("NameFailure", "Name");
			if (!FOptimusValidatedName::IsValid(InNewText.ToString(), &OutErrorMessage, &FailureContext))
			{
				return false;
			}
				
			return true;
		})
	];
}


FOptimusSourceDetailsCustomization::FOptimusSourceDetailsCustomization()
	: SyntaxHighlighter(FOptimusHLSLSyntaxHighlighter::Create())
{
}

TSharedRef<IDetailCustomization> FOptimusSourceDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FOptimusSourceDetailsCustomization);
}

void FOptimusSourceDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() > 1)
	{
		return;
	}
	OptimusSource = Cast<UOptimusSource>(ObjectsBeingCustomized[0].Get());
	if (OptimusSource == nullptr)
	{
		return;
	}

	TSharedRef<IPropertyHandle> SourcePropertyHandle = DetailBuilder.GetProperty(TEXT("SourceText"));
	DetailBuilder.EditDefaultProperty(SourcePropertyHandle)->CustomWidget()
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(SourceTextBox, SOptimusShaderTextDocumentTextBox)
			.Text(this, &FOptimusSourceDetailsCustomization::GetText)
			.IsReadOnly(false)
			.Marshaller(SyntaxHighlighter)
			.OnTextChanged(this, &FOptimusSourceDetailsCustomization::OnTextChanged)
		]
	];
}

FText FOptimusSourceDetailsCustomization::GetText() const
{
	return FText::FromString(OptimusSource->GetSource());
}

void FOptimusSourceDetailsCustomization::OnTextChanged(const FText& InValue)
{
	OptimusSource->SetSource(InValue.ToString());
}


FOptimusComponentSourceBindingDetailsCustomization::FOptimusComponentSourceBindingDetailsCustomization()
{
}

TSharedRef<IDetailCustomization> FOptimusComponentSourceBindingDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FOptimusComponentSourceBindingDetailsCustomization);
}

void FOptimusComponentSourceBindingDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() > 1)
	{
		return;
	}
	OptimusSourceBinding = Cast<UOptimusComponentSourceBinding>(ObjectsBeingCustomized[0].Get());
	if (OptimusSourceBinding == nullptr)
	{
		return;
	}

	// Collect and sort ComponentSources for combo box.
	UOptimusComponentSource* CurrentSource = OptimusSourceBinding->ComponentType->GetDefaultObject<UOptimusComponentSource>();
	TSharedPtr<FString> CurrentSelection;
	for (const UOptimusComponentSource* Source : UOptimusComponentSource::GetAllSources())
	{
		if (!OptimusSourceBinding->IsPrimaryBinding() || Source->IsUsableAsPrimarySource())
		{
			TSharedPtr<FString> SourceName = MakeShared<FString>(Source->GetDisplayName().ToString());
			if (Source == CurrentSource)
			{
				CurrentSelection = SourceName;
			}
			ComponentSources.Add(SourceName);
		}
	}
	Algo::Sort(ComponentSources, [](TSharedPtr<FString> ItemA, TSharedPtr<FString> ItemB)
	{
		return ItemA->Compare(*ItemB) < 0;
	});

	TSharedRef<IPropertyHandle> SourcePropertyHandle = DetailBuilder.GetProperty(TEXT("ComponentType"));
	DetailBuilder.EditDefaultProperty(SourcePropertyHandle)->ShowPropertyButtons(false)
	.CustomWidget()
	.OverrideResetToDefault(FResetToDefaultOverride::Hide())
	.NameContent()
	[
		SourcePropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(STextComboBox)
			.OptionsSource(&ComponentSources)
			.InitiallySelectedItem(CurrentSelection)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.OnSelectionChanged(this, &FOptimusComponentSourceBindingDetailsCustomization::ComponentSourceChanged)
	];
}

void FOptimusComponentSourceBindingDetailsCustomization::ComponentSourceChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
	for (const UOptimusComponentSource* Source : UOptimusComponentSource::GetAllSources())
	{
		if (*Selection == Source->GetDisplayName().ToString())
		{
			UOptimusDeformer* Deformer = OptimusSourceBinding->GetOwningDeformer();
			Deformer->SetComponentBindingSource(OptimusSourceBinding, Source);
			return;
		}
	}
}


TSharedRef<IPropertyTypeCustomization> FOptimusDeformerInstanceComponentBindingCustomization::MakeInstance()
{
	return MakeShareable(new FOptimusDeformerInstanceComponentBindingCustomization);
}

FOptimusDeformerInstanceComponentBindingCustomization::FOptimusDeformerInstanceComponentBindingCustomization()
{
}

FOptimusDeformerInstanceComponentBindingCustomization::~FOptimusDeformerInstanceComponentBindingCustomization()
{
}

void FOptimusDeformerInstanceComponentBindingCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow,
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
{
	const TSharedPtr<IPropertyHandle> NameProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusDeformerInstanceComponentBinding, ProviderName));
	const TSharedPtr<IPropertyHandle> ComponentProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusDeformerInstanceComponentBinding, ActorComponent));
	
	FName BindingName;
	NameProperty->GetValue(BindingName);

	UObject* SelectedComponent;
	ComponentProperty->GetValue(SelectedComponent);
	
	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);

	const IOptimusComponentBindingsProvider* BindingProvider = Cast<IOptimusComponentBindingsProvider>(OuterObjects[0]);
	const UOptimusComponentSourceBinding* Binding = nullptr;

	FComponentHandle SelectedComponentHandle;

	if (BindingProvider)
	{
		const AActor* OwningActor = BindingProvider->GetActor();
		Binding = BindingProvider->GetComponentBindingByName(BindingName);
		
		if (OwningActor && Binding)
		{
			TArray<UActorComponent*> FilteredComponents;
			OwningActor->GetComponents(Binding->GetComponentSource()->GetComponentClass(), FilteredComponents);
			for (const UActorComponent* Component: FilteredComponents)
			{
				ComponentHandles.Add(MakeShared<FSoftObjectPath>(FSoftObjectPath::GetOrCreateIDForObject(Component)));
				if (Component == SelectedComponent)
				{
					SelectedComponentHandle = ComponentHandles.Last();
				}
			}
		}
	}

	InHeaderRow
	.NameContent()
	[
		NameProperty->CreatePropertyNameWidget(FText::FromName(BindingName))
	]
	.ValueContent()
	[
		SNew(SComboBox<FComponentHandle>)
		.IsEnabled(Binding)
		.OptionsSource(&ComponentHandles)
		.InitiallySelectedItem(SelectedComponentHandle)
		.OnGenerateWidget_Lambda([](const FComponentHandle InComponentHandle)
		{
			const UActorComponent* Component = Cast<UActorComponent>(InComponentHandle->ResolveObject());
			
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FSlateIconFinder::FindIconBrushForClass(Component ? Component->GetClass() : nullptr, TEXT("SCS.Component")))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0)
				.Padding(2.0f, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromName(Component ? Component->GetFName() : FName("<Invalid>")))
				];
		})
		.OnSelectionChanged_Lambda([ComponentProperty](const FComponentHandle InComponentHandle, ESelectInfo::Type InInfo)
		{
			if (InInfo != ESelectInfo::Direct)
			{
				const UActorComponent* Component = Cast<UActorComponent>(InComponentHandle->ResolveObject());
				ComponentProperty->SetValue(Component);
			}
		})
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				.Image_Lambda([ComponentProperty]()-> const FSlateBrush*
				{
					if (UObject* ComponentObject = nullptr; ComponentProperty->GetValue(ComponentObject) == FPropertyAccess::Success && ComponentObject)
					{
						return FSlateIconFinder::FindIconBrushForClass(ComponentObject->GetClass(), TEXT("SCS.Component"));
					}
					return nullptr;
				})
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1.0)
			.Padding(2.0f, 0, 0, 0)
			[
				SNew(STextBlock)
				.Font(IPropertyTypeCustomizationUtils::GetRegularFont())
				.Text_Lambda([ComponentProperty]()
				{
					if (UObject* ComponentObject = nullptr; ComponentProperty->GetValue(ComponentObject) == FPropertyAccess::Success && ComponentObject)
					{
						return FText::FromName(ComponentObject->GetFName());
					}
					return FText::GetEmpty();
				})
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
