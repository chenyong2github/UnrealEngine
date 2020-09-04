// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigCompilerDetails.h"
#include "Widgets/SWidget.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ControlRig.h"

#define LOCTEXT_NAMESPACE "ControlRigCompilerDetails"

void FRigVMCompileSettingsDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	HeaderRow
	.NameContent()
	[
		InStructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		InStructPropertyHandle->CreatePropertyValueWidget()
	];

	TArray<UObject*> Objects;
	InStructPropertyHandle->GetOuterObjects(Objects);
	ensure(Objects.Num() == 1); // This is in here to ensure we are only showing the modifier details in the blueprint editor

	for (UObject* Object : Objects)
	{
		if (Object->IsA<UControlRigBlueprint>())
		{
			BlueprintBeingCustomized = Cast<UControlRigBlueprint>(Object);
		}
	}
}

void FRigVMCompileSettingsDetails::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	if (InStructPropertyHandle->IsValidHandle())
	{
		uint32 NumChildren = 0;
		InStructPropertyHandle->GetNumChildren(NumChildren);

		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ChildIndex++)
		{
			StructBuilder.AddProperty(InStructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef());
		}

		StructBuilder.AddCustomRow(LOCTEXT("ASTTools", "AST Tools"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("AST")))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				[
					SNew(SButton)
					.OnClicked(this, &FRigVMCompileSettingsDetails::OnCopyASTClicked)
					.ContentPadding(FMargin(2))
					.Content()
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(LOCTEXT("CopyASTToClipboard", "Copy AST"))
					]
				]
				+ SVerticalBox::Slot()
				[
					SNew(SButton)
					.OnClicked(this, &FRigVMCompileSettingsDetails::OnCopyByteCodeClicked)
					.ContentPadding(FMargin(2))
					.Content()
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(LOCTEXT("CopyByteCodeToClipboard", "Copy ByteCode"))
					]
				]
			];
	}
}

FReply FRigVMCompileSettingsDetails::OnCopyASTClicked()
{
	if (BlueprintBeingCustomized)
	{
		if (BlueprintBeingCustomized->Model)
		{
			FString DotContent = BlueprintBeingCustomized->Model->GetRuntimeAST()->DumpDot();
			FPlatformApplicationMisc::ClipboardCopy(*DotContent);
		}
	}
	return FReply::Handled();
}

FReply FRigVMCompileSettingsDetails::OnCopyByteCodeClicked()
{
	if (BlueprintBeingCustomized)
	{
		if (BlueprintBeingCustomized->Model)
		{
			if(UControlRig* ControlRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
			{
				FString ByteCodeContent = ControlRig->GetVM()->DumpByteCodeAsText();
				FPlatformApplicationMisc::ClipboardCopy(*ByteCodeContent);
			}
		}
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
