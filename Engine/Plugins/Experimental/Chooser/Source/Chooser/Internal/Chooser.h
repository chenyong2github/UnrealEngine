// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IObjectChooser.h"
#include "IChooserColumn.h"

#include "Chooser.generated.h"


UCLASS(MinimalAPI)
class UChooserTable : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	UChooserTable() {}

	// each possible result
	UPROPERTY(EditAnywhere, Meta = (EditInlineInterface = "true"), Category = "Hidden")
	TArray<TScriptInterface<IObjectChooser>> Results;
	
	// columns which filter results
	UPROPERTY(EditAnywhere, Meta = (EditInlineInterface = "true"), Category="Hidden")
	TArray<TScriptInterface<IChooserColumn>> Columns;

	UPROPERTY(EditAnywhere, Category="Input", Meta = (AllowAbstract=true))
	TObjectPtr<UClass> ContextObjectType;
	
	UPROPERTY(EditAnywhere, Category="Output", Meta = (AllowAbstract=true))
	TObjectPtr<UClass> OutputObjectType;
};

UCLASS()
class CHOOSER_API UObjectChooser_EvaluateChooser : public UObject, public IObjectChooser
{
	GENERATED_BODY()

	virtual UObject* ChooseObject(const UObject* ContextObject) const final override;
	virtual EIteratorStatus ChooseMulti(const UObject* ContextObject, FObjectChooserIteratorCallback Callback) const final override;
	public:
	
	UPROPERTY(EditAnywhere, Category="Parameters")
	TObjectPtr<UChooserTable> Chooser;
};


UCLASS()
class CHOOSER_API UChooserColumnMenuContext : public UObject
{
	GENERATED_BODY()
public:
	class FAssetEditorToolkit* Editor;
	TWeakObjectPtr<UChooserTable> Chooser;
	int ColumnIndex;
};