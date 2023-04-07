// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Templates/SubclassOf.h"

class UScriptStruct;
class USubsystem;

/**
 * The TypedElementQueryBuilder allows for the construction of queries for use by the Typed Element Data Storage.
 * There are two types of queries, simple and normal. Simple queries are guaranteed to be supported by the data
 * storage backend and guaranteed to have no performance side effects. <Normal queries pending development.>
 * 
 * Queries are constructed with the following section:
 * - Select		A list of the data objects that are returned as the result of the query.
 * - Count		Counts the total number or rows that pass the filter.
 * - Where		A list of conditions that restrict what's accepted by the query.
 * - DependsOn	A list of systems outside the data storage that will be accessed by the query('s user).
 * - Compile	Compiles the query into its final form and can be used afterwards.
 * 
 * Calls to the sections become increasingly restrictive, e.g. after calling Where only DependsOn can be
 * called again.
 * 
 * Arguments to the various functions take a pointer to a description of a UStruct. These can be provived
 * in the follow ways:
 * - By using the templated version, e.g. Any<FStructExample>()
 * - By callling the static StaticStruct() function on the UStruct, e.g. FStructExample::StaticStruct();
 * - By name using the Type or TypeOptional string operator, e.g. "/Script/ExamplePackage.FStructExample"_Type or
 *		"/Script/OptionalPackage.FStructOptional"_TypeOptional
 * All functions allow for a single type to be added or a list of types, e.g. ReadOnly(Type<FStructExample>() or
 *		ReadOnly({ Type<FStructExample1>(), FStructExample2::StaticStruct(), "/Script/ExamplePackage.FStructExample3"_Type });
 *
 * Some functions allow binding to a callback. In these cases the arguments to the provided callback are analyzed and
 * added to the query automatically. Const arguments are added as ReadOnly, while non-const arguments are added as 
 * ReadWrite. Callbacks can be periodically called if constructed as a processor, in which case the callback is triggered
 * repeatedly, usually once per frame and called for all row (ranges) that match the query. If constructed as an observer
 * the provided target type is monitored for actions like addition or deletion into/from any table and will trigger the
 * callback once if the query matches. The following function signatures are accepted by "Select":
 *	- void([const]Column&...) 
 *	- void([const]Column*...) 
 *	- void(TypedElementRowHandle, [const]Column&...) 
 *	- void(<Context>&, [const]Column&...) 
 *	- void(<Context>&, TypedElementRowHandle, [const]Column&...) 
 *	- void(<Context>&, [const]Column*...) 
 *	- void(<Context>&, const TypedElementRowHandle*, [const]Column*...) 
 *	Where <Context> is ITypedElementDataStorageInterface::IQueryContext or FCachedQueryContext<...>	e.g.:
 *		void(
 *			FCachedQueryContext<Subsystem1, const Subsystem2>& Context, 
 *			TypedElementRowHandle Row, 
 *			ColumnType0& ColumnA, 
 *			const ColumnType1& ColumnB) 
 *			{...}
 *
 * FCachedQueryContext can be used to store cached pointers to dependencies to reduce the overhead of retrieving these. The same
 * const principle as for other arguments applies so dependencies marked as const can only be accessed as read-only or otherwise
 * can be accessed as readwrite.
 *
 * The following is a simplified example of these options combined together:
 *		FProcessor Info(
 *			ITypedElementDataStorageInterface::EQueryTickPhase::FrameEnd, 
 *			DataStorage->GetQueryTickGroupName(ITypedElementDataStorageInterface::EQueryTickGroups::SyncExternalToDataStorage);
 *		Query = Select(FName(TEXT("Example Callback")), Info, 
 *				[](FCachedQueryContext<Subsystem1, const Subsystem2>&, const FDataExample1&, FDataExample2&) {});
 * 
 * "Select" is constructed with: 
 * - ReadOnly: Indicates that the data object will only be read from
 * - ReadWrite: Indicated that the data object will be read and written to.
 * 
 * "Count" does not have any construction options.
 * 
 * "Where" is constructed with:
 * - All: The query will be accepted only if all the types listed here are present in a table.
 * - Any: The query will be accepted if at least one of the listed types is present in a table.
 * - Not: The query will be accepted if none of the listed types are present in a table.
 * The above construction calls can be mixed and be called multiple times.
 * All functions accept a nullptr for the type in which case the call will have no effect. This can be used to
 *		reference types in plugins that may not be loaded when using the TypeOptional string operator.

 * "DependsOn" is constructed with:
 * - ReadOnly: Indicates that the external system will only be used to read data from.
 * - ReadWrite: Indicates that the external system will be used to write data to.
 *
 * Usage example:
 * ITypedElementDataStorageInterface::FQueryDescription Query =
 *		Select()
 *			.ReadWrite({ FDataExample1::StaticStruct() })
 *			.ReadWrite<FDataExample2, FDataExample3>()
 *			.ReadOnly<FDataExample4>()
 *		.Where()
 *			.All<FTagExample1, FDataExample5>()
 *			.Any("/Script/ExamplePackage.FStructExample"_TypeOptional)
 *			.None(FTagExample2::StaticStruct())
 *		.DependsOn()
 *			.ReadOnly<USystemExample1, USystemExample2>()
 *			.ReadWrite(USystemExample2::StaticClass())
 *		.Compile();
 *
 * Creating a query is expensive on the builder and the backend side. It's therefore recommended to create a query
 * and store its compiled form for repeated use instead of rebuilding the query on every update.
 */

