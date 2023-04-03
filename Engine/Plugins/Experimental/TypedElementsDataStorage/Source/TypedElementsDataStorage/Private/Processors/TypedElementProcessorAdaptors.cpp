// Copyright Epic Games, Inc. All Rights Reserved.

#include "Processors/TypedElementProcessorAdaptors.h"

#include <utility>
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "TypedElementDatabase.h"

struct FMassContextForwarderShared
{
	static const void* GetColumn(const FMassExecutionContext& Context, const UScriptStruct* ColumnType)
	{
		return Context.GetFragmentView(ColumnType).GetData();
	}

	static void* GetMutableColumn(FMassExecutionContext& Context, const UScriptStruct* ColumnType)
	{
		return Context.GetMutableFragmentView(ColumnType).GetData();
	}

	static void GetColumnsUnguarded(FMassExecutionContext& Context, int32 TypeCount, char** RetrievedAddresses,
		const TWeakObjectPtr<const UScriptStruct>* ColumnTypes, const ITypedElementDataStorageInterface::EQueryAccessType* AccessTypes)
	{
		for (int32 Index = 0; Index < TypeCount; ++Index)
		{
			checkf(ColumnTypes->IsValid(), TEXT("Attempting to retrieve a column that is not available."));
			*RetrievedAddresses = *AccessTypes == ITypedElementDataStorageInterface::EQueryAccessType::ReadOnly
				? const_cast<char*>(reinterpret_cast<const char*>(Context.GetFragmentView(ColumnTypes->Get()).GetData()))
				: reinterpret_cast<char*>(Context.GetMutableFragmentView(ColumnTypes->Get()).GetData());

			++RetrievedAddresses;
			++ColumnTypes;
			++AccessTypes;
		}
	}

	static void GetColumns(FMassExecutionContext& Context, TArrayView<char*> RetrievedAddresses, 
		TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes,
		TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes)
	{
		checkf(RetrievedAddresses.Num() == ColumnTypes.Num(), TEXT("Unable to retrieve a batch of columns as the number of addresses "
			"doesn't match the number of requested column."));
		checkf(RetrievedAddresses.Num() == AccessTypes.Num(), TEXT("Unable to retrieve a batch of columns as the number of addresses "
			"doesn't match the number of access types."));

		GetColumnsUnguarded(Context, ColumnTypes.Num(), RetrievedAddresses.GetData(), ColumnTypes.GetData(), AccessTypes.GetData());
	}

	static uint32 GetRowCount(const FMassExecutionContext& Context)
	{
		return Context.GetNumEntities();
	}

	static TConstArrayView<TypedElementRowHandle> GetRowHandles(const FMassExecutionContext& Context)
	{
		static_assert(
			sizeof(TypedElementRowHandle) == sizeof(FMassEntityHandle) && alignof(TypedElementRowHandle) == alignof(FMassEntityHandle),
			"TypedElementRowHandle and FMassEntityHandle need to by layout compatible to support Typed Elements Data Storage.");
		TConstArrayView<FMassEntityHandle> Entities = Context.GetEntities();
		return TConstArrayView<TypedElementRowHandle>(reinterpret_cast<const TypedElementRowHandle*>(Entities.GetData()), Entities.Num());
	}
};

struct FMassContextForwarder final : public ITypedElementDataStorageInterface::IQueryContext
{
	explicit FMassContextForwarder(FMassExecutionContext& Context)
		: Context(Context)
	{}

	~FMassContextForwarder() override = default;

	const void* GetColumn(const UScriptStruct* ColumnType) const override
	{
		return FMassContextForwarderShared::GetColumn(Context, ColumnType);
	}

	void* GetMutableColumn(const UScriptStruct* ColumnType) override
	{
		return FMassContextForwarderShared::GetMutableColumn(Context, ColumnType);
	}

	void GetColumns(TArrayView<char*> RetrievedAddresses, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes,
		TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes) override
	{
		FMassContextForwarderShared::GetColumns(Context, RetrievedAddresses, ColumnTypes, AccessTypes);
	}

