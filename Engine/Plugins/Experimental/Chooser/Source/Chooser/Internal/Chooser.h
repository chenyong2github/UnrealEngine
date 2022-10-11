// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IObjectChooser.h"
#include "IChooserColumn.h"
#include "IChooserColumnParameter.h"

#include "Chooser.generated.h"

UCLASS()
class CHOOSER_API UChooserParameterBool_ContextProperty :  public UObject, public IChooserParameterBool
{
	GENERATED_BODY()
public:
	UPROPERTY()
 	FName PropertyName;
	
	virtual bool GetValue(const UObject* ContextObject, bool& OutResult) override;

	static const FString& CPPTypeName()
	{
		static FString TypeName = "bool";
		return TypeName;
	}
};

UCLASS()
class CHOOSER_API UChooserParameterFloat_ContextProperty :  public UObject, public IChooserParameterFloat
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FName PropertyName;
	
	virtual bool GetValue(const UObject* ContextObject, float& OutResult) override;
	
	static const FString& CPPTypeName()
	{
		static FString TypeName = "double";
		return TypeName;
	}
};

UCLASS()
class CHOOSER_API UChooserColumnBool : public UObject, public IChooserColumn
{
	GENERATED_BODY()
	public:
	UChooserColumnBool() {};
	UChooserColumnBool(const FObjectInitializer& ObjectInitializer);
	
	UPROPERTY(EditAnywhere, Meta = (EditInlineInterface = "true"), Category = "Input")
	TScriptInterface<IChooserParameterBool> InputValue;

	UPROPERTY(EditAnywhere, Category=Runtime)
	// array of results (cells for this column for each row in the table)
	// should match the length of the Results array 
	TArray<bool> RowValues;
	
	virtual void Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) override;

	// todo: macro boilerplate
	virtual void SetNumRows(uint32 NumRows) override { RowValues.SetNum(NumRows); }
	virtual void DeleteRows(const TArray<uint32> & RowIndices )
	{
		for(uint32 Index : RowIndices)
		{
			RowValues.RemoveAt(Index);
		}
	}
	virtual UClass* GetInputValueInterface() override { return UChooserParameterBool::StaticClass(); };
	virtual UObject* GetInputValue() override { return InputValue.GetObject(); };
	virtual void SetInputValue(UObject* Value) override { InputValue = Value; };
};

USTRUCT()
struct FChooserFloatRangeRowData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category=Runtime)
	float Min=0;
	
	UPROPERTY(EditAnywhere, Category=Runtime)
	float Max=0;
};

UCLASS()
class CHOOSER_API UChooserColumnFloatRange : public UObject, public IChooserColumn
{
	GENERATED_BODY()
	public:
	UChooserColumnFloatRange() {}
	UChooserColumnFloatRange(const FObjectInitializer& ObjectInitializer);
		
	UPROPERTY(EditAnywhere, Meta = (EditInlineInterface = "true"), Category = "Input")
	TScriptInterface<IChooserParameterFloat> InputValue;
	
	UPROPERTY(EditAnywhere, Category=Runtime)
	// array of results (cells for this column for each row in the table)
	// should match the length of the Results array 
	TArray<FChooserFloatRangeRowData> RowValues;
	
	virtual void Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) override;
	virtual void SetNumRows(uint32 NumRows) { RowValues.SetNum(NumRows); }
	virtual void DeleteRows(const TArray<uint32> & RowIndices )
	{
		for(uint32 Index : RowIndices)
		{
			RowValues.RemoveAt(Index);
		}
	}
	virtual UClass* GetInputValueInterface() { return UChooserParameterFloat::StaticClass(); };
	virtual UObject* GetInputValue() override { return InputValue.GetObject(); };
	virtual void SetInputValue(UObject* Value) override { InputValue = Value; };
};

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

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UClass> ContextObjectType;
	
	UPROPERTY(EditAnywhere, Category="Output")
	TObjectPtr<UClass> OutputObjectType;
};

UCLASS()
class CHOOSER_API UObjectChooser_EvaluateChooser : public UObject, public IObjectChooser
{
	GENERATED_BODY()

	virtual UObject* ChooseObject(const UObject* ContextObject) const final override;

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