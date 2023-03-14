// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IObjectChooser.h"
#include "ChooserPropertyAccess.h"
#include "IChooserParameterProxyTable.h"
#include "InstancedStruct.h"
#include "ProxyTable.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FProxyTypeChanged, const UClass* OutputObjectType);

UCLASS(MinimalAPI,BlueprintType)
class UProxyAsset : public UObject, public IHasContextClass
{
	GENERATED_UCLASS_BODY()
public:
	UProxyAsset() {}

#if WITH_EDITOR
	FProxyTypeChanged OnTypeChanged;
	virtual void PostEditUndo() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;

	// caching the type so that on Undo, we can tell if we should fire the changed delegate
	UClass* CachedPreviousType = nullptr;
#endif
	

	UPROPERTY(EditAnywhere, Category = "Proxy", Meta = (AllowAbstract=true))
	TObjectPtr<UClass> Type;
	
	UPROPERTY(EditAnywhere, Category = "Proxy Table Reference")
	TObjectPtr<UClass> ContextClass;
	
	UPROPERTY(EditAnywhere, Meta = (ExcludeBaseStruct, BaseStruct ="/Script/ProxyTable.ChooserParameterProxyTableBase"), Category = "Proxy Table Reference")
	FInstancedStruct ProxyTable;

	virtual UClass* GetContextClass() override { return ContextClass; }
};

USTRUCT()
struct FProxyEntry
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Data")
	TObjectPtr<UProxyAsset> Proxy;

	// temporarily leaving this property for backwards compatibility with old content which used FNames rather than UProxyAsset
	UPROPERTY(EditAnywhere, Category = "Data")
	FName Key;
	
	UPROPERTY(DisplayName="Value", EditAnywhere, Meta = (ExcludeBaseStruct, BaseStruct = "/Script/Chooser.ObjectChooserBase"), Category = "Data")
	FInstancedStruct ValueStruct;
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


USTRUCT()
struct PROXYTABLE_API FProxyTableContextProperty :  public FChooserParameterProxyTableBase
{
	GENERATED_BODY()
public:
	
	UPROPERTY(EditAnywhere, Meta = (BindingType = "UProxyTable*"), Category = "Binding")
	FChooserPropertyBinding Binding;

	virtual bool GetValue(const UObject* ContextObject, const UProxyTable*& OutResult) const override;

#if WITH_EDITOR
	static bool CanBind(const FProperty& Property)
	{
		return Property.GetCPPType() == "UProxyTable*";
	}

	void SetBinding(const TArray<FBindingChainElement>& InBindingChain)
	{
		UE::Chooser::CopyPropertyChain(InBindingChain, Binding.PropertyBindingChain);
	}
#endif
};

USTRUCT()
struct PROXYTABLE_API FLookupProxy : public FObjectChooserBase
{
	GENERATED_BODY()
	virtual UObject* ChooseObject(const UObject* ContextObject) const final override;
	
	public:
	FLookupProxy();
	
	UPROPERTY(EditAnywhere, Category="Parameters")
	TObjectPtr<UProxyAsset> Proxy;
};