	void GetColumnsUnguarded(int32 TypeCount, char** RetrievedAddresses, const TWeakObjectPtr<const UScriptStruct>* ColumnTypes,
		const ITypedElementDataStorageInterface::EQueryAccessType* AccessTypes) override
	{
		FMassContextForwarderShared::GetColumnsUnguarded(Context, TypeCount, RetrievedAddresses, ColumnTypes, AccessTypes);
	}

	UObject* GetMutableDependency(const UClass* DependencyClass) override
	{
		return Context.GetMutableSubsystem<USubsystem>(const_cast<UClass*>(DependencyClass));
	}

	const UObject* GetDependency(const UClass* DependencyClass) override
	{
		return Context.GetSubsystem<USubsystem>(const_cast<UClass*>(DependencyClass));
	}

	void GetDependencies(TArrayView<UObject*> RetrievedAddresses, TConstArrayView<TWeakObjectPtr<const UClass>> SubsystemTypes,
		TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes) override
	{
		checkf(RetrievedAddresses.Num() == SubsystemTypes.Num(), TEXT("Unable to retrieve a batch of subsystem as the number of addresses "
			"doesn't match the number of requested subsystem types."));

		GetDependenciesUnguarded(RetrievedAddresses.Num(), RetrievedAddresses.GetData(), SubsystemTypes.GetData(), AccessTypes.GetData());
	}

	void GetDependenciesUnguarded(int32 SubsystemCount, UObject** RetrievedAddresses, const TWeakObjectPtr<const UClass>* DependencyTypes,
		const ITypedElementDataStorageInterface::EQueryAccessType* AccessTypes)
	{
		for (int32 Index = 0; Index < SubsystemCount; ++Index)
		{
			checkf(DependencyTypes->IsValid(), TEXT("Attempting to retrieve a subsystem that's no longer valid."));
			*RetrievedAddresses = *AccessTypes == ITypedElementDataStorageInterface::EQueryAccessType::ReadOnly
				? const_cast<USubsystem*>(Context.GetSubsystem<USubsystem>(const_cast<UClass*>(DependencyTypes->Get())))
				: Context.GetMutableSubsystem<USubsystem>(const_cast<UClass*>(DependencyTypes->Get()));

			++RetrievedAddresses;
			++DependencyTypes;
			++AccessTypes;
		}
	}

	uint32 GetRowCount() const override
	{
		return FMassContextForwarderShared::GetRowCount(Context);
	}

	TConstArrayView<TypedElementRowHandle> GetRowHandles() const
	{
		return FMassContextForwarderShared::GetRowHandles(Context);
	}

	void RemoveRow(TypedElementRowHandle Row) override
	{
		Context.Defer().DestroyEntity(FMassEntityHandle::FromNumber(Row));
	}

	void RemoveRows(TConstArrayView<TypedElementRowHandle> Rows) override
	{
		for (TypedElementRowHandle Row : Rows)
		{
			Context.Defer().DestroyEntity(FMassEntityHandle::FromNumber(Row));
		}
	}

	void AddColumns(TypedElementRowHandle Row, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		for (const UScriptStruct* ColumnType : ColumnTypes)
		{
			bool bIsTag = ColumnType->IsChildOf(FMassTag::StaticStruct());

			checkf(ColumnType->IsChildOf(FMassFragment::StaticStruct()) || bIsTag,
				TEXT("Given struct type to add is not a valid fragment or tag type."));

			if (bIsTag)
			{
				Context.Defer().PushCommand<FMassDeferredAddCommand>(
					[EntityHandle = FMassEntityHandle::FromNumber(Row), ColumnType](FMassEntityManager& System)
					{
						System.AddTagToEntity(EntityHandle, ColumnType);
					});
			}
			else
			{
				Context.Defer().PushCommand<FMassDeferredAddCommand>(
					[EntityHandle = FMassEntityHandle::FromNumber(Row), ColumnType](FMassEntityManager& System)
					{
						System.AddFragmentToEntity(EntityHandle, ColumnType);
					});
			}
		}
	}

	void AddColumns(TConstArrayView<TypedElementRowHandle> Rows, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		for (TypedElementRowHandle Row : Rows)
		{
			AddColumns(Row, ColumnTypes);
		}
	}

