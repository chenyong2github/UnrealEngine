// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories.h"

#include "OptimusEditorClipboard.generated.h"

class UOptimusNodeGraph;
class UOptimusNode;

USTRUCT()
struct FOptimusClipboardNodeLink
{
	GENERATED_BODY()
	
	// Index into the UOptimusClipboardContent::Nodes list.
	UPROPERTY()
	int32 NodeOutputIndex = INDEX_NONE;

	UPROPERTY()
	FString NodeOutputPinName;

	UPROPERTY()
	int32 NodeInputIndex = INDEX_NONE;

	UPROPERTY()
	FString NodeInputPinName;
};

UCLASS()
class UOptimusClipboardContent :
	public UObject
{
	GENERATED_BODY()
	
public:
	static UOptimusClipboardContent* Create(
		const UOptimusNodeGraph* InGraph,
		const TArray<UOptimusNode*>& InNodes
		);

	// Creates a temporary node graph with the contents of the clipboard
	UOptimusNodeGraph* GetGraphFromClipboardContent() const;

	UPROPERTY()
	TArray<TObjectPtr<const UOptimusNode>> Nodes;

	UPROPERTY()
	TArray<FOptimusClipboardNodeLink> NodeLinks;
};


class OPTIMUSEDITOR_API FOptimusEditorClipboard
{
public:
	FOptimusEditorClipboard() = default;
	
	void SetClipboardContent(UOptimusClipboardContent *InContent);

	UOptimusClipboardContent *GetClipboardContent() const;

	bool HasValidClipboardContent() const;
};


struct FOptimusEditorClipboardContentTextObjectFactory final :
	FCustomizableTextObjectFactory
{
	UOptimusClipboardContent* ClipboardContent = nullptr;

	FOptimusEditorClipboardContentTextObjectFactory() :
		FCustomizableTextObjectFactory(GWarn)
	{
	}

protected:
	bool CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const override
	{
		return ObjectClass == UOptimusClipboardContent::StaticClass();
	}

	void ProcessConstructedObject(UObject* CreatedObject) override
	{
		if (CreatedObject->IsA<UOptimusClipboardContent>())
		{
			ClipboardContent = CastChecked<UOptimusClipboardContent>(CreatedObject);
		}
	}
};
