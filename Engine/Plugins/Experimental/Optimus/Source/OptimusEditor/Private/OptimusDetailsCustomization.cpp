// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDetailsCustomization.h"

#include "OptimusEditorStyle.h"
#include "OptimusHLSLSyntaxHighlighter.h"
#include "SOptimusDataTypeSelector.h"

#include "OptimusDataTypeRegistry.h"
#include "OptimusShaderText.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "DetailWidgetRow.h"
#include "IPropertyTypeCustomization.h"
#include "IDetailChildrenBuilder.h"
#include "OptimusComputeDataInterface.h"
#include "OptimusResourceDescription.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SComboBox.h"
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
	EOptimusDataTypeUsageFlags UsageMask = EOptimusDataTypeUsageFlags::None;
	
	if (InPropertyHandle->HasMetaData(FName(TEXT("UseInResource"))))
	{
		UsageMask |= EOptimusDataTypeUsageFlags::Resource;
	}
	if (InPropertyHandle->HasMetaData(FName(TEXT("UseInVariable"))))
	{
		UsageMask |= EOptimusDataTypeUsageFlags::Variable;
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

// The current tab width for the editor.
static constexpr int32 GTabWidth = 4;


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
		.VAlign(VAlign_Top)
		.AutoHeight()
		[
			InPropertyHandle->CreatePropertyNameWidget()
		]
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
					SNew(SScrollBox)
					.Orientation(Orient_Vertical)
					.ExternalScrollbar(VerticalScrollbar)
					+ SScrollBox::Slot()
					[
						SNew(STextBlock)
						.Font(InCustomizationUtils.GetBoldFont())
						.Text(LOCTEXT("OptimusType_ShaderTextCustomization_Decl", "Declarations:"))
						.Margin(FMargin(0, 3, 0, 0))
					]
					+ SScrollBox::Slot()
					[
						SNew(SMultiLineEditableText)
						.Font(Font)
						.TextStyle(&TextStyle)
						.Text(this, &FOptimusShaderTextCustomization::GetDeclarationsText)
						.Marshaller(SyntaxHighlighter)
						.HScrollBar(HorizontalScrollbar)
						.AutoWrapText(false)
						.IsReadOnly(true)
					]
					+ SScrollBox::Slot()
					[
						SNew(SSeparator)
					]
					+ SScrollBox::Slot()
					[
						SNew(STextBlock)
						.Font(InCustomizationUtils.GetBoldFont())
						.Text(LOCTEXT("OptimusType_ShaderTextCustomization_Src", "Compute Kernel Source:"))
						.Margin(FMargin(0, 3, 0, 0))
					]
					+ SScrollBox::Slot()
					[
						SAssignNew(ShaderEditor, SMultiLineEditableText)
						.Font(Font)
						.TextStyle(&TextStyle)
						.Text(this, &FOptimusShaderTextCustomization::GetShaderText)
						.OnTextChanged(this, &FOptimusShaderTextCustomization::OnShaderTextChanged)
						// By default, the Tab key gets routed to "next widget". We want to disable that behaviour.
						.OnIsTypedCharValid_Lambda([](const TCHAR InChar) { return true; })
						.OnKeyCharHandler(this, &FOptimusShaderTextCustomization::OnShaderTextKeyChar)
						.AutoWrapText(false)
						.Marshaller(SyntaxHighlighterMain)
						.HScrollBar(HorizontalScrollbar)
					]
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


FText FOptimusShaderTextCustomization::GetDeclarationsText() const
{
	FString Preamble;
	DeclarationsProperty->GetValue(Preamble);
	return FText::FromString(Preamble);
}


FText FOptimusShaderTextCustomization::GetShaderText() const
{
	FString ShaderText;
	ShaderTextProperty->GetValue(ShaderText);
	return FText::FromString(ShaderText);
}


void FOptimusShaderTextCustomization::OnShaderTextChanged(const FText& InText)
{
	ShaderTextProperty->SetValue(InText.ToString());
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


FReply FOptimusShaderTextCustomization::OnShaderTextKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	if (ShaderEditor->IsTextReadOnly())
	{
		return FReply::Unhandled();
	}

	const TCHAR Character = InCharacterEvent.GetCharacter();
	if (Character == TEXT('\t'))
	{
		// Tab to nearest 4.
		ShaderEditor->InsertTextAtCursor(TEXT("    "));
		return FReply::Handled();
	}
	else if (Character == TEXT('\n'))
	{
		// Figure out if we need to auto-indent.
		FString CurrentLine;
		ShaderEditor->GetCurrentTextLine(CurrentLine);

		// See what the open/close curly brace balance is.
		int32 BraceBalance = 0;
		for (TCHAR Char : CurrentLine)
		{
			BraceBalance += (Char == TEXT('{'));
			BraceBalance -= (Char == TEXT('}'));
		}

		return FReply::Handled();
	}
	else
	{
		// Let SMultiLineEditableText::OnKeyChar handle it.
		return FReply::Unhandled();
	}
}


#undef LOCTEXT_NAMESPACE
