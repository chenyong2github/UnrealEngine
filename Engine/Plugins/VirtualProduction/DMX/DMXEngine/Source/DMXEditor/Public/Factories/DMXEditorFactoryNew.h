// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorReimportHandler.h"
#include "Factories/Factory.h"

#include "DMXEditorFactoryNew.generated.h"

class UDMXLibrary;

UCLASS(hidecategories = Object)
class DMXEDITOR_API UDMXEditorFactoryNew : public UFactory
{
	GENERATED_BODY()
public:
	UDMXEditorFactoryNew();

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	uint32 GetMenuCategories() const override;
	//~ Begin UFactory Interface	

private:
	UDMXLibrary* MakeNewEditor(UObject* InParent, FName Name, EObjectFlags Flags);
};
