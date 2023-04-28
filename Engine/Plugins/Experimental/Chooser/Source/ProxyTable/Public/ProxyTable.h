// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ChooserPropertyAccess.h"
#include "IChooserParameterProxyTable.h"
#include "InstancedStruct.h"
#include "InstancedStructContainer.h"
#include "Misc/Guid.h"
#include "ProxyAsset.h"
#include "ProxyTable.generated.h"

USTRUCT()
struct PROXYTABLE_API FProxyEntry
{
	GENERATED_BODY()

	bool operator== (const FProxyEntry& Other) const;
	bool operator< (const FProxyEntry& Other) const;
	
	UPROPERTY(EditAnywhere, Category = "Data")
	TObjectPtr<UProxyAsset> Proxy;

	// temporarily leaving this property for backwards compatibility with old content which used FNames rather than UProxyAsset
	UPROPERTY(EditAnywhere, Category = "Data")
	FName Key;
	
	UPROPERTY(DisplayName="Value", EditAnywhere, Meta = (ExcludeBaseStruct, BaseStruct = "/Script/Chooser.ObjectChooserBase"), Category = "Data")
	FInstancedStruct ValueStruct;

	const FGuid GetGuid() const;
};

#if WITH_EDITORONLY_DATA
inline uint32 GetTypeHash(const FProxyEntry& Entry) { return GetTypeHash(Entry.GetGuid()); }
#endif

DECLARE_MULTICAST_DELEGATE(FProxyTableChanged);

UCLASS(MinimalAPI,BlueprintType)
class UProxyTable : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	UProxyTable() {}

	UPROPERTY()
	TArray<FGuid> Keys;

	UPROPERTY()
	FInstancedStructContainer Values;
	
	UObject* FindProxyObject(const FGuid& Key, FChooserEvaluationContext& Context) const;

#if WITH_EDITORONLY_DATA
public:
	FProxyTableChanged OnProxyTableChanged;
	
	UPROPERTY(EditAnywhere, Category = "Hidden")
	TArray<FProxyEntry> Entries;
	
	UPROPERTY(EditAnywhere, Category = "Inheritance")
	TArray<TObjectPtr<UProxyTable>> InheritEntriesFrom;

	virtual void PostLoad() override;
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
private:
	void BuildRuntimeData();
	TArray<TWeakObjectPtr<UProxyTable>> TableDependencies;
	TArray<TWeakObjectPtr<UProxyAsset>> ProxyDependencies;
#endif

};
