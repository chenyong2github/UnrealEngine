// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IObjectChooser.h"
#include "ChooserPropertyAccess.h"
#include "IChooserParameterProxyTable.h"

#include "ProxyTable.generated.h"


USTRUCT()
struct FProxyEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Data")
	FName Key;
	UPROPERTY(EditAnywhere, Meta = (EditInlineInterface = "true"), Category = "Data")
	TScriptInterface<IObjectChooser> Value;
};

UCLASS(MinimalAPI,BlueprintType)
class UProxyTable : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	UProxyTable() {}

	UPROPERTY(EditAnywhere, Category = "Hidden")
	TArray<FProxyEntry> Entries;
	
	UPROPERTY(EditAnywhere, Category = "Inheritance")
	TArray<TObjectPtr<UProxyTable>> InheritEntriesFrom;
};


struct FBindingChainElement;

UCLASS()
class PROXYTABLE_API UChooserParameterProxyTable_ContextProperty :  public UObject, public IChooserParameterProxyTable
{
	GENERATED_BODY()
public:

	UPROPERTY()
	TArray<FName> PropertyBindingChain;
	
	virtual bool GetValue(const UObject* ContextObject, const UProxyTable*& OutResult) const override;

#if WITH_EDITOR
	static bool CanBind(const FProperty& Property)
	{
		return Property.GetCPPType() == "UProxyTable*";
	}

	void SetBinding(const TArray<FBindingChainElement>& InBindingChain)
	{
		UE::Chooser::CopyPropertyChain(InBindingChain, PropertyBindingChain);
	}
#endif
};



UCLASS()
class PROXYTABLE_API UObjectChooser_LookupProxy : public UObject, public IObjectChooser
{
	GENERATED_BODY()
	virtual UObject* ChooseObject(const UObject* ContextObject) const final override;
	
	public:
	UObjectChooser_LookupProxy() {};
	UObjectChooser_LookupProxy(const FObjectInitializer& ObjectInitializer);
	
	UPROPERTY(EditAnywhere, Meta = (EditInlineInterface = "true"), Category = "Input")
	TScriptInterface<IChooserParameterProxyTable> ProxyTable;
	
	UPROPERTY(EditAnywhere, Category="Parameters")
	FName Key;
};