namespace TypedElementQueryBuilder
{
	TYPEDELEMENTFRAMEWORK_API const UScriptStruct* Type(FTopLevelAssetPath Name);
	TYPEDELEMENTFRAMEWORK_API const UScriptStruct* TypeOptional(FTopLevelAssetPath Name);
	TYPEDELEMENTFRAMEWORK_API const UScriptStruct* operator""_Type(const char* Name, std::size_t NameSize);
	TYPEDELEMENTFRAMEWORK_API const UScriptStruct* operator""_TypeOptional(const char* Name, std::size_t NameSize);

	class TYPEDELEMENTFRAMEWORK_API FDependency final
	{
		friend class Count;
		friend class Select;
		friend class FSimpleQuery;
	public:
		template<typename... TargetTypes>
		FDependency& ReadOnly();
		FDependency& ReadOnly(const UClass* Target);
		FDependency& ReadOnly(std::initializer_list<const UClass*> Targets);
		template<typename... TargetTypes>
		FDependency& ReadWrite();
		FDependency& ReadWrite(const UClass* Target);
		FDependency& ReadWrite(std::initializer_list<const UClass*> Targets);

		ITypedElementDataStorageInterface::FQueryDescription&& Compile();

	private:
		explicit FDependency(ITypedElementDataStorageInterface::FQueryDescription* Query);

		ITypedElementDataStorageInterface::FQueryDescription* Query;
	};

	class TYPEDELEMENTFRAMEWORK_API FSimpleQuery final
	{
	public:
		friend class Count;
		friend class Select;

		FDependency DependsOn();
		ITypedElementDataStorageInterface::FQueryDescription&& Compile();

		template<typename... TargetTypes>
		FSimpleQuery& All();
		FSimpleQuery& All(const UScriptStruct* Target);
		FSimpleQuery& All(std::initializer_list<const UScriptStruct*> Targets);
		template<typename... TargetTypes>
		FSimpleQuery& Any();
		FSimpleQuery& Any(const UScriptStruct* Target);
		FSimpleQuery& Any(std::initializer_list<const UScriptStruct*> Targets);
		template<typename... TargetTypes>
		FSimpleQuery& None();
		FSimpleQuery& None(const UScriptStruct* Target);
		FSimpleQuery& None(std::initializer_list<const UScriptStruct*> Targets);

	private:
		explicit FSimpleQuery(ITypedElementDataStorageInterface::FQueryDescription* Query);

		ITypedElementDataStorageInterface::FQueryDescription* Query;
	};

	struct TYPEDELEMENTFRAMEWORK_API FQueryCallbackType{};

	struct TYPEDELEMENTFRAMEWORK_API FProcessor final : public FQueryCallbackType
	{
		FProcessor(ITypedElementDataStorageInterface::EQueryTickPhase Phase, FName Group);
		FProcessor& SetPhase(ITypedElementDataStorageInterface::EQueryTickPhase NewPhase);
		FProcessor& SetGroup(FName GroupName);
		FProcessor& SetBeforeGroup(FName GroupName);
		FProcessor& SetAfterGroup(FName GroupName);
		FProcessor& ForceToGameThread(bool bForce);
		
		ITypedElementDataStorageInterface::EQueryTickPhase Phase;
		FName Group;
		FName BeforeGroup;
		FName AfterGroup;
		bool bForceToGameThread{ false };
	};

	struct TYPEDELEMENTFRAMEWORK_API FObserver final : public FQueryCallbackType
	{
		enum class EEvent : uint8
		{
			Add,
			Remove
		};

		template<typename ColumnType>
		explicit FObserver(EEvent MonitorForEvent);
		FObserver(EEvent MonitorForEvent, const UScriptStruct* MonitoredColumn);

		FObserver& SetEvent(EEvent MonitorForEvent);
		FObserver& SetMonitoredColumn(const UScriptStruct* MonitoredColumn);
		template<typename ColumnType>
		FObserver& SetMonitoredColumn();
		FObserver& ForceToGameThread(bool bForce);

		const UScriptStruct* Monitor;
		EEvent Event;
		bool bForceToGameThread{ false };
	};

	struct TYPEDELEMENTFRAMEWORK_API FPhaseAmble final : public FQueryCallbackType
	{
		enum class ELocation : uint8
		{
			Preamble,
			Postamble
		};