	void RemoveColumns(TypedElementRowHandle Row, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		for (const UScriptStruct* ColumnType : ColumnTypes)
		{
			bool bIsTag = ColumnType->IsChildOf(FMassTag::StaticStruct());

			checkf(ColumnType->IsChildOf(FMassFragment::StaticStruct()) || bIsTag,
				TEXT("Given struct type to remove is not a valid fragment or tag type."));

			if (bIsTag)
			{
				Context.Defer().PushCommand<FMassDeferredAddCommand>(
					[EntityHandle = FMassEntityHandle::FromNumber(Row), ColumnType](FMassEntityManager& System)
					{
						System.RemoveTagFromEntity(EntityHandle, ColumnType);
					});
			}
			else
			{
				Context.Defer().PushCommand<FMassDeferredAddCommand>(
					[EntityHandle = FMassEntityHandle::FromNumber(Row), ColumnType](FMassEntityManager& System)
					{
						System.RemoveFragmentFromEntity(EntityHandle, ColumnType);
					});
			}
		}
	}

	void RemoveColumns(TConstArrayView<TypedElementRowHandle> Rows, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		for (TypedElementRowHandle Row : Rows)
		{
			RemoveColumns(Row, ColumnTypes);
		}
	}

	FMassExecutionContext& Context;
};

struct FMassDirectContextForwarder final : public ITypedElementDataStorageInterface::IDirectQueryContext
{
	explicit FMassDirectContextForwarder(FMassExecutionContext& Context)
		: Context(Context)
	{}

	~FMassDirectContextForwarder() override = default;

	FMassExecutionContext& Context;

	const void* GetColumn(const UScriptStruct* ColumnType) const override
	{
		return FMassContextForwarderShared::GetColumn(Context, ColumnType);
	}

	void* GetMutableColumn(const UScriptStruct* ColumnType) override
	{
		return FMassContextForwarderShared::GetMutableColumn(Context, ColumnType);
	}

	void GetColumns(TArrayView<char*> RetrievedAddresses, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes,
		TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes) override
	{
		FMassContextForwarderShared::GetColumns(Context, RetrievedAddresses, ColumnTypes, AccessTypes);
	}

	void GetColumnsUnguarded(int32 TypeCount, char** RetrievedAddresses, const TWeakObjectPtr<const UScriptStruct>* ColumnTypes,
		const ITypedElementDataStorageInterface::EQueryAccessType* AccessTypes) override
	{
		FMassContextForwarderShared::GetColumnsUnguarded(Context, TypeCount, RetrievedAddresses, ColumnTypes, AccessTypes);
	}

	uint32 GetRowCount() const override
	{
		return FMassContextForwarderShared::GetRowCount(Context);
	}

	TConstArrayView<TypedElementRowHandle> GetRowHandles() const
	{
		return FMassContextForwarderShared::GetRowHandles(Context);
	}
};


/**
 * FTypedElementQueryProcessorData
 */
FTypedElementQueryProcessorData::FTypedElementQueryProcessorData(UMassProcessor& Owner)
	: Query(Owner)
{
}

EMassProcessingPhase FTypedElementQueryProcessorData::MapToMassProcessingPhase(ITypedElementDataStorageInterface::EQueryTickPhase Phase) const
{
	switch(Phase)
	{
	case ITypedElementDataStorageInterface::EQueryTickPhase::PrePhysics:
		return EMassProcessingPhase::PrePhysics;
	case ITypedElementDataStorageInterface::EQueryTickPhase::DuringPhysics:
		return EMassProcessingPhase::DuringPhysics;
	case ITypedElementDataStorageInterface::EQueryTickPhase::PostPhysics:
		return EMassProcessingPhase::PostPhysics;
	case ITypedElementDataStorageInterface::EQueryTickPhase::FrameEnd:
		return EMassProcessingPhase::FrameEnd;
	default:
		checkf(false, TEXT("Query tick phase '%i' is unsupported."), static_cast<int>(Phase));
		return EMassProcessingPhase::MAX;
	};
}

FString FTypedElementQueryProcessorData::GetProcessorName() const
{
	return ParentQuery ? ParentQuery->Description.Callback.Name.ToString() : FString(TEXT("<unnamed>"));
}

