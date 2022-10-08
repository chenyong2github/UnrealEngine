// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IObjectChooser.h"
#include "UObject/Interface.h"

#include "Chooser.generated.h"

UINTERFACE(NotBlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class CHOOSER_API UChooserParameterBool : public UInterface
{
	GENERATED_BODY()
};

class CHOOSER_API IChooserParameterBool
{
	GENERATED_BODY()

public:
	virtual bool GetValue(const UObject* ContextObject, bool& OutResult) { return false; }
};

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


UINTERFACE(NotBlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class CHOOSER_API UChooserParameterFloat : public UInterface
{
	GENERATED_BODY()
};

class CHOOSER_API IChooserParameterFloat
{
	GENERATED_BODY()

public:
	virtual bool GetValue(const UObject* ContextObject, float& OutResult) { return false; }
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

UINTERFACE(NotBlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class CHOOSER_API UChooserColumn : public UInterface
{
	GENERATED_BODY()
};

class CHOOSER_API IChooserColumn 
{
	GENERATED_BODY()

public:
	virtual void Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) {};
	virtual void SetNumRows(uint32 NumRows) {}
	virtual void DeleteRows(const TArray<uint32> & RowIndices) {}
	virtual UClass* GetInputValueInterface() {return nullptr;};
	virtual UObject* GetInputValue() {return nullptr;}
	virtual void SetInputValue(UObject* InputValue) {}
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