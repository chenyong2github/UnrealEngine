// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IObjectChooser.h"
#include "UObject/Interface.h"

#include "Chooser.generated.h"

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
	virtual FName GetDisplayName() { return "ChooserColumn"; }
	virtual void SetDisplayName(FName Name) { }
	virtual void SetNumRows(uint32 NumRows) {}
	virtual void DeleteRows(const TArray<uint32> & RowIndices) {}
};

UCLASS()
class CHOOSER_API UChooserColumnBool : public UObject, public IChooserColumn
{
	GENERATED_BODY()
	public:
	UChooserColumnBool() { }
	
	UPROPERTY(EditAnywhere, Category = "Input")
	FName InputPropertyName;

	UPROPERTY(EditAnywhere, Category=Runtime)
	// array of results (cells for this column for each row in the table)
	// should match the length of the Results array 
	TArray<bool> RowValues;
	
	virtual void Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) override;

	// todo: macro boilerplate
	virtual void SetNumRows(uint32 NumRows) override { RowValues.SetNum(NumRows); }
	virtual FName GetDisplayName() override { return InputPropertyName; }
	virtual void SetDisplayName(FName Name) override { InputPropertyName = Name; }
	virtual void DeleteRows(const TArray<uint32> & RowIndices )
	{
		for(uint32 Index : RowIndices)
		{
			RowValues.RemoveAt(Index);
		}
	}
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
	UChooserColumnFloatRange() { }
		
	UPROPERTY(EditAnywhere, Category = "Input")
		FName InputPropertyName;

	UPROPERTY(EditAnywhere, Category=Runtime)
	// array of results (cells for this column for each row in the table)
	// should match the length of the Results array 
	TArray<FChooserFloatRangeRowData> RowValues;
	
	virtual void Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) override;
	virtual void SetNumRows(uint32 NumRows) { RowValues.SetNum(NumRows); }
	virtual FName GetDisplayName() override { return InputPropertyName; }
	virtual void SetDisplayName(FName Name) override { InputPropertyName = Name; }
	virtual void DeleteRows(const TArray<uint32> & RowIndices )
	{
		for(uint32 Index : RowIndices)
		{
			RowValues.RemoveAt(Index);
		}
	}
};

UCLASS(MinimalAPI)
class UChooserTable : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	UChooserTable() {}
	
	UPROPERTY(EditAnywhere, Meta=(EditInline="true"), Category = "Runtime")
	TArray<TScriptInterface<IChooserColumn>> Columns;

	// array of results (rows of table)
	UPROPERTY(EditAnywhere, Meta = (EditInline = "true"), Category = "Runtime")
	TArray<TScriptInterface<IObjectChooser>> Results;
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