// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ContainersFwd.h"
#include "Delegates/Delegate.h"
#include "Elements/Framework/TypedElementColumnUtils.h"
#include "Math/NumericLimits.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/Function.h"
#include "UObject/Interface.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementDataStorageInterface.generated.h"

class UClass;
class USubsystem;
class UScriptStruct;

struct ColumnDataResult
{
	/** Pointer to the structure that holds the description of the returned data. */
	const UScriptStruct* Description;
	/** Pointer to the column data. The type is guaranteed to match type described in Description. */
	void* Data;
};

using TypedElementTableHandle = uint64;
static constexpr auto TypedElementInvalidTableHandle = TNumericLimits<TypedElementTableHandle>::Max();
using TypedElementRowHandle = uint64;
static constexpr auto TypedElementInvalidRowHandle = TNumericLimits<TypedElementRowHandle>::Max();
using TypedElementQueryHandle = uint64;
static constexpr auto TypedElementInvalidQueryHandle = TNumericLimits<TypedElementQueryHandle>::Max();

using FTypedElementOnDataStorageCreation = FSimpleMulticastDelegate;
using FTypedElementOnDataStorageDestruction = FSimpleMulticastDelegate;
using FTypedElementOnDataStorageUpdate = FSimpleMulticastDelegate;

using TypedElementDataStorageCreationCallbackRef = TFunctionRef<void(TypedElementRowHandle Row)>;

/**
 * Base for the data structures for a column.
 */
USTRUCT()
struct FTypedElementDataStorageColumn
{
	GENERATED_BODY()
};

/**
 * Base for the data structures that act as tags to rows. Tags should not have any data.
 */
USTRUCT()
struct FTypedElementDataStorageTag
{
	GENERATED_BODY()
};

UINTERFACE(MinimalAPI)
class UTypedElementDataStorageInterface : public UInterface
{
	GENERATED_BODY()
};

class TYPEDELEMENTFRAMEWORK_API ITypedElementDataStorageInterface
{
	GENERATED_BODY()

public:
	/**
	 * @section Table management
	 * 
	 * @description
	 * Tables are automatically created by taking an existing table and adding/removing columns. For
	 * performance its however better to create a table before adding objects to the table. This
	 * doesn't prevent those objects from having columns added/removed at a later time.
	 * To make debugging and profiling easier it's also recommended to give tables a name.
	 */

