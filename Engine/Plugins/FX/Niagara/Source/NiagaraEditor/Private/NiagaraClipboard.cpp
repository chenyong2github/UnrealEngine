// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraClipboard.h"
#include "NiagaraDataInterface.h"

#include "Factories.h"
#include "UObject/UObjectMarks.h"
#include "UObject/PropertyPortFlags.h"
#include "Containers/UnrealString.h"
#include "UnrealExporter.h"
#include "Exporters/Exporter.h"
#include "HAL/PlatformApplicationMisc.h"

struct FNiagaraClipboardContentTextObjectFactory : public FCustomizableTextObjectFactory
{
public:
	UNiagaraClipboardContent* ClipboardContent;

public:
	FNiagaraClipboardContentTextObjectFactory()
		: FCustomizableTextObjectFactory(GWarn)
		, ClipboardContent(nullptr)
	{
	}

protected:
	virtual bool CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const override
	{
		return ObjectClass == UNiagaraClipboardContent::StaticClass();
	}

	virtual void ProcessConstructedObject(UObject* CreatedObject) override
	{
		if (CreatedObject->IsA<UNiagaraClipboardContent>())
		{
			ClipboardContent = CastChecked<UNiagaraClipboardContent>(CreatedObject);
		}
	}
};

UNiagaraClipboardFunctionInput* MakeNewInput(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, ENiagaraClipboardFunctionInputValueMode InValueMode)
{
	UNiagaraClipboardFunctionInput* NewInput = Cast<UNiagaraClipboardFunctionInput>(NewObject<UNiagaraClipboardFunctionInput>(InOuter));
	NewInput->InputName = InInputName;
	NewInput->InputType = InInputType;
	NewInput->ValueMode = InValueMode;
	return NewInput;
}

const UNiagaraClipboardFunctionInput* UNiagaraClipboardFunctionInput::CreateLocalValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, TArray<uint8>& InLocalValueData)
{
	checkf(InLocalValueData.Num() == InInputType.GetSize(), TEXT("Input data size didn't match type size."))
	UNiagaraClipboardFunctionInput* NewInput = MakeNewInput(InOuter, InInputName, InInputType, ENiagaraClipboardFunctionInputValueMode::Local);
	NewInput->Local = InLocalValueData;
	return NewInput;
}

const UNiagaraClipboardFunctionInput* UNiagaraClipboardFunctionInput::CreateLinkedValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, FName InLinkedValue)
{
	UNiagaraClipboardFunctionInput* NewInput = MakeNewInput(InOuter, InInputName, InInputType, ENiagaraClipboardFunctionInputValueMode::Linked);
	NewInput->Linked = InLinkedValue;
	return NewInput;
}

const UNiagaraClipboardFunctionInput* UNiagaraClipboardFunctionInput::CreateDataValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, UNiagaraDataInterface* InDataValue)
{
	UNiagaraClipboardFunctionInput* NewInput = MakeNewInput(InOuter, InInputName, InInputType, ENiagaraClipboardFunctionInputValueMode::Data);
	NewInput->Data = NewObject<UNiagaraDataInterface>(NewInput, InDataValue->GetClass());
	InDataValue->CopyTo(NewInput->Data);
	return NewInput;
}

const UNiagaraClipboardFunctionInput* UNiagaraClipboardFunctionInput::CreateExpressionValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, const FString& InExpressionValue)
{
	UNiagaraClipboardFunctionInput* NewInput = MakeNewInput(InOuter, InInputName, InInputType, ENiagaraClipboardFunctionInputValueMode::Expression);
	NewInput->Expression = InExpressionValue;
	return NewInput;
}

const UNiagaraClipboardFunctionInput* UNiagaraClipboardFunctionInput::CreateDynamicValue(UObject* InOuter, FName InInputName, FNiagaraTypeDefinition InInputType, UNiagaraScript* InDynamicValue)
{
	UNiagaraClipboardFunctionInput* NewInput = MakeNewInput(InOuter, InInputName, InInputType, ENiagaraClipboardFunctionInputValueMode::Dynamic);
	NewInput->Dynamic = UNiagaraClipboardFunction::CreateScriptFunction(NewInput, InDynamicValue);
	return NewInput;
}

UNiagaraClipboardFunction* UNiagaraClipboardFunction::CreateScriptFunction(UObject* InOuter, UNiagaraScript* InScript)
{
	UNiagaraClipboardFunction* NewFunction = Cast<UNiagaraClipboardFunction>(NewObject<UNiagaraClipboardFunction>(InOuter));
	NewFunction->ScriptMode = ENiagaraClipboardFunctionScriptMode::ScriptAsset;
	NewFunction->Script = InScript;
	return NewFunction;
}

FNiagaraClipboard::FNiagaraClipboard()
{
}

void FNiagaraClipboard::Copy(const UNiagaraClipboardFunctionInput* Input)
{
	UNiagaraClipboardContent* ClipboardContent = NewObject<UNiagaraClipboardContent>(GetTransientPackage());
	ClipboardContent->FunctionInputs.Add(CastChecked<UNiagaraClipboardFunctionInput>(StaticDuplicateObject(Input, ClipboardContent)));
	SetClipboardContentInternal(ClipboardContent);
}

const UNiagaraClipboardFunctionInput* FNiagaraClipboard::GetCopiedInput() const
{
	const UNiagaraClipboardContent* ClipboardContent = GetClipboardContentInternal();
	if (ClipboardContent != nullptr &&ClipboardContent->FunctionInputs.Num() == 1)
	{
		return ClipboardContent->FunctionInputs[0];
	}
	return nullptr;
}

void FNiagaraClipboard::SetClipboardContentInternal(UNiagaraClipboardContent* ClipboardContent)
{
	// Clear the mark state for saving.
	UnMarkAllObjects(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));

	// Export the clipboard to text.
	FStringOutputDevice Archive;
	const FExportObjectInnerContext Context;
	UExporter::ExportToOutputDevice(&Context, ClipboardContent, nullptr, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, ClipboardContent->GetOuter());
	FPlatformApplicationMisc::ClipboardCopy(*Archive);
}

const UNiagaraClipboardContent* FNiagaraClipboard::GetClipboardContentInternal() const
{
	// Get the text from the clipboard.
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

	// Try to create niagara clipboard content from that.
	FNiagaraClipboardContentTextObjectFactory ClipboardContentFactory;
	if (ClipboardContentFactory.CanCreateObjectsFromText(ClipboardText))
	{
		ClipboardContentFactory.ProcessBuffer(GetTransientPackage(), RF_Transient, ClipboardText);
		return ClipboardContentFactory.ClipboardContent;
	}

	return nullptr;
}