bool FTypedElementQueryProcessorData::PrepareCachedDependenciesOnParentQuery(
	ITypedElementDataStorageInterface::FQueryDescription& Description, FMassExecutionContext& Context)
{
	const int32 DependencyCount = Description.DependencyTypes.Num();
	TWeakObjectPtr<const UClass>* Types = Description.DependencyTypes.GetData();
	ITypedElementDataStorageInterface::EQueryDependencyFlags* Flags = Description.DependencyFlags.GetData();
	TWeakObjectPtr<UObject>* Caches = Description.CachedDependencies.GetData();

	for (int32 Index = 0; Index < DependencyCount; ++Index)
	{
		checkf(Types->IsValid(), TEXT("Attempting to retrieve a dependency type that's no longer available."));
		
		if (EnumHasAnyFlags(*Flags, ITypedElementDataStorageInterface::EQueryDependencyFlags::AlwaysRefresh) || !Caches->IsValid())
		*Caches = EnumHasAnyFlags(*Flags, ITypedElementDataStorageInterface::EQueryDependencyFlags::ReadOnly)
			? const_cast<USubsystem*>(Context.GetSubsystem<USubsystem>(const_cast<UClass*>(Types->Get())))
			: Context.GetMutableSubsystem<USubsystem>(const_cast<UClass*>(Types->Get()));

		if (*Caches != nullptr)
		{
			++Types;
			++Flags;
			++Caches;
		}
		else
		{
			checkf(false, TEXT("Unable to retrieve instance of depencendy '%s'."), *((*Types)->GetName()));
			return false;
		}
	}
	return true;
}

ITypedElementDataStorageInterface::FQueryResult FTypedElementQueryProcessorData::Execute(
	ITypedElementDataStorageInterface::DirectQueryCallbackRef& Callback,
	ITypedElementDataStorageInterface::FQueryDescription& Description, 
	FMassEntityQuery& NativeQuery, 
	FMassEntityManager& EntityManager)
{
	FMassExecutionContext Context(EntityManager);
	ITypedElementDataStorageInterface::FQueryResult Result;
	Result.Completed = ITypedElementDataStorageInterface::FQueryResult::ECompletion::Fully;
	
	NativeQuery.ForEachEntityChunk(EntityManager, Context,
		[&Result, &Callback, &Description](FMassExecutionContext& Context)
		{
			if (PrepareCachedDependenciesOnParentQuery(Description, Context))
			{
				FMassDirectContextForwarder QueryContext(Context);
				Callback(Description, QueryContext);
				Result.Count += Context.GetNumEntities();
			}
			else
			{
				Result.Completed = ITypedElementDataStorageInterface::FQueryResult::ECompletion::MissingDependency;
			}
		}
	);
	return Result;
}

void FTypedElementQueryProcessorData::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	checkf(ParentQuery, TEXT("A query callback was registered for execution without an associated query."));
	
	ITypedElementDataStorageInterface::FQueryDescription& Description = ParentQuery->Description;
	Query.ForEachEntityChunk(EntityManager, Context,
		[&Description](FMassExecutionContext& Context)
		{
			if (PrepareCachedDependenciesOnParentQuery(Description, Context))
			{
				FMassContextForwarder QueryContext(Context);
				Description.Callback.Function(Description, QueryContext);
			}
		}
	);
}



/**
 * UTypedElementQueryProcessorCallbackAdapterProcessor
 */

UTypedElementQueryProcessorCallbackAdapterProcessor::UTypedElementQueryProcessorCallbackAdapterProcessor()
	: Data(*this)
{
	bAllowMultipleInstances = true;
	bAutoRegisterWithProcessingPhases = false;
}

FMassEntityQuery& UTypedElementQueryProcessorCallbackAdapterProcessor::GetQuery()
{
	return Data.Query;
}

