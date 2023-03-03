// Copyright Epic Games, Inc. All Rights Reserved.

#include "Processors/TypedElementProcessorAdaptors.h"

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

	void GetColumns(TArrayView<char*> RetrievedAddresses, TConstArrayView<const UScriptStruct*> ColumnTypes,
		TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes) override
	{
		checkf(RetrievedAddresses.Num() == ColumnTypes.Num(), TEXT("Unable to retrieve a batch of columns as the number of addresses "
			"doesn't match the number of requested column."));
		checkf(RetrievedAddresses.Num() == AccessTypes.Num(), TEXT("Unable to retrieve a batch of columns as the number of addresses "
			"doesn't match the number of access types."));
		
		GetColumnsUnguarded(ColumnTypes.Num(), RetrievedAddresses.GetData(), ColumnTypes.GetData(), AccessTypes.GetData());
	}

	void GetColumnsUnguarded(int32 TypeCount, char** RetrievedAddresses, const UScriptStruct* const* ColumnTypes,
		const ITypedElementDataStorageInterface::EQueryAccessType* AccessTypes) override
	{
		for (int32 Index = 0; Index < TypeCount; ++Index)
		{
			*RetrievedAddresses = *AccessTypes == ITypedElementDataStorageInterface::EQueryAccessType::ReadOnly
				? const_cast<char*>(reinterpret_cast<const char*>(Context.GetFragmentView(*ColumnTypes).GetData()))
				: reinterpret_cast<char*>(Context.GetMutableFragmentView(*ColumnTypes).GetData());

			++RetrievedAddresses;
			++ColumnTypes;
			++AccessTypes;
		}
	}

	USubsystem* GetMutableSubsystem(const TSubclassOf<USubsystem> SubsystemClass) override
	{
		return Context.GetMutableSubsystem<USubsystem>(SubsystemClass);
	}

	const USubsystem* GetSubsystem(const TSubclassOf<USubsystem> SubsystemClass) override
	{
		return Context.GetSubsystem<USubsystem>(SubsystemClass);
	}

	void GetSubsystems(TArrayView<char*> RetrievedAddresses, TConstArrayView<const TSubclassOf<USubsystem>> SubsystemTypes, 
		TConstArrayView<ITypedElementDataStorageInterface::EQueryAccessType> AccessTypes) override
	{
		checkf(RetrievedAddresses.Num() == SubsystemTypes.Num(), TEXT("Unable to retrieve a batch of subsystem as the number of addresses "
			"doesn't match the number of requested subsystem types."));

		GetSubsystemsUnguarded(RetrievedAddresses.Num(), RetrievedAddresses.GetData(), SubsystemTypes.GetData(), AccessTypes.GetData());
	}

	void GetSubsystemsUnguarded(int32 SubsystemCount, char** RetrievedAddresses, const TSubclassOf<USubsystem>* SubsystemTypes,
		const ITypedElementDataStorageInterface::EQueryAccessType* AccessTypes)
	{
		for (int32 Index = 0; Index < SubsystemCount; ++Index)
		{
			*RetrievedAddresses = *AccessTypes == ITypedElementDataStorageInterface::EQueryAccessType::ReadOnly
				? const_cast<char*>(reinterpret_cast<const char*>(Context.GetSubsystem<USubsystem>(*SubsystemTypes)))
				: reinterpret_cast<char*>(Context.GetMutableSubsystem<USubsystem>(*SubsystemTypes));

			++RetrievedAddresses;
			++SubsystemTypes;
			++AccessTypes;
		}
	}

	uint32 GetRowCount() const override
	{
		return RowCount;
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

FString FTypedElementQueryProcessorData::GetProcessorName(FString RootProcessorName) const
{
	if (ParentQuery)
	{
		FString Result = ParentQuery->Callback.Name.ToString();
		Result += TEXT(" [");
		Result += RootProcessorName;
		Result += TEXT("]");
		return Result;
	}
	else
	{
		return MoveTemp(RootProcessorName);
	}
}

void FTypedElementQueryProcessorData::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (ParentQuery)
	{
		Query.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
			{
				FMassContextForwarder QueryContext(Context);
				ParentQuery->Callback.Function(QueryContext);
			}
		);
	}
}



/**
 * UTypedElementQueryProcessorCallbackAdapterProcessor
 */

UTypedElementQueryProcessorCallbackAdapterProcessor::UTypedElementQueryProcessorCallbackAdapterProcessor()
	: Data(*this)
{
	bAllowMultipleInstances = true;
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
	return Data.GetProcessorName(Super::GetProcessorName());
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
	ProcessingPhase = Data.MapToMassProcessingPhase(TargetParentQuery.Callback.Phase);

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
	return Data.GetProcessorName(Super::GetProcessorName());
}

void UTypedElementQueryObserverCallbackAdapterProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	Data.Execute(EntityManager, Context);
}
