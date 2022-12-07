// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IChooserColumn.h"
#include "IChooserParameterGameplayTag.h"
#include "ChooserPropertyAccess.h"
#include "GameplayTagContainer.h"
#include "GameplayTagColumn.generated.h"

struct FBindingChainElement;

UCLASS(DisplayName = "Gameplay Tags Property Binding")
class CHOOSER_API UChooserParameterGameplayTag_ContextProperty :  public UObject, public IChooserParameterGameplayTag
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<FName> PropertyBindingChain;
	
	virtual bool GetValue(const UObject* ContextObject, const FGameplayTagContainer*& OutResult) const override;

#if WITH_EDITOR
	static bool CanBind(const FProperty& Property)
	{
		static FString TypeName = "FGameplayTagContainer";
		return Property.GetCPPType() == TypeName;
	}

	void SetBinding(const TArray<FBindingChainElement>& InBindingChain)
	{
		UE::Chooser::CopyPropertyChain(InBindingChain, PropertyBindingChain);
	}
#endif
};

UCLASS()
class CHOOSER_API UChooserColumnGameplayTag : public UObject, public IChooserColumn
{
	GENERATED_BODY()
	public:
	UChooserColumnGameplayTag() {};
	UChooserColumnGameplayTag(const FObjectInitializer& ObjectInitializer);
	
	UPROPERTY(EditAnywhere, Meta = (EditInlineInterface = "true"), Category = "Input")
	TScriptInterface<IChooserParameterGameplayTag> InputValue;

	UPROPERTY(EditAnywhere, Category=Runtime)
	EGameplayContainerMatchType	TagMatchType = EGameplayContainerMatchType::Any;

	UPROPERTY(EditAnywhere, Category=Runtime)
	// array of results (cells for this column for each row in the table)
	// should match the length of the Results array 
	TArray<FGameplayTagContainer> RowValues;
	
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
	virtual UClass* GetInputValueInterface() override { return UChooserParameterGameplayTag::StaticClass(); };
	virtual UObject* GetInputValue() override { return InputValue.GetObject(); };
	virtual void SetInputValue(UObject* Value) override { InputValue = Value; };
};