	/** Creates a new table for with the provided columns. Optionally a name can be given which is useful for retrieval later. */
	virtual TypedElementTableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList) = 0;
	virtual TypedElementTableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList, const FName Name) = 0;
	/** 
	 * Copies the column information from the provided table and creates a new table for with the provided columns. Optionally a 
	 * name can be given which is useful for retrieval later.
	 */
	virtual TypedElementTableHandle RegisterTable(TypedElementTableHandle SourceTable, 
		TConstArrayView<const UScriptStruct*> ColumnList) = 0;
	virtual TypedElementTableHandle RegisterTable(TypedElementTableHandle SourceTable, 
		TConstArrayView<const UScriptStruct*> ColumnList, const FName Name) = 0;

	/** Returns a previously created table with the provided name or TypedElementInvalidTableHandle if not found. */
	virtual TypedElementTableHandle FindTable(const FName Name) = 0;
	
	/**
	 * @section Row management
	 */

	/** Adds a new row to the provided table. */
	virtual TypedElementRowHandle AddRow(TypedElementTableHandle Table) = 0;
	virtual TypedElementRowHandle AddRow(FName TableName) = 0;
	
	/**
	 * Add multiple rows at once. For each new row the OnCreated callback is called. Callers are expected to use the callback to
	 * initialize the row if needed.
	 */
	virtual bool BatchAddRow(TypedElementTableHandle Table, int32 Count, TypedElementDataStorageCreationCallbackRef OnCreated) = 0;
	virtual bool BatchAddRow(FName TableName, int32 Count, TypedElementDataStorageCreationCallbackRef OnCreated) = 0;
	
	/** Removes a previously added row. */
	virtual void RemoveRow(TypedElementRowHandle Row) = 0;

	
	/**
	 * @section Column management
	 */

	/** Adds a tag to a row or does nothing if already added. */
	virtual void AddTag(TypedElementRowHandle Row, const UScriptStruct* TagType) = 0;
	virtual void AddTag(TypedElementRowHandle Row, FTopLevelAssetPath TagName) = 0;
	
	/** Adds a new column to a row. If the column already exists it will be returned instead. */
	virtual void* AddOrGetColumnData(TypedElementRowHandle Row, const UScriptStruct* ColumnType) = 0;
	virtual ColumnDataResult AddOrGetColumnData(TypedElementRowHandle Row, FTopLevelAssetPath ColumnName) = 0;
	
	/**
	 * Sets the data of a column using the provided argument bag. This is only meant for simple initialization for 
	 * fragments that use UPROPERTY to expose properties. For complex initialization or when the fragment type is known
	 * it's recommende to use call that work directly on the type for better performance and a wider range of configuration options.
	 */
	virtual ColumnDataResult AddOrGetColumnData(TypedElementRowHandle Row, FTopLevelAssetPath ColumnName,
		TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments) = 0;
	
	/** Retrieves a pointer to the column of the given row or a nullptr if not found. */
	virtual void* GetColumnData(TypedElementRowHandle Row, const UScriptStruct* ColumnType) = 0;
	virtual ColumnDataResult GetColumnData(TypedElementRowHandle Row, FTopLevelAssetPath ColumnName) = 0;
	

	/**
	 * @section Query
	 * @description
	 * Queries can be constructed using the Query Builder. Note that the Query Builder allows for the creation of queries that
	 * are more complex than the backend may support. The backend is allowed to simplify the query, in which case the query
	 * can be used directly in the processor to do additional filtering. This will however impact performance and it's 
	 * therefore recommended to try to simplify the query first before relying on extended query filtering in a processor.
	 */
	
	enum class EQueryTickPhase : uint8
	{
		PrePhysics, //< Queries are executed before physics simulation starts.
		DuringPhysics, //< Queries that can be run in parallel with physics simulation work.
		PostPhysics, //< Queries that need rigid body and cloth simulation to be completed before being executed.
		FrameEnd, //< Catchall for queries demoted to the last possible moment.

		MAX
	};

	enum class EQueryTickGroups : uint8
	{
		/** The standard group to run work in. */
		Default,
		
		/**
		 * Prepares to sync data from an external source to the Data Storage. This is typically used for preparing columns with
		 * data that is needed to sync from external sources.
		 */
		PrepareSyncExternalToDataStorage,
		/** 
		 * The group for queries that need to sync data from external sources such as subsystems or the world into
		 * the Data Storage. These typically run early in a phase.
		 */
		SyncExternalToDataStorage,
		/**
		 * Finalizes the steps to sync data from an external source to the Data Storage. This is typically needed for processing 
		 * data with a dependency on external updates being completed.
		 */
		FinalizeSyncExternalToDataStorage,
		
		/** Prepares for data from the Data Storage to be synced to external sources such as subsystem or the world. */
		PrepareSyncDataStorageToExternal,
		/**
		 * The group for quries that need to sync data from the Data Storage to external sources such as subsystems
		 * or the world into. These typically run late in a phase.
		 */
		SyncDataStorageToExternal,
		/** Finalizes the sync of data from the Data Storage to external sources such as subsystem or the world. */
		FinalizeSyncDataStorageToExternal,

		/** Prepares for widgets getting synced to/from widgets. */
		PrepareSyncWidgets,
		/**
		 * Queries grouped under this name will sync data to/from widgets.
		 */
		SyncWidgets,
		/** Finalize for widgets getting synced to/from widgets. */
		FinalizeSyncWidgets
	};

	enum class EQueryCallbackType : uint8
	{
		/** No callback provided. */
		None,
		/** The query will be run every tick if at least one row matches. */
		Processor,
		/** The query will be run when a row is added that matches the query. The first recorded column will be actively monitored for changes. */
		ObserveAdd,
		/** The query will be run when a row is removed that matches the query. The first recorded column will be actively monitored for changes. */
		ObserveRemove
	};

	enum class EQueryAccessType : bool
	{ 
		ReadOnly, 
		ReadWrite 
	};

	enum class EQueryDependencyFlags : uint8
	{
		None = 0,
		/** If set the dependency is accessed as read-only. If not set the dependency requires Read/Write access. */
		ReadOnly = 1 << 0,
		/** If set the dependecy can only be used from the game thread, otherwise it can be accessed from any thread. */
		GameThreadBound = 1 << 1,
		/** If set the dependency will be refetched every iteration, otherwise only if not fetched before. */
		AlwaysRefresh = 1 << 2
	};

	/**
	 * Base interface for any contexts provided to query callbacks.
	 */
	struct ICommonQueryContext
	{
		virtual ~ICommonQueryContext() = default;

		/** Return the address of a immutable column matching the requested type or a nullptr if not found. */
		virtual const void* GetColumn(const UScriptStruct* ColumnType) const = 0;
		/** Return the address of a mutable column matching the requested type or a nullptr if not found. */
		virtual void* GetMutableColumn(const UScriptStruct* ColumnType) = 0;
		/**
		 * Get a list of columns or nullptrs if the columntype wasn't found. Mutable addresses are returned and it's up to
		 * the caller to not change immutable addresses.
		 */
		virtual void GetColumns(TArrayView<char*> RetrievedAddresses, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes,
			TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes) = 0;
		/**
		 * Get a list of columns or nullptrs if the columntype wasn't found. Mutable addresses are returned and it's up to
		 * the caller to not change immutable addresses. This version doesn't verify that the enough space is provided and
		 * it's up to the caller to guarantee the target addresses have enough space.
		 */
		virtual void GetColumnsUnguarded(int32 TypeCount, char** RetrievedAddresses, const TWeakObjectPtr<const UScriptStruct>* ColumnTypes,
			const ITypedElementDataStorageInterface::EQueryAccessType* AccessTypes) = 0;

		/** Returns the number rows in the batch. */
		virtual uint32 GetRowCount() const = 0;
		/**
		 * Returns an immutable view that contains the row handles for all returned results. The returned size will be the same  as the
		 * value returned by GetRowCount().
		 */
		virtual TConstArrayView<TypedElementRowHandle> GetRowHandles() const = 0;

		// Utility functions

		template<typename Column>
		const Column* GetColumn() const;
		template<typename Column>
		Column* GetMutableColumn();
	};
	
	/** 
	 * Interface to be provided to query callbacks running with the Data Storage.
	 * Note that at the time of writing only subclasses of Subsystem are supported as dependencies.
	 */
	struct IQueryContext : public ICommonQueryContext
	{
		virtual ~IQueryContext() = default;
		
		/** Returns an immutable instance of the requested dependency or a nullptr if not found. */
		virtual const UObject* GetDependency(const UClass* DependencyClass) = 0;
		/** Returns a mutable instance of the requested dependency or a nullptr if not found. */
		virtual UObject* GetMutableDependency(const UClass* DependencyClass) = 0;
		/** 
		 * Returns a list of dependencies or nullptrs if a dependency wasn't found. Mutable versions are return and it's up to the 
		 * caller to not change immutable dependencies.
		 */
		virtual void GetDependencies(TArrayView<UObject*> RetrievedAddresses, TConstArrayView<TWeakObjectPtr<const UClass>> DependencyTypes,
			TConstArrayView<EQueryAccessType> AccessTypes) = 0;

		/**
		 * Removes the row with the provided row handle. The removal will not be immediately done but delayed until the end of the tick 
		 * group.
		 */
		virtual void RemoveRow(TypedElementRowHandle Row) = 0;
		/**
		 * Removes rows with the provided row handles. The removal will not be immediately done but delayed until the end of the tick
		 * group.
		 */
		virtual void RemoveRows(TConstArrayView<TypedElementRowHandle> Rows) = 0;

		/**
		 * Adds new empty columns to a row of the provided type. The addition will not be immediately done but delayed until the end of the
		 * tick group.
		 */
		virtual void AddColumns(TypedElementRowHandle Row, TConstArrayView<const UScriptStruct*> ColumnTypes) = 0;
		/**
		 * Adds new empty columns to the listed rows of the provided type. The addition will not be immediately done but delayed until the end of the
		 * tick group.
		 */
		virtual void AddColumns(TConstArrayView<TypedElementRowHandle> Rows, TConstArrayView<const UScriptStruct*> ColumnTypes) = 0;
		/**
		 * Removes columns of the provided types from a row. The removal will not be immediately done but delayed until the end of the
		 * tick group.
		 */
		virtual void RemoveColumns(TypedElementRowHandle Row, TConstArrayView<const UScriptStruct*> ColumnTypes) = 0;
		/**
		 * Removes columns of the provided types from the listed rows. The removal will not be immediately done but delayed until the end of the
		 * tick group.
		 */
		virtual void RemoveColumns(TConstArrayView<TypedElementRowHandle> Rows, TConstArrayView<const UScriptStruct*> ColumnTypes) = 0;

		// Utility functions

		template<typename... Columns>
		void AddColumns(TypedElementRowHandle Row);
		template<typename... Columns>
		void AddColumns(TConstArrayView<TypedElementRowHandle> Rows);
		template<typename... Columns>
		void RemoveColumns(TypedElementRowHandle Row);
		template<typename... Columns>
		void RemoveColumns(TConstArrayView<TypedElementRowHandle> Rows);
	};

	/**
	 * Interface to be provided to query callbacks that are directly called through RunQuery.
	 */
	struct IDirectQueryContext : public ICommonQueryContext
	{
		virtual ~IDirectQueryContext() = default;
	};

	struct FQueryDescription;
	using QueryCallback = TFunction<void(const FQueryDescription&, IQueryContext&)>;
	using DirectQueryCallbackRef = TFunctionRef<void(const FQueryDescription&, IDirectQueryContext&)>;

	struct FQueryDescription final
	{
		static constexpr int32 NumInlineSelections = 8;
		static constexpr int32 NumInlineConditions = 8;
		static constexpr int32 NumInlineDependencies = 2;

		enum class EActionType : uint8
		{
			None,	/** Do nothing. */
			Select,	/** Selects a set of columns for further processing. */
			Count	/** Counts the number of entries that match the filter condition. */
		};

		using OperatorIndex = int32;
		enum class EOperatorType : uint16
		{
			SimpleAll,			// Unary: Type
			SimpleAny,			// Unary: Type
			SimpleNone,			// Unary: Type
			SimpleOptional,		// Unary: Type
			And,				// Binary: left operator index, right operator index
			Or,					// Binary: left operator index, right operator index
			Not,				// Unary: condition index
			Type				// Unary: Type
		};

		struct FBinaryOperator final
		{
			OperatorIndex Left;
			OperatorIndex Right;
		};
		
		union FOperator
		{
			FBinaryOperator Binary;
			OperatorIndex Unary;
			TWeakObjectPtr<const UScriptStruct> Type;
		};

		struct FCallbackData
		{
			QueryCallback Function;
			FName Name;
			FName Group;
			FName BeforeGroup;
			FName AfterGroup;
			const UScriptStruct* MonitoredType{ nullptr };
			EQueryCallbackType Type{ EQueryCallbackType::None };
			EQueryTickPhase Phase;
			bool bForceToGameThread{ false };
		};
		
		FCallbackData Callback;

		// The list of arrays below are required to remain in the same order as they're added as the function binding expects certain entries
		// to be in a specific location.

		TArray<TWeakObjectPtr<const UScriptStruct>, TInlineAllocator<NumInlineSelections>> SelectionTypes;
		TArray<EQueryAccessType, TInlineAllocator<NumInlineSelections>> SelectionAccessTypes;
		
		TArray<EOperatorType, TInlineAllocator<NumInlineConditions>> ConditionTypes;
		TArray<FOperator, TInlineAllocator<NumInlineConditions>> ConditionOperators;
		
		TArray<TWeakObjectPtr<const UClass>, TInlineAllocator<NumInlineDependencies>> DependencyTypes;
		TArray<EQueryDependencyFlags, TInlineAllocator<NumInlineDependencies>> DependencyFlags;
		/** Cached instances of the dependencies. This will always match the count of the other Dependency*Types, but may contain null pointers. */
		TArray<TWeakObjectPtr<UObject>, TInlineAllocator<NumInlineDependencies>> CachedDependencies;
		
		EActionType Action;
		/** If true, this query only has simple operations and is guaranteed to be executed fully and at optimal performance. */
		bool bSimpleQuery{ false };
	};
	virtual TypedElementQueryHandle RegisterQuery(FQueryDescription&& Query) = 0;
	virtual void UnregisterQuery(TypedElementQueryHandle Query) = 0;
	/** Returns the description of a previously registered query. If the query no longer exists an empty description will be returned. */
	virtual const FQueryDescription& GetQueryDescription(TypedElementQueryHandle Query) const = 0;
	/**
	 * Tick groups for queries can be given any name and the Data Storage will figure out the order of execution based on found
	 * dependencies. However keeping processors within the same query group can help promote better performance through parallelization.
	 * Therefore a collection of common tick group names is provided to help create consistent tick group names.
	 */
	virtual FName GetQueryTickGroupName(EQueryTickGroups Group) const = 0;
	struct FQueryResult
	{
		enum class ECompletion
		{
			/** Query could be fully executed. */
			Fully,
			/** Only portions of the query were executed. This is caused by a problem that was encountered partway through processing. */
			Partially,
			/** 
			 * The backend doesn't support the particular query. This may be a limitation in how/where the query is run or because
			 * the query contains actions and/or operations that are not supported.
			 */
			Unsupported,
			/** The provided query is no longer available. */
			Unavailable,
			/** One or more dependencies declared on the query could not be retrieved. */
			MissingDependency
		};
		
		uint32 Count{ 0 }; /** The number of rows were processed. */
		ECompletion Completed{ ECompletion::Unavailable };
	};
	virtual FQueryResult RunQuery(TypedElementQueryHandle Query) = 0;
	virtual FQueryResult RunQuery(TypedElementQueryHandle Query, DirectQueryCallbackRef Callback) = 0;
	
	/**
	 * @section Misc
	 */
	
	/**
	 * Called periodically when the storage is available. This provides an opportunity to do any repeated processing
	 * for the data storage.
	 */
	virtual FTypedElementOnDataStorageUpdate& OnUpdate() = 0;

	/**
	 * Whether or not the data storage is available. The data storage is available most of the time, but can be
	 * unavailable for a brief time between being destroyed and a new one created.
	 */
	virtual bool IsAvailable() const = 0;

	/** Returns a pointer to the registered external system if found, otherwise null. */
	virtual void* GetExternalSystemAddress(UClass* Target) = 0;

	

	/**
	 *
	 * The following are utility functions that are not part of the interface but are provided in order to make using the
	 * interface easier.
	 *
	 */
	
	
	/** Adds a tag to a row or does nothing if already added. */
	template<typename TagType>
	void AddTag(TypedElementRowHandle Row);
	
	/**
	 * Returns a pointer to the column of the given row or creates a new one if not found. Optionally arguments can be provided
	 * to update or initialize the column's data.
	 */
	template<typename ColumnType, typename... Args>
	ColumnType* AddOrGetColumn(TypedElementRowHandle Row, Args... Arguments);
	
	/** Returns a pointer to the column of the given row or a nullptr if the type couldn't be found or the row doesn't exist. */
	template<typename ColumnType>
	ColumnType* GetColumn(TypedElementRowHandle Row);

	/** Returns a pointer to the registered external system if found, otherwise null. */
	template<typename SystemType>
	SystemType* GetExternalSystem();
};



