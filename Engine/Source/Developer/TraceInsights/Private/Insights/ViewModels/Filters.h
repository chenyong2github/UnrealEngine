// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <functional>

#include "CoreMinimal.h"
#include "Misc/TVariant.h"

#define LOCTEXT_NAMESPACE "Filters"

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class EFilterField : uint32
{
	StartTime = 0,
	EndTime = 1,
	Duration = 2,
	EventType = 3
};

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class EFilterDataType : uint32
{
	Int64,
	Double
};

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class EFilterOperator : uint8
{
	Eq = 0, // Equals
	Lt = 1, // Less Than
	Lte = 2, // Less than or equal to
	Gt = 3, // Greater than
	Gte = 4, // Greater than or equal to 
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class IFilterOperator
{
public:

	virtual EFilterOperator GetKey() = 0;
	virtual FString GetName() = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

template<typename T>
class FFilterOperator : public IFilterOperator
{
public:

	typedef TFunction<bool(T, T)> OperatorFunc;

	FFilterOperator(EFilterOperator InKey, FString InName, OperatorFunc InFunc)
		: Func(InFunc)
		, Key(InKey)
		, Name(InName)
	{}
	virtual ~FFilterOperator() {};

	virtual EFilterOperator GetKey() override { return Key; }
	virtual FString GetName() override { return Name; };

	OperatorFunc Func;
private:
	EFilterOperator Key;
	FString Name;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class EFilterGroupOperator
{
	And = 0,
	Or = 1,
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FFilterGroupOperator
{
	FFilterGroupOperator(EFilterGroupOperator InType, FText InName, FText InDesc)
		: Type(InType)
		, Name(InName)
		, Desc(InDesc)
	{}

	EFilterGroupOperator Type;
	FText Name;
	FText Desc;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FFilter
{
	typedef TSharedPtr<const TArray<TSharedPtr<IFilterOperator>>> SupportedOperatorsArrayPtr;

	FFilter(EFilterField InKey, FText InName, FText InDesc, EFilterDataType InDataType, SupportedOperatorsArrayPtr InSupportedOperators)
		: Key(InKey)
		, Name(InName)
		, Desc(InDesc)
		, DataType(InDataType)
		, SupportedOperators(InSupportedOperators)
	{}

	SupportedOperatorsArrayPtr GetSupportedOperators() { return SupportedOperators;	}

	EFilterField Key;
	FText Name;
	FText Desc;
	EFilterDataType DataType;
	SupportedOperatorsArrayPtr SupportedOperators;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFilterStorage
{
public:
	FFilterStorage()
	{
		TSharedPtr<TArray<TSharedPtr<IFilterOperator>>> DoubleOperators = MakeShared<TArray<TSharedPtr<IFilterOperator>>>();
		DoubleOperators->Add(StaticCastSharedRef<IFilterOperator>(MakeShared<FFilterOperator<double>>(EFilterOperator::Lt, TEXT("<"), std::less<>{})));
		DoubleOperators->Add(StaticCastSharedRef<IFilterOperator>(MakeShared<FFilterOperator<double>>(EFilterOperator::Lte, TEXT("<="), std::less_equal<>())));
		DoubleOperators->Add(StaticCastSharedRef<IFilterOperator>(MakeShared<FFilterOperator<double>>(EFilterOperator::Eq, TEXT("="), std::equal_to<>())));
		DoubleOperators->Add(StaticCastSharedRef<IFilterOperator>(MakeShared<FFilterOperator<double>>(EFilterOperator::Gt, TEXT(">"), std::greater<>())));
		DoubleOperators->Add(StaticCastSharedRef<IFilterOperator>(MakeShared<FFilterOperator<double>>(EFilterOperator::Gte, TEXT(">="), std::greater_equal<>())));

		TSharedPtr<TArray<TSharedPtr<IFilterOperator>>> IntegerOperators = MakeShared<TArray<TSharedPtr<IFilterOperator>>>();
		IntegerOperators->Add(StaticCastSharedRef<IFilterOperator>(MakeShared<FFilterOperator<int64>>(EFilterOperator::Lt, TEXT("<"), std::less<>{})));
		IntegerOperators->Add(StaticCastSharedRef<IFilterOperator>(MakeShared<FFilterOperator<int64>>(EFilterOperator::Lte, TEXT("<="), std::less_equal<>())));
		IntegerOperators->Add(StaticCastSharedRef<IFilterOperator>(MakeShared<FFilterOperator<int64>>(EFilterOperator::Eq, TEXT("="), std::equal_to<>())));
		IntegerOperators->Add(StaticCastSharedRef<IFilterOperator>(MakeShared<FFilterOperator<int64>>(EFilterOperator::Gt, TEXT(">"), std::greater<>())));
		IntegerOperators->Add(StaticCastSharedRef<IFilterOperator>(MakeShared<FFilterOperator<int64>>(EFilterOperator::Gte, TEXT(">="), std::greater_equal<>())));

		AllAvailableFilters.Add(EFilterField::StartTime, MakeShared<FFilter>(EFilterField::StartTime, LOCTEXT("StartTime", "Start Time"), LOCTEXT("StartTime", "Start Time"), EFilterDataType::Double, DoubleOperators));
		AllAvailableFilters.Add(EFilterField::EndTime, MakeShared<FFilter>(EFilterField::EndTime, LOCTEXT("EndTime", "End Time"), LOCTEXT("EndTime", "End Time"), EFilterDataType::Double, DoubleOperators));
		AllAvailableFilters.Add(EFilterField::Duration, MakeShared<FFilter>(EFilterField::Duration, LOCTEXT("Duration", "Duration"), LOCTEXT("Duration", "Duration"), EFilterDataType::Double, DoubleOperators));
		AllAvailableFilters.Add(EFilterField::EventType, MakeShared<FFilter>(EFilterField::EventType, LOCTEXT("Type", "Type"), LOCTEXT("Type", "Type"), EFilterDataType::Int64, IntegerOperators));

		FilterGroupOperators.Add(MakeShared<FFilterGroupOperator>(EFilterGroupOperator::And, LOCTEXT("AllOff", "All Off"), LOCTEXT("AllOff", "All Off")));
		FilterGroupOperators.Add(MakeShared<FFilterGroupOperator>(EFilterGroupOperator::Or, LOCTEXT("AnyOff", "Any Off"), LOCTEXT("AnyOff", "Any Off")));
	}

	TSharedPtr<FFilter> GetFilter(EFilterField FilterKey) { return *AllAvailableFilters.Find(FilterKey); }
	TArray<TSharedPtr<FFilterGroupOperator>>& GetFilterGroupOperators() { return FilterGroupOperators; }

private:
	TMap<EFilterField, TSharedPtr<FFilter>> AllAvailableFilters;
	TArray<TSharedPtr<FFilterGroupOperator>> FilterGroupOperators;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFilterService
{
public:
	FFilterService();
	~FFilterService();

	static void CreateInstance() { Instance = MakeShared<FFilterService>(); }
	static TSharedPtr<FFilterService> Get() { return Instance; }

	void RegisterTabSpawner();
	TSharedRef<class SDockTab> SpawnTab(const class FSpawnTabArgs& Args);

	TSharedPtr<FFilter> GetFilter(EFilterField FilterKey) { return FilterStorage.GetFilter(FilterKey); }
	TArray<TSharedPtr<FFilterGroupOperator>>& GetFilterGroupOperators() { return FilterStorage.GetFilterGroupOperators(); }

	TSharedPtr<class SWidget> CreateFilterConfiguratorWidget(TSharedPtr<class FFilterConfigurator> FilterConfiguratorViewModel);

private:
	static const FName FilterConfiguratorTabId;

	static TSharedPtr<FFilterService> Instance;
	FFilterStorage FilterStorage;

	TSharedPtr<class SFilterConfigurator> PendingWidget;
};

////////////////////////////////////////////////////////////////////////////////////////////////////


class FFilterContext
{
public:
	typedef TVariant<double, int64> ContextData;

	template<typename T>
	void AddFilterData(EFilterField Key, const T& InData)
	{
		ContextData VariantData;
		VariantData.Set<T>(InData);
		DataMap.Add(Key, VariantData);
	}

	template<typename T>
	void GetFilterData(EFilterField Key, T& OutData) const
	{
		const ContextData* Data = DataMap.Find(Key);
		check(Data);

		check(Data->IsType<T>());
		OutData = Data->Get<T>();
	}

private:
	TMap<EFilterField, ContextData> DataMap;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
