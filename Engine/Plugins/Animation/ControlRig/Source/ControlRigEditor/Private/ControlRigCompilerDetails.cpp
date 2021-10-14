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
#include "IPropertyUtilities.h"
#include "IPythonScriptPlugin.h"
#include "RigVMPythonUtils.h"
#include "ControlRigVisualGraphUtils.h"
#include "ControlRig/Private/Units/Execution/RigUnit_BeginExecution.h"

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

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

		StructBuilder.AddCustomRow(LOCTEXT("MemoryInspection", "Memory Inspection"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Memory Inspection")))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				[
					SNew(SButton)
					.OnClicked(this, &FRigVMCompileSettingsDetails::OnInspectMemory, ERigVMMemoryType::Literal)
					.ContentPadding(FMargin(2))
					.Content()
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(LOCTEXT("InspectLiteralMemory", "Inspect Literal Memory"))
					]
				]
				+ SVerticalBox::Slot()
				[
					SNew(SButton)
					.OnClicked(this, &FRigVMCompileSettingsDetails::OnInspectMemory, ERigVMMemoryType::Work)
					.ContentPadding(FMargin(2))
					.Content()
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(LOCTEXT("InspectWorkMemory", "Inspect Work Memory"))
					]
				]
			];
#endif

		StructBuilder.AddCustomRow(LOCTEXT("DebuggingTools", "Debugging Tools"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Debugging")))
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
						.Text(LOCTEXT("CopyASTToClipboard", "Copy AST Graph"))
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
				+ SVerticalBox::Slot()
				[
					SNew(SButton)
					.OnClicked(this, &FRigVMCompileSettingsDetails::OnCopyHierarchyGraphClicked)
					.ContentPadding(FMargin(2))
					.Content()
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(LOCTEXT("CopyHierarchyGraphToClipboard", "Copy Hierarchy Graph"))
					]
				]
			];
	}
}

#if !UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED

FReply FRigVMCompileSettingsDetails::OnInspectMemory(ERigVMMemoryType InMemoryType)
{
	if (BlueprintBeingCustomized)
	{
		if(UControlRig* DebuggedRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
		{
			if(URigVMMemoryStorage* MemoryStorage = DebuggedRig->GetVM()->GetMemoryByType(InMemoryType))
			{
				TArray<UObject*> ObjectsToSelect = {MemoryStorage};
				BlueprintBeingCustomized->RequestInspectObject(ObjectsToSelect);
			}
		}
	}
	return FReply::Handled();
}

#endif

FReply FRigVMCompileSettingsDetails::OnCopyASTClicked()
{
	if (BlueprintBeingCustomized)
	{
		if (BlueprintBeingCustomized->GetModel())
		{
			FString DotContent = BlueprintBeingCustomized->GetModel()->GetRuntimeAST()->DumpDot();
			FPlatformApplicationMisc::ClipboardCopy(*DotContent);
		}
	}
	return FReply::Handled();
}

FReply FRigVMCompileSettingsDetails::OnCopyByteCodeClicked()
{
	if (BlueprintBeingCustomized)
	{
		if (BlueprintBeingCustomized->GetModel())
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

FReply FRigVMCompileSettingsDetails::OnCopyHierarchyGraphClicked()
{
	if (BlueprintBeingCustomized)
	{
		if(UControlRig* ControlRig = Cast<UControlRig>(BlueprintBeingCustomized->GetObjectBeingDebugged()))
		{
			FName EventName = FRigUnit_BeginExecution::EventName;
			if(!ControlRig->GetEventQueue().IsEmpty())
			{
				EventName = ControlRig->GetEventQueue()[0];
			}
			
			const FString DotGraphContent = FControlRigVisualGraphUtils::DumpRigHierarchyToDotGraph(ControlRig->GetHierarchy(), EventName);
			FPlatformApplicationMisc::ClipboardCopy(*DotGraphContent);
		}
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