// Implementations

ENUM_CLASS_FLAGS(ITypedElementDataStorageInterface::EQueryDependencyFlags);

template<typename TagType>
void ITypedElementDataStorageInterface::AddTag(TypedElementRowHandle Row)
{
	AddTag(Row, TagType::StaticStruct());
}

template<typename ColumnType, typename... Args>
ColumnType* ITypedElementDataStorageInterface::AddOrGetColumn(TypedElementRowHandle Row, Args... Arguments)
{
	auto* Result = reinterpret_cast<ColumnType*>(AddOrGetColumnData(Row, ColumnType::StaticStruct()));
	if constexpr (sizeof...(Arguments) > 0)
	{
		if (Result)
		{
			new(Result) ColumnType{ Forward<Arguments>... };
		}
	}
	return Result;
}

template<typename Column>
const Column* ITypedElementDataStorageInterface::ICommonQueryContext::GetColumn() const
{
	return reinterpret_cast<const Column*>(GetColumn(Column::StaticStruct()));
}

template<typename Column>
Column* ITypedElementDataStorageInterface::ICommonQueryContext::GetMutableColumn()
{
	return reinterpret_cast<Column*>(GetMutableColumn(Column::StaticStruct()));
}

template<typename ColumnType>
ColumnType* ITypedElementDataStorageInterface::GetColumn(TypedElementRowHandle Row)
{
	return reinterpret_cast<ColumnType*>(GetColumnData(Row, ColumnType::StaticStruct()));
}

template<typename SystemType>
SystemType* ITypedElementDataStorageInterface::GetExternalSystem()
{
	return reinterpret_cast<SystemType*>(GetExternalSystemAddress(SystemType::StaticClass()));
}


template<typename... Columns>
void ITypedElementDataStorageInterface::IQueryContext::AddColumns(TypedElementRowHandle Row)
{
	AddColumns(Row, { Columns::StaticStruct()... });
}

template<typename... Columns>
void ITypedElementDataStorageInterface::IQueryContext::AddColumns(TConstArrayView<TypedElementRowHandle> Rows)
{
	AddColumns(Rows, { Columns::StaticStruct()... });
}

template<typename... Columns>
void ITypedElementDataStorageInterface::IQueryContext::RemoveColumns(TypedElementRowHandle Row)
{
	RemoveColumns(Row, { Columns::StaticStruct()... });
}

template<typename... Columns>
void ITypedElementDataStorageInterface::IQueryContext::RemoveColumns(TConstArrayView<TypedElementRowHandle> Rows)
{
	RemoveColumns(Rows, { Columns::StaticStruct()... });
}