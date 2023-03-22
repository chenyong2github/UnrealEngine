// Copyright Epic Games, Inc. All Rights Reserved.

#include "Processors/TypedElementProcessorAdaptors.h"

#include <utility>
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "TypedElementDatabase.h"

struct FMassContextForwarder final : public ITypedElementDataStorageInterface::FQueryContext
{
	explicit FMassContextForwarder(FMassExecutionContext& Context)
		: Context(Context)
		, RowCount(Context.GetNumEntities())
	{}

	const void* GetColumn(const UScriptStruct* ColumnType) const override
	{
		return Context.GetFragmentView(ColumnType).GetData();
	}

	void* GetMutableColumn(const UScriptStruct* ColumnType) override
	{
		return Context.GetMutableFragmentView(ColumnType).GetData();
	}

	void GetColumns(TArrayView<char*> RetrievedAddresses, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes,
		TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes) override
	{
		checkf(RetrievedAddresses.Num() == ColumnTypes.Num(), TEXT("Unable to retrieve a batch of columns as the number of addresses "
			"doesn't match the number of requested column."));
		checkf(RetrievedAddresses.Num() == AccessTypes.Num(), TEXT("Unable to retrieve a batch of columns as the number of addresses "
			"doesn't match the number of access types."));
		
		GetColumnsUnguarded(ColumnTypes.Num(), RetrievedAddresses.GetData(), ColumnTypes.GetData(), AccessTypes.GetData());
	}

	void GetColumnsUnguarded(int32 TypeCount, char** RetrievedAddresses, const TWeakObjectPtr<const UScriptStruct>* ColumnTypes,
		const ITypedElementDataStorageInterface::EQueryAccessType* AccessTypes) override
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
		return RowCount;
	}

	TConstArrayView<TypedElementRowHandle> GetRowHandles() const
	{
		static_assert(
			sizeof(TypedElementRowHandle) == sizeof(FMassEntityHandle) && alignof(TypedElementRowHandle) == alignof(FMassEntityHandle),
			"TypedElementRowHandle and FMassEntityHandle need to by layout compatible to support Typed Elements Data Storage.");
		TConstArrayView<FMassEntityHandle> Entities = Context.GetEntities();
		return TConstArrayView<TypedElementRowHandle>(reinterpret_cast<const TypedElementRowHandle*>(Entities.GetData()), Entities.Num());
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
	uint32 RowCount{ 0 };
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
	return ParentQuery ? ParentQuery->Callback.Name.ToString() : FString(TEXT("<unnamed>"));
}

void FTypedElementQueryProcessorData::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	checkf(ParentQuery, TEXT("A query callback was registered for execution without an associated query."));
	
	Query.ForEachEntityChunk(EntityManager, Context, 
		[this](FMassExecutionContext& Context)
		{
			FMassContextForwarder QueryContext(Context);
			ParentQuery->Callback.Function(QueryContext);
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

	bRequiresGameThreadExecution = TargetParentQuery.Callback.bForceToGameThread;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Editor); 
	ExecutionOrder.ExecuteInGroup = TargetParentQuery.Callback.Group;
	if (!TargetParentQuery.Callback.BeforeGroup.IsNone())
	{
		ExecutionOrder.ExecuteBefore.Add(TargetParentQuery.Callback.BeforeGroup);
	}
	if (!TargetParentQuery.Callback.AfterGroup.IsNone())
	{
		ExecutionOrder.ExecuteAfter.Add(TargetParentQuery.Callback.AfterGroup);
	}
	ProcessingPhase = Data.MapToMassProcessingPhase(TargetParentQuery.Callback.Phase);

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

	bRequiresGameThreadExecution = TargetParentQuery.Callback.bForceToGameThread;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Editor);
	
	ObservedType = const_cast<UScriptStruct*>(TargetParentQuery.Callback.MonitoredType);
	
	switch (TargetParentQuery.Callback.Type)
	{
	case ITypedElementDataStorageInterface::EQueryCallbackType::ObserveAdd:
		Operation = EMassObservedOperation::Add;
		break;
	case ITypedElementDataStorageInterface::EQueryCallbackType::ObserveRemove:
		Operation = EMassObservedOperation::Remove;
		break;
	default:
		checkf(false, TEXT("Query type %i is not supported from the observer processor adapter."),
			static_cast<int>(TargetParentQuery.Callback.Type));
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