		FPhaseAmble(ELocation InLocation, ITypedElementDataStorageInterface::EQueryTickPhase InPhase);
		FPhaseAmble& SetLocation(ELocation NewLocation);
		FPhaseAmble& SetPhase(ITypedElementDataStorageInterface::EQueryTickPhase NewPhase);
		FPhaseAmble& ForceToGameThread(bool bForce);

		ITypedElementDataStorageInterface::EQueryTickPhase Phase;
		ELocation Location;
		bool bForceToGameThread{ false };
	};

	// Because this is a thin wrapper called from within a query callback, it's better to inline fully so all
	// function pre/postambles can be optimized away.
	struct FQueryContextForwarder : public ITypedElementDataStorageInterface::IQueryContext
	{
		inline FQueryContextForwarder(
			const ITypedElementDataStorageInterface::FQueryDescription& InDescription, 
			ITypedElementDataStorageInterface::IQueryContext& InParentContext);
		inline ~FQueryContextForwarder() = default;

		inline const void* GetColumn(const UScriptStruct* ColumnType) const override;
		inline void* GetMutableColumn(const UScriptStruct* ColumnType) override;
		inline void GetColumns(TArrayView<char*> RetrievedAddresses, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes,
			TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes) override;
		inline void GetColumnsUnguarded(int32 TypeCount, char** RetrievedAddresses, const TWeakObjectPtr<const UScriptStruct>* ColumnTypes,
			const ITypedElementDataStorageInterface::EQueryAccessType* AccessTypes) override;

		inline UObject* GetMutableDependency(const UClass* DependencyClass) override;
		inline const UObject* GetDependency(const UClass* DependencyClass) override;
		inline void GetDependencies(TArrayView<UObject*> RetrievedAddresses, TConstArrayView<TWeakObjectPtr<const UClass>> SubsystemTypes,
			TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes) override;

		inline uint32 GetRowCount() const override;
		inline TConstArrayView<TypedElementRowHandle> GetRowHandles() const override;
		inline void RemoveRow(TypedElementRowHandle Row) override;
		inline void RemoveRows(TConstArrayView<TypedElementRowHandle> Rows) override;

		inline void AddColumns(TypedElementRowHandle Row, TConstArrayView<const UScriptStruct*> ColumnTypes) override;
		inline void AddColumns(TConstArrayView<TypedElementRowHandle> Rows, TConstArrayView<const UScriptStruct*> ColumnTypes) override;
		inline void RemoveColumns(TypedElementRowHandle Row, TConstArrayView<const UScriptStruct*> ColumnTypes) override;
		inline void RemoveColumns(TConstArrayView<TypedElementRowHandle> Rows, TConstArrayView<const UScriptStruct*> ColumnTypes) override;

		ITypedElementDataStorageInterface::IQueryContext& ParentContext;
		const ITypedElementDataStorageInterface::FQueryDescription& Description;
	};

	template<typename... Dependencies>
	struct FCachedQueryContext final : public FQueryContextForwarder
	{
		explicit FCachedQueryContext(
			const ITypedElementDataStorageInterface::FQueryDescription& InDescription, 
			ITypedElementDataStorageInterface::IQueryContext& InParentContext);
		
		static void Register(ITypedElementDataStorageInterface::FQueryDescription& Query);

		template<typename Dependency>
		Dependency& GetCachedMutableDependency();
		template<typename Dependency>
		const Dependency& GetCachedDependency() const;
	};

	// Explicitly not following the naming convention in order to present this as a query that can be read as such.
	class TYPEDELEMENTFRAMEWORK_API Select final
	{
	public:
		Select();

		template<typename CallbackType, typename Function>
		Select(FName Name, const CallbackType& Type, Function&& Callback);
		template<typename CallbackType, typename Class, typename Function>
		Select(FName Name, const CallbackType& Type, Class* Instance, Function&& Callback);

		template<typename... TargetTypes>
		Select& ReadOnly();
		Select& ReadOnly(const UScriptStruct* Target);
		Select& ReadOnly(std::initializer_list<const UScriptStruct*> Targets);
		template<typename... TargetTypes>
		Select& ReadWrite();
		Select& ReadWrite(const UScriptStruct* Target);
		Select& ReadWrite(std::initializer_list<const UScriptStruct*> Targets);

		ITypedElementDataStorageInterface::FQueryDescription&& Compile();
		FSimpleQuery Where();
		FDependency DependsOn();

	private:
		ITypedElementDataStorageInterface::FQueryDescription Query;
	};

	// Explicitly not following the naming convention in order to keep readability consistent. It now reads like a query sentence.
	class TYPEDELEMENTFRAMEWORK_API Count final
	{
	public:
		Count();

		FSimpleQuery Where();
		FDependency DependsOn();

	private:
		ITypedElementDataStorageInterface::FQueryDescription Query;
	};
} // namespace TypedElementQueryBuilder

#include "Elements/Framework/TypedElementQueryBuilder.inl"
