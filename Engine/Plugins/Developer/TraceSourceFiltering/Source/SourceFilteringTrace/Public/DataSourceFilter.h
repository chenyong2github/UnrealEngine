// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "IDataSourceFilterInterface.h"

#include "DataSourceFilter.generated.h"

struct FFilterLedger : public TThreadSingleton<FFilterLedger>
{
public:
	TArray<const UDataSourceFilter*> RejectedFilters;
};

UCLASS(Blueprintable)
class SOURCEFILTERINGTRACE_API UDataSourceFilter : public UObject, public IDataSourceFilterInterface
{
	GENERATED_BODY()
public:
	UDataSourceFilter();
	virtual ~UDataSourceFilter();

	UFUNCTION(BlueprintNativeEvent, Category = TraceSourceFiltering)
	bool DoesActorPassFilter(const AActor* InActor) const;
	bool DoesActorPassFilter_Implementation(const AActor* InActor) const;

	/** Begin IDataSourceFilterInterface overrides */
	virtual void SetEnabled(bool bState) override;	
	virtual bool IsEnabled() const final;
protected:
	virtual void GetDisplayText_Internal(FText& OutDisplayText) const override;
	/** End IDataSourceFilterInterface overrides */

	virtual bool DoesActorPassFilter_Internal(const AActor* InActor) const;
protected:
	/** Whether or not this filter is enabled */
	bool bIsEnabled;
};