void UTypedElementQueryProcessorCallbackAdapterProcessor::ConfigureQueryCallback(FTypedElementDatabaseExtendedQuery& TargetParentQuery)
{
	Data.ParentQuery = &TargetParentQuery;

	bRequiresGameThreadExecution = TargetParentQuery.Description.Callback.bForceToGameThread;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Editor); 
	ExecutionOrder.ExecuteInGroup = TargetParentQuery.Description.Callback.Group;
	if (!TargetParentQuery.Description.Callback.BeforeGroup.IsNone())
	{
		ExecutionOrder.ExecuteBefore.Add(TargetParentQuery.Description.Callback.BeforeGroup);
	}
	if (!TargetParentQuery.Description.Callback.AfterGroup.IsNone())
	{
		ExecutionOrder.ExecuteAfter.Add(TargetParentQuery.Description.Callback.AfterGroup);
	}
	ProcessingPhase = Data.MapToMassProcessingPhase(TargetParentQuery.Description.Callback.Phase);

	Super::PostInitProperties();
}

void UTypedElementQueryProcessorCallbackAdapterProcessor::ConfigureQueries()
{
	// When the extended query information is provided the native query will already be fully configured.
}

void UTypedElementQueryProcessorCallbackAdapterProcessor::PostInitProperties()
{
	Super::Super::PostInitProperties();
}

FString UTypedElementQueryProcessorCallbackAdapterProcessor::GetProcessorName() const
{
	FString Name = Data.GetProcessorName();
	Name += TEXT(" [Editor Processor]");
	return Name;
}

void UTypedElementQueryProcessorCallbackAdapterProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	Data.Execute(EntityManager, Context);
}



/**
 * UTypedElementQueryObserverCallbackAdapterProcessor
 */

UTypedElementQueryObserverCallbackAdapterProcessor::UTypedElementQueryObserverCallbackAdapterProcessor()
	: Data(*this)
{
	bAllowMultipleInstances = true;
	bAutoRegisterWithProcessingPhases = false;
}

FMassEntityQuery& UTypedElementQueryObserverCallbackAdapterProcessor::GetQuery()
{
	return Data.Query;
}

const UScriptStruct* UTypedElementQueryObserverCallbackAdapterProcessor::GetObservedType() const
{
	return ObservedType;
}

EMassObservedOperation UTypedElementQueryObserverCallbackAdapterProcessor::GetObservedOperation() const
{
	return Operation;
}

void UTypedElementQueryObserverCallbackAdapterProcessor::ConfigureQueryCallback(FTypedElementDatabaseExtendedQuery& TargetParentQuery)
{
	Data.ParentQuery = &TargetParentQuery;

	bRequiresGameThreadExecution = TargetParentQuery.Description.Callback.bForceToGameThread;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Editor);
	
	ObservedType = const_cast<UScriptStruct*>(TargetParentQuery.Description.Callback.MonitoredType);
	
	switch (TargetParentQuery.Description.Callback.Type)
	{
	case ITypedElementDataStorageInterface::EQueryCallbackType::ObserveAdd:
		Operation = EMassObservedOperation::Add;
		break;
	case ITypedElementDataStorageInterface::EQueryCallbackType::ObserveRemove:
		Operation = EMassObservedOperation::Remove;
		break;
	default:
		checkf(false, TEXT("Query type %i is not supported from the observer processor adapter."),
			static_cast<int>(TargetParentQuery.Description.Callback.Type));
		break;
	}

	Super::PostInitProperties();
}

void UTypedElementQueryObserverCallbackAdapterProcessor::ConfigureQueries()
{
	// When the extended query information is provided the native query will already be fully configured.
}

void UTypedElementQueryObserverCallbackAdapterProcessor::PostInitProperties()
{
	Super::Super::PostInitProperties();
}

void UTypedElementQueryObserverCallbackAdapterProcessor::Register()
{ 
	// Do nothing as this processor will be explicitly registered.
}

FString UTypedElementQueryObserverCallbackAdapterProcessor::GetProcessorName() const
{
	FString Name = Data.GetProcessorName();
	EMassObservedOperation ObservationType = GetObservedOperation();
	if (ObservationType == EMassObservedOperation::Add)
	{
		Name += TEXT(" [Editor Add Observer]");
	}
	else if (ObservationType == EMassObservedOperation::Remove)
	{
		Name += TEXT(" [Editor Remove Observer]");
	}
	else
	{
		Name += TEXT(" [Editor <Unknown> Observer]");
	}
	return Name;
}

void UTypedElementQueryObserverCallbackAdapterProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	Data.Execute(EntityManager, Context);
}
