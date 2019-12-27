// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertSyncSessionDatabase.h"
#include "ConcertFileCache.h"
#include "ConcertLogGlobal.h"
#include "ConcertUtil.h"

#include "SQLiteDatabase.h"
#include "HAL/FileManager.h"
#include "UObject/StructOnScope.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace TransactionDataUtil
{

const int32 MinFilesToCache = 10;
const uint64 MaxFileSizeBytesToCache = 50 * 1024 * 1024;
const int64 BucketSize = 500;
const uint32 DataVersion = 1;
const FGuid DataEntryFooter = FGuid(0xE473C070, 0x65DA42BF, 0xA0607C78, 0xE0DC47CF);

FString GetDataPath(const FString& InSessionPath)
{
	return InSessionPath / TEXT("Transactions");
}

FString GetDataFilename(const int64 InIndex)
{
	return FString::Printf(TEXT("%s/%s.utrans"), *LexToString(InIndex / BucketSize), *LexToString(InIndex));
}

bool WriteTransactionData(const FStructOnScope& InTransaction, TArray<uint8>& OutSerializedTransactionData)
{
	FMemoryWriter Ar(OutSerializedTransactionData);

	const UScriptStruct* TransactionType = CastChecked<const UScriptStruct>(InTransaction.GetStruct());

	FString TransactionTypeStr = TransactionType->GetPathName();
	Ar << TransactionTypeStr;
	const_cast<UScriptStruct*>(TransactionType)->SerializeItem(Ar, (void*)InTransaction.GetStructMemory(), nullptr);

	return !Ar.IsError();
}

bool WriteTransaction(const FStructOnScope& InTransaction, TArray<uint8>& OutSerializedTransactionData)
{
	check(InTransaction.IsValid());

	FMemoryWriter Ar(OutSerializedTransactionData);

	// Serialize the data version
	uint32 SerializedDataVersion = DataVersion;
	Ar.SerializeIntPacked(SerializedDataVersion);

	// Write the raw transaction data
	TArray<uint8> UncompressedTransaction;
	if (!WriteTransactionData(InTransaction, UncompressedTransaction))
	{
		return false;
	}

	// Serialize the raw transaction
	uint32 UncompressedTransactionSize = UncompressedTransaction.Num();
	Ar.SerializeIntPacked(UncompressedTransactionSize);
	if (UncompressedTransactionSize > 0)
	{
		Ar.SerializeCompressed(UncompressedTransaction.GetData(), UncompressedTransactionSize, NAME_Zlib);
	}

	// Serialize the footer so we know we didn't crash mid-write
	FGuid SerializedFooter = DataEntryFooter;
	Ar << SerializedFooter;

	return !Ar.IsError();
}

bool ReadTransactionData(const TArray<uint8>& InSerializedTransactionData, FStructOnScope& OutTransaction)
{
	FMemoryReader Ar(InSerializedTransactionData);

	// Deserialize the transaction
	UScriptStruct* TransactionType = nullptr;
	{
		FString TransactionTypeStr;
		Ar << TransactionTypeStr;
		TransactionType = LoadObject<UScriptStruct>(nullptr, *TransactionTypeStr);
		if (!TransactionType)
		{
			return false;
		}
	}
	if (OutTransaction.IsValid())
	{
		// If were given an existing transaction to fill with data, then the type must match
		if (TransactionType != OutTransaction.GetStruct())
		{
			return false;
		}
	}
	else
	{
		OutTransaction.Initialize(TransactionType);
	}
	TransactionType->SerializeItem(Ar, OutTransaction.GetStructMemory(), nullptr);

	return !Ar.IsError();
}

bool ReadTransaction(const TArray<uint8>& InSerializedTransactionData, FStructOnScope& OutTransaction)
{
	FMemoryReader Ar(InSerializedTransactionData);

	// Test the footer is in place so we know we didn't crash mid-write
	bool bParsedFooter = false;
	{
		const int64 SerializedTransactionSize = Ar.TotalSize();
		if (SerializedTransactionSize >= sizeof(FGuid))
		{
			FGuid SerializedFooter;
			Ar.Seek(SerializedTransactionSize - sizeof(FGuid));
			Ar << SerializedFooter;
			Ar.Seek(0);
			bParsedFooter = SerializedFooter == DataEntryFooter;
		}
	}
	if (!bParsedFooter)
	{
		return false;
	}

	// Deserialize the data version
	uint32 SerializedDataVersion = 0;
	Ar.SerializeIntPacked(SerializedDataVersion);

	// Deserialize the raw transaction
	uint32 UncompressedTransactionSize = 0;
	Ar.SerializeIntPacked(UncompressedTransactionSize);
	TArray<uint8> UncompressedTransaction;
	UncompressedTransaction.AddZeroed(UncompressedTransactionSize);
	if (UncompressedTransactionSize > 0)
	{
		Ar.SerializeCompressed(UncompressedTransaction.GetData(), UncompressedTransactionSize, NAME_Zlib);
	}

	// Read the raw transaction data
	if (!ReadTransactionData(UncompressedTransaction, OutTransaction))
	{
		return false;
	}

	return !Ar.IsError();
}

} // namespace TransactionDataUtil

namespace PackageDataUtil
{

const int32 MinFilesToCache = 10;
const uint32 DataVersion = 1;
const uint64 MaxFileSizeBytesToCache = 200 * 1024 * 1024;
const FGuid EntryFooter = FGuid(0x2EFC8CDD, 0x748E46C0, 0xA5485769, 0x13A3C354);

FString GetDataPath(const FString& InSessionPath)
{
	return InSessionPath / TEXT("Packages");
}

FString GetDataFilename(const FString InPackageName, const int64 InRevision)
{
	return FString::Printf(TEXT("%s_%s.upackage"), *InPackageName.ToLower(), *LexToString(InRevision));
}

FString GetDataFilename(const FName InPackageName, const int64 InRevision)
{
	return GetDataFilename(InPackageName.ToString(), InRevision);
}

bool WritePackage(const FConcertPackageInfo& InPackageInfo, const TArray<uint8>& InPackageData, TArray<uint8>& OutSerializedPackageData)
{
	FMemoryWriter Ar(OutSerializedPackageData);

	// Serialize the data version
	uint32 SerializedDataVersion = DataVersion;
	Ar.SerializeIntPacked(SerializedDataVersion);

	// Serialize the info (header)
	int64 BodyOffsetTell = Ar.Tell();
	int64 BodyOffset = 0;
	Ar << BodyOffset;
	FConcertPackageInfo::StaticStruct()->SerializeItem(Ar, const_cast<FConcertPackageInfo*>(&InPackageInfo), nullptr);

	// Serialize the raw data (body)
	BodyOffset = Ar.Tell();
	Ar.Seek(BodyOffsetTell);
	Ar << BodyOffset;
	Ar.Seek(BodyOffset);
	uint32 UncompressedPackageSize = InPackageData.Num();
	Ar.SerializeIntPacked(UncompressedPackageSize);
	if (UncompressedPackageSize > 0)
	{
		Ar.SerializeCompressed((uint8*)InPackageData.GetData(), UncompressedPackageSize, NAME_Zlib);
	}

	// Serialize the footer so we know we didn't crash mid-write
	FGuid SerializedFooter = EntryFooter;
	Ar << SerializedFooter;

	return !Ar.IsError();
}

bool ReadPackage(const TArray<uint8>& InSerializedPackageData, FConcertPackageInfo* OutPackageInfo, TArray<uint8>* OutPackageData)
{
	FMemoryReader Ar(InSerializedPackageData);

	// Test the footer is in place so we know we didn't crash mid-write
	bool bParsedFooter = false;
	{
		const int64 SerializedTransactionSize = Ar.TotalSize();
		if (SerializedTransactionSize >= sizeof(FGuid))
		{
			FGuid SerializedFooter;
			Ar.Seek(SerializedTransactionSize - sizeof(FGuid));
			Ar << SerializedFooter;
			Ar.Seek(0);
			bParsedFooter = SerializedFooter == EntryFooter;
		}
	}
	if (!bParsedFooter)
	{
		return false;
	}

	// Deserialize the data version
	uint32 SerializedDataVersion = 0;
	Ar.SerializeIntPacked(SerializedDataVersion);

	// Deserialize the info (header)
	int64 BodyOffset = 0;
	Ar << BodyOffset;
	if (OutPackageInfo)
	{
		FConcertPackageInfo::StaticStruct()->SerializeItem(Ar, OutPackageInfo, nullptr);
	}

	// Deserialize the raw data (body)
	if (OutPackageData)
	{
		Ar.Seek(BodyOffset);

		uint32 UncompressedPackageSize = 0;
		Ar.SerializeIntPacked(UncompressedPackageSize);
		OutPackageData->Reset(UncompressedPackageSize);
		OutPackageData->AddZeroed(UncompressedPackageSize);
		if (UncompressedPackageSize > 0)
		{
			Ar.SerializeCompressed(OutPackageData->GetData(), UncompressedPackageSize, NAME_Zlib);
		}
	}

	return !Ar.IsError();
}

} // namespace PackageDataUtil

enum class FConcertSyncSessionDatabaseVersion
{
	Empty = 0,
	Initial = 1,

	Current = Initial,
};

class FConcertSyncSessionDatabaseStatements
{
public:
	explicit FConcertSyncSessionDatabaseStatements(FSQLiteDatabase& InDatabase)
		: Database(InDatabase)
	{
		check(Database.IsValid());
	}

	bool CreatePreparedStatements()
	{
		check(Database.IsValid());

		#define PREPARE_STATEMENT(VAR)																		\
			(VAR) = Database.PrepareStatement<decltype(VAR)>(ESQLitePreparedStatementFlags::Persistent);	\
			if (!(VAR).IsValid()) { return false; }

		PREPARE_STATEMENT(Statement_BeginTransaction);
		PREPARE_STATEMENT(Statement_CommitTransaction);
		PREPARE_STATEMENT(Statement_RollbackTransaction);

		PREPARE_STATEMENT(Statement_AddObjectPathName);
		PREPARE_STATEMENT(Statement_GetObjectPathName);
		PREPARE_STATEMENT(Statement_GetObjectNameId);

		PREPARE_STATEMENT(Statement_AddPackageName);
		PREPARE_STATEMENT(Statement_GetPackageName);
		PREPARE_STATEMENT(Statement_GetPackageNameId);

		PREPARE_STATEMENT(Statement_GetPersistEventId);
		PREPARE_STATEMENT(Statement_AddPersistEvent);

		PREPARE_STATEMENT(Statement_SetEndpointData);
		PREPARE_STATEMENT(Statement_GetEndpointDataForId);
		PREPARE_STATEMENT(Statement_GetAllEndpointData);
		PREPARE_STATEMENT(Statement_GetAllEndpointIds);

		PREPARE_STATEMENT(Statement_AddConnectionEvent);
		PREPARE_STATEMENT(Statement_SetConnectionEvent);
		PREPARE_STATEMENT(Statement_GetConnectionEventForId);

		PREPARE_STATEMENT(Statement_AddLockEvent);
		PREPARE_STATEMENT(Statement_SetLockEvent);
		PREPARE_STATEMENT(Statement_GetLockEventForId);

		PREPARE_STATEMENT(Statement_SetTransactionEvent);
		PREPARE_STATEMENT(Statement_GetTransactionEventForId);
		PREPARE_STATEMENT(Statement_GetTransactionMaxEventId);

		PREPARE_STATEMENT(Statement_SetPackageEvent);
		PREPARE_STATEMENT(Statement_GetPackageEventForId);
		PREPARE_STATEMENT(Statement_GetPackageNameIdAndRevisonForId);
		PREPARE_STATEMENT(Statement_GetUniquePackageNameIdsForPackageEvents);
		PREPARE_STATEMENT(Statement_GetPackageMaxEventId);
		PREPARE_STATEMENT(Statement_GetPackageDataForRevision);
		PREPARE_STATEMENT(Statement_GetPackageHeadEventId);
		PREPARE_STATEMENT(Statement_GetPackageHeadEventIdAndTransactionIdAtSave);
		PREPARE_STATEMENT(Statement_GetMaxPackageEventIdAndTransactionEventIdAtSavePerPackageNameId);
		PREPARE_STATEMENT(Statement_GetPackageHeadRevison);
		PREPARE_STATEMENT(Statement_GetPackageTransactionEventIdAtLastSave);

		PREPARE_STATEMENT(Statement_AddActivityData);
		PREPARE_STATEMENT(Statement_SetActivityData);
		PREPARE_STATEMENT(Statement_GetActivityDataForId);
		PREPARE_STATEMENT(Statement_GetActivityDataForEvent);
		PREPARE_STATEMENT(Statement_GetActivityEventTypeForId);
		PREPARE_STATEMENT(Statement_GetAllActivityData);
		PREPARE_STATEMENT(Statement_GetAllActivityDataForEventType);
		PREPARE_STATEMENT(Statement_GetActivityDataInRange);
		PREPARE_STATEMENT(Statement_GetAllActivityIdAndEventTypes);
		PREPARE_STATEMENT(Statement_GetActivityIdAndEventTypesInRange);
		PREPARE_STATEMENT(Statement_GetActivityMaxId);
		
		PREPARE_STATEMENT(Statement_IgnoreActivity);
		PREPARE_STATEMENT(Statement_PerceiveActivity);
		PREPARE_STATEMENT(Statement_IsActivityIgnored);

		PREPARE_STATEMENT(Statement_MapObjectNameIdToLockEventId);
		PREPARE_STATEMENT(Statement_UnmapObjectNameIdsForLockEventId);
		PREPARE_STATEMENT(Statement_GetLockEventIdsForObjectNameId);
		PREPARE_STATEMENT(Statement_GetObjectNameIdsForLockEventId);

		PREPARE_STATEMENT(Statement_MapPackageNameIdToTransactionEventId);
		PREPARE_STATEMENT(Statement_UnmapPackageNameIdsForTransactionEventId);
		PREPARE_STATEMENT(Statement_GetTransactionEventIdsForPackageNameId);
		PREPARE_STATEMENT(Statement_GetTransactionEventIdsInRangeForPackageNameId);
		PREPARE_STATEMENT(Statement_GetPackageNameIdsMaxTransactionId);
		PREPARE_STATEMENT(Statement_GetPackageNameIdsWithTransactions);
		PREPARE_STATEMENT(Statement_GetPackageNameIdsForTransactionEventId);

		PREPARE_STATEMENT(Statement_MapObjectNameIdToTransactionEventId);
		PREPARE_STATEMENT(Statement_UnmapObjectNameIdsForTransactionEventId);
		PREPARE_STATEMENT(Statement_GetTransactionEventIdsForObjectNameId);

		#undef PREPARE_STATEMENT

		return true;
	}

	/**
	 * Statements managing database transactions
	 */

	/** Begin a database transaction */
	SQLITE_PREPARED_STATEMENT_SIMPLE(FBeginTransaction, "BEGIN TRANSACTION;");
	FBeginTransaction Statement_BeginTransaction;
	bool BeginTransaction()
	{
		return Statement_BeginTransaction.Execute();
	}

	/** Commit a database transaction */
	SQLITE_PREPARED_STATEMENT_SIMPLE(FCommitTransaction, "COMMIT TRANSACTION;");
	FCommitTransaction Statement_CommitTransaction;
	bool CommitTransaction()
	{
		return Statement_CommitTransaction.Execute();
	}

	/** Rollback a database transaction */
	SQLITE_PREPARED_STATEMENT_SIMPLE(FRollbackTransaction, "ROLLBACK TRANSACTION;");
	FRollbackTransaction Statement_RollbackTransaction;
	bool RollbackTransaction()
	{
		return Statement_RollbackTransaction.Execute();
	}

	/**
	 * Statements working on object_names
	 */

	/** Add a new object_path_name to object_names and get its object_name_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FAddObjectPathName, "INSERT INTO object_names(object_path_name) VALUES(?1);", SQLITE_PREPARED_STATEMENT_BINDINGS(FName));
	FAddObjectPathName Statement_AddObjectPathName;
	bool AddObjectPathName(const FName InObjectPathName, int64& OutObjectNameId)
	{
		if (Statement_AddObjectPathName.BindAndExecute(InObjectPathName))
		{
			OutObjectNameId = Database.GetLastInsertRowId();
			return true;
		}
		return false;
	}

	/** Get an object_path_name from object_names for the given object_name_id */
	SQLITE_PREPARED_STATEMENT(FGetObjectPathName, "SELECT object_path_name FROM object_names WHERE object_name_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(FName), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetObjectPathName Statement_GetObjectPathName;
	bool GetObjectPathName(const int64 InObjectNameId, FName& OutObjectPathName)
	{
		return Statement_GetObjectPathName.BindAndExecuteSingle(InObjectNameId, OutObjectPathName);
	}

	/** Get an object_name_id from object_names for the given object_path_name */
	SQLITE_PREPARED_STATEMENT(FGetObjectNameId, "SELECT object_name_id FROM object_names WHERE object_path_name = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(FName));
	FGetObjectNameId Statement_GetObjectNameId;
	bool GetObjectNameId(const FName InObjectPathName, int64& OutObjectNameId)
	{
		return Statement_GetObjectNameId.BindAndExecuteSingle(InObjectPathName, OutObjectNameId);
	}

	/**
	 * Statements working on package_names
	 */

	/** Add a new package_name to package_names and get its package_name_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FAddPackageName, "INSERT INTO package_names(package_name) VALUES(?1);", SQLITE_PREPARED_STATEMENT_BINDINGS(FName));
	FAddPackageName Statement_AddPackageName;
	bool AddPackageName(const FName InPackageName, int64& OutPackageNameId)
	{
		if (Statement_AddPackageName.BindAndExecute(InPackageName))
		{
			OutPackageNameId = Database.GetLastInsertRowId();
			return true;
		}
		return false;
	}

	/** Get an package_name from package_names for the given package_name_id */
	SQLITE_PREPARED_STATEMENT(FGetPackageName, "SELECT package_name FROM package_names WHERE package_name_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(FName), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetPackageName Statement_GetPackageName;
	bool GetPackageName(const int64 InPackageNameId, FName& OutPackageName)
	{
		return Statement_GetPackageName.BindAndExecuteSingle(InPackageNameId, OutPackageName);
	}

	/** Get a package_name_id from package_names for the given package_name */
	SQLITE_PREPARED_STATEMENT(FGetPackageNameId, "SELECT package_name_id FROM package_names WHERE package_name = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(FName));
	FGetPackageNameId Statement_GetPackageNameId;
	bool GetPackageNameId(const FName InPackageName, int64& OutPackageNameId)
	{
		return Statement_GetPackageNameId.BindAndExecuteSingle(InPackageName, OutPackageNameId);
	}

	/**
	 * Statements working on persist_events
	 */

	/** Get a persist_event_id and transaction_event_id_at_persist from persist_events for the given package_event_id */
	SQLITE_PREPARED_STATEMENT(FGetPersistEventId, "SELECT persist_event_id, transaction_event_id_at_persist FROM persist_events WHERE package_event_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetPersistEventId Statement_GetPersistEventId;
	bool GetPersistEventId(int64 InPackageEventId, int64& OutPersistEventId, int64& OutTransactionEventIdAtPersist)
	{
		return Statement_GetPersistEventId.BindAndExecuteSingle(InPackageEventId, OutPersistEventId, OutTransactionEventIdAtPersist);
	}

	/** Add a new package_event_id to persist_events and get its persist_event_id. */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FAddPersistEvent, "INSERT INTO persist_events(package_event_id, transaction_event_id_at_persist) VALUES(?1, ?2);", SQLITE_PREPARED_STATEMENT_BINDINGS(int64, int64));
	FAddPersistEvent Statement_AddPersistEvent;
	bool AddPersistEvent(int64 InPackageEventId, int64 InTransactionEventIdAtPersist, int64& OutPersistEventId)
	{
		if (Statement_AddPersistEvent.BindAndExecute(InPackageEventId, InTransactionEventIdAtPersist))
		{
			OutPersistEventId = Database.GetLastInsertRowId();
			return true;
		}
		return false;
	}

	/**
	 * Statements working on endpoints
	 */

	/** Set the endpoint data in endpoints for the given endpoint_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FSetEndpointData, "INSERT OR REPLACE INTO endpoints(endpoint_id, user_id, client_info_size_bytes, client_info_data) VALUES(?1, ?2, ?3, ?4);", SQLITE_PREPARED_STATEMENT_BINDINGS(FGuid, FString, int32, TArray<uint8>));
	FSetEndpointData Statement_SetEndpointData;
	bool SetEndpointData(const FGuid& InEndpointId, const FConcertClientInfo& InClientInfo)
	{
		FConcertSessionSerializedCborPayload ClientInfoPayload;
		verify(ClientInfoPayload.SetTypedPayload(InClientInfo));
		return Statement_SetEndpointData.BindAndExecute(InEndpointId, InClientInfo.UserName, ClientInfoPayload.UncompressedPayloadSize, ClientInfoPayload.CompressedPayload);
	}

	/** Set the endpoint data from endpoints for the given endpoint_id */
	SQLITE_PREPARED_STATEMENT(FGetEndpointDataForId, "SELECT client_info_size_bytes, client_info_data FROM endpoints WHERE endpoint_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int32, TArray<uint8>), SQLITE_PREPARED_STATEMENT_BINDINGS(FGuid));
	FGetEndpointDataForId Statement_GetEndpointDataForId;
	bool GetEndpointDataForId(const FGuid& InEndpointId, FConcertClientInfo& OutClientInfo)
	{
		FConcertSessionSerializedCborPayload ClientInfoPayload;
		ClientInfoPayload.PayloadTypeName = *FConcertClientInfo::StaticStruct()->GetPathName();
		if (Statement_GetEndpointDataForId.BindAndExecuteSingle(InEndpointId, ClientInfoPayload.UncompressedPayloadSize, ClientInfoPayload.CompressedPayload))
		{
			verify(ClientInfoPayload.GetTypedPayload(OutClientInfo));
			return true;
		}
		return false;
	}

	/** Get the endpoint data from endpoints for all endpoint_ids */
	SQLITE_PREPARED_STATEMENT_COLUMNS_ONLY(FGetAllEndpointData, "SELECT endpoint_id, client_info_size_bytes, client_info_data FROM endpoints ORDER BY endpoint_id;", SQLITE_PREPARED_STATEMENT_COLUMNS(FGuid, int32, TArray<uint8>));
	FGetAllEndpointData Statement_GetAllEndpointData;
	bool GetAllEndpointData(TFunctionRef<ESQLitePreparedStatementExecuteRowResult(const FGuid&, FConcertClientInfo&&)> InCallback)
	{
		FConcertSessionSerializedCborPayload ClientInfoPayload;
		ClientInfoPayload.PayloadTypeName = *FConcertClientInfo::StaticStruct()->GetPathName();
		return Statement_GetAllEndpointData.Execute([&ClientInfoPayload, &InCallback](const FGetAllEndpointData& InStatement)
		{
			FGuid EndpointId;
			if (InStatement.GetColumnValues(EndpointId, ClientInfoPayload.UncompressedPayloadSize, ClientInfoPayload.CompressedPayload))
			{
				FConcertClientInfo ClientInfo;
				verify(ClientInfoPayload.GetTypedPayload(ClientInfo));
				return InCallback(EndpointId, MoveTemp(ClientInfo));
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the endpoint_ids from all endpoints */
	SQLITE_PREPARED_STATEMENT_COLUMNS_ONLY(FGetAllEndpointIds, "SELECT endpoint_id FROM endpoints ORDER BY endpoint_id;", SQLITE_PREPARED_STATEMENT_COLUMNS(FGuid));
	FGetAllEndpointIds Statement_GetAllEndpointIds;
	bool GetAllEndpointIds(TFunctionRef<ESQLitePreparedStatementExecuteRowResult(const FGuid&)> InCallback)
	{
		return Statement_GetAllEndpointIds.Execute([&InCallback](const FGetAllEndpointIds& InStatement)
		{
			FGuid EndpointId;
			if (InStatement.GetColumnValues(EndpointId))
			{
				return InCallback(EndpointId);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/**
	 * Statements working on connection_events
	 */

	/** Add the connection event to connection_events and get its connection_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FAddConnectionEvent, "INSERT INTO connection_events(connection_event_type) VALUES(?1);", SQLITE_PREPARED_STATEMENT_BINDINGS(EConcertSyncConnectionEventType));
	FAddConnectionEvent Statement_AddConnectionEvent;
	bool AddConnectionEvent(const EConcertSyncConnectionEventType InConnectionEventType, int64& OutConnectionEventId)
	{
		if (Statement_AddConnectionEvent.BindAndExecute(InConnectionEventType))
		{
			OutConnectionEventId = Database.GetLastInsertRowId();
			return true;
		}
		return false;
	}

	/** Set the connection event in connection_events for the given connection_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FSetConnectionEvent, "INSERT OR REPLACE INTO connection_events(connection_event_id, connection_event_type) VALUES(?1, ?2);", SQLITE_PREPARED_STATEMENT_BINDINGS(int64, EConcertSyncConnectionEventType));
	FSetConnectionEvent Statement_SetConnectionEvent;
	bool SetConnectionEvent(const int64 InConnectionEventId, const EConcertSyncConnectionEventType InConnectionEventType)
	{
		return Statement_SetConnectionEvent.BindAndExecute(InConnectionEventId, InConnectionEventType);
	}

	/** Get the connection event from connection_events for the given connection_event_id */
	SQLITE_PREPARED_STATEMENT(FGetConnectionEventForId, "SELECT connection_event_type FROM connection_events WHERE connection_event_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(EConcertSyncConnectionEventType), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetConnectionEventForId Statement_GetConnectionEventForId;
	bool GetConnectionEventForId(const int64 InConnectionEventId, EConcertSyncConnectionEventType& OutConnectionEventType)
	{
		return Statement_GetConnectionEventForId.BindAndExecuteSingle(InConnectionEventId, OutConnectionEventType);
	}

	/**
	 * Statements working on lock_events
	 */

	/** Add the lock event to lock_events and get its lock_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FAddLockEvent, "INSERT INTO lock_events(lock_event_type) VALUES(?1);", SQLITE_PREPARED_STATEMENT_BINDINGS(EConcertSyncLockEventType));
	FAddLockEvent Statement_AddLockEvent;
	bool AddLockEvent(const EConcertSyncLockEventType InLockEventType, int64& OutLockEventId)
	{
		if (Statement_AddLockEvent.BindAndExecute(InLockEventType))
		{
			OutLockEventId = Database.GetLastInsertRowId();
			return true;
		}
		return false;
	}

	/** Set the lock event in lock_events for the given lock_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FSetLockEvent, "INSERT OR REPLACE INTO lock_events(lock_event_id, lock_event_type) VALUES(?1, ?2);", SQLITE_PREPARED_STATEMENT_BINDINGS(int64, EConcertSyncLockEventType));
	FSetLockEvent Statement_SetLockEvent;
	bool SetLockEvent(const int64 InLockEventId, const EConcertSyncLockEventType InLockEventType)
	{
		return Statement_SetLockEvent.BindAndExecute(InLockEventId, InLockEventType);
	}

	/** Get the lock event from lock_events for the given lock_event_id */
	SQLITE_PREPARED_STATEMENT(FGetLockEventForId, "SELECT lock_event_type FROM lock_events WHERE lock_event_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(EConcertSyncLockEventType), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetLockEventForId Statement_GetLockEventForId;
	bool GetLockEventForId(const int64 InLockEventId, EConcertSyncLockEventType& OutLockEventType)
	{
		return Statement_GetLockEventForId.BindAndExecuteSingle(InLockEventId, OutLockEventType);
	}

	/**
	 * Statements working on transaction_events
	 */

	/** Set the transaction event in transaction_events for the given transaction_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FSetTransactionEvent, "INSERT OR REPLACE INTO transaction_events(transaction_event_id, data_filename) VALUES(?1, ?2);", SQLITE_PREPARED_STATEMENT_BINDINGS(int64, FString));
	FSetTransactionEvent Statement_SetTransactionEvent;
	bool SetTransactionEvent(const int64 InTransactionEventId, const FString& InDataFilename)
	{
		return Statement_SetTransactionEvent.BindAndExecute(InTransactionEventId, InDataFilename);
	}

	/** Get the transaction event from transaction_events for the given transaction_event_id */
	SQLITE_PREPARED_STATEMENT(FGetTransactionEventForId, "SELECT data_filename FROM transaction_events WHERE transaction_event_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(FString), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetTransactionEventForId Statement_GetTransactionEventForId;
	bool GetTransactionEventForId(const int64 InTransactionEventId, FString& OutDataFilename)
	{
		return Statement_GetTransactionEventForId.BindAndExecuteSingle(InTransactionEventId, OutDataFilename);
	}

	/** Get the largest transaction_event_id currently in transaction_events */
	SQLITE_PREPARED_STATEMENT_COLUMNS_ONLY(FGetTransactionMaxEventId, "SELECT MAX(transaction_event_id) FROM transaction_events;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64));
	FGetTransactionMaxEventId Statement_GetTransactionMaxEventId;
	bool GetTransactionMaxEventId(int64& OutTransactionEventId)
	{
		return Statement_GetTransactionMaxEventId.ExecuteSingle(OutTransactionEventId);
	}

	/**
	 * Statements working on package_events
	 */
	
	/** Set the package event in package_events for the given package_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FSetPackageEvent, "INSERT OR REPLACE INTO package_events(package_event_id, package_name_id, package_revision, package_info_size_bytes, package_info_data, transaction_event_id_at_save, data_filename) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);", SQLITE_PREPARED_STATEMENT_BINDINGS(int64, int64, int64, int32, TArray<uint8>, int64, FString));
	FSetPackageEvent Statement_SetPackageEvent;
	bool SetPackageEvent(const int64 InPackageEventId, const int64 InPackageNameId, const int64 InPackageRevision, const int64 InTransactionEventIdAtSave, const FConcertPackageInfo& InPackageInfo, const FString& InDataFilename)
	{
		FConcertSessionSerializedCborPayload PackageInfoPayload;
		verify(PackageInfoPayload.SetTypedPayload(InPackageInfo));
		return Statement_SetPackageEvent.BindAndExecute(InPackageEventId, InPackageNameId, InPackageRevision, PackageInfoPayload.UncompressedPayloadSize, PackageInfoPayload.CompressedPayload, InTransactionEventIdAtSave, InDataFilename);
	}

	/** Get the package event from package_events for the given package_event_id */
	SQLITE_PREPARED_STATEMENT(FGetPackageEventForId, "SELECT package_revision, package_info_size_bytes, package_info_data, data_filename FROM package_events WHERE package_event_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, int32, TArray<uint8>, FString), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetPackageEventForId Statement_GetPackageEventForId;
	bool GetPackageEventForId(const int64 InPackageEventId, int64& OutPackageRevision, FConcertPackageInfo& OutPackageInfo, FString& OutDataFilename)
	{
		FConcertSessionSerializedCborPayload PackageInfoPayload;
		PackageInfoPayload.PayloadTypeName = *FConcertPackageInfo::StaticStruct()->GetPathName();
		if (Statement_GetPackageEventForId.BindAndExecuteSingle(InPackageEventId, OutPackageRevision, PackageInfoPayload.UncompressedPayloadSize, PackageInfoPayload.CompressedPayload, OutDataFilename))
		{
			verify(PackageInfoPayload.GetTypedPayload(OutPackageInfo));
			return true;
		}
		return false;
	}

	/** Get the package_name_id and package_revision from package_events for the given package_event_id */
	SQLITE_PREPARED_STATEMENT(FGetPackageNameIdAndRevisonForId, "SELECT package_name_id, package_revision FROM package_events WHERE package_event_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetPackageNameIdAndRevisonForId Statement_GetPackageNameIdAndRevisonForId;
	bool GetPackageNameIdAndRevisonForId(const int64 InPackageEventId, int64& OutPackageNameId, int64& OutPackageRevision)
	{
		return Statement_GetPackageNameIdAndRevisonForId.BindAndExecuteSingle(InPackageEventId, OutPackageNameId, OutPackageRevision);
	}

	/** Get the package_name_id from package_events for all unique package_name_ids */
	SQLITE_PREPARED_STATEMENT_COLUMNS_ONLY(FGetUniquePackageNameIdsForPackageEvents, "SELECT DISTINCT package_name_id FROM package_events;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64));
	FGetUniquePackageNameIdsForPackageEvents Statement_GetUniquePackageNameIdsForPackageEvents;
	bool GetUniquePackageNameIdsForPackageEvents(TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64)> InCallback)
	{
		return Statement_GetUniquePackageNameIdsForPackageEvents.Execute([&InCallback](const FGetUniquePackageNameIdsForPackageEvents& InStatement)
		{
			int64 PackageNameId = 0;
			if (InStatement.GetColumnValues(PackageNameId))
			{
				return InCallback(PackageNameId);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the largest package_event_id currently in package_events */
	SQLITE_PREPARED_STATEMENT_COLUMNS_ONLY(FGetPackageMaxEventId, "SELECT MAX(package_event_id) FROM package_events;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64));
	FGetPackageMaxEventId Statement_GetPackageMaxEventId;
	bool GetPackageMaxEventId(int64& OutPackageEventId)
	{
		return Statement_GetPackageMaxEventId.ExecuteSingle(OutPackageEventId);
	}

	/** Get the package data from package_events for the given package_name_id and package_revision */
	SQLITE_PREPARED_STATEMENT(FGetPackageDataForRevision, "SELECT package_info_size_bytes, package_info_data, data_filename FROM package_events WHERE package_name_id = ?1 AND package_revision = ?2;", SQLITE_PREPARED_STATEMENT_COLUMNS(int32, TArray<uint8>, FString), SQLITE_PREPARED_STATEMENT_BINDINGS(int64, int64));
	FGetPackageDataForRevision Statement_GetPackageDataForRevision;
	bool GetPackageDataForRevision(const int64 InPackageId, const int64 InPackageRevision, FConcertPackageInfo& OutPackageInfo, FString& OutDataFilename)
	{
		FConcertSessionSerializedCborPayload PackageInfoPayload;
		PackageInfoPayload.PayloadTypeName = *FConcertPackageInfo::StaticStruct()->GetPathName();
		if (Statement_GetPackageDataForRevision.BindAndExecuteSingle(InPackageId, InPackageRevision, PackageInfoPayload.UncompressedPayloadSize, PackageInfoPayload.CompressedPayload, OutDataFilename))
		{
			verify(PackageInfoPayload.GetTypedPayload(OutPackageInfo));
			return true;
		}
		return false;
	}

	/** Get the largest package_event_id currently in package_events for the given package_name_id */
	SQLITE_PREPARED_STATEMENT(FGetPackageHeadEventId, "SELECT MAX(package_event_id) FROM package_events WHERE package_name_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetPackageHeadEventId Statement_GetPackageHeadEventId;
	bool GetPackageHeadEventId(const int64 InPackageNameId, int64& OutPackageEventId)
	{
		return Statement_GetPackageHeadEventId.BindAndExecuteSingle(InPackageNameId, OutPackageEventId);
	}

	/** Get the largest package_event_id and its transaction_event_id_at_save currently in package_events for the given package_name_id */
	SQLITE_PREPARED_STATEMENT(FGetPackageHeadEventIdAndTransactionIdAtSave, "SELECT MAX(package_event_id), transaction_event_id_at_save FROM package_events WHERE package_name_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetPackageHeadEventIdAndTransactionIdAtSave Statement_GetPackageHeadEventIdAndTransactionIdAtSave;
	bool GetPackageHeadEventIdAndTransactionIdAtSave(const int64 InPackageNameId, int64& OutPackageEventId, int64& OutTransactionEventIdAtSave)
	{
		return Statement_GetPackageHeadEventIdAndTransactionIdAtSave.BindAndExecuteSingle(InPackageNameId, OutPackageEventId, OutTransactionEventIdAtSave);
	}

	/** Get the largest package_event_id along its transaction_event_id_at_save currently in package_events for each distinct package_name_id */
	SQLITE_PREPARED_STATEMENT_COLUMNS_ONLY(FGetMaxPackageEventIdAndTransactionEventIdAtSavePerPackageNameId, "SELECT package_name_id, MAX(package_event_id), transaction_event_id_at_save FROM package_events GROUP BY package_name_id;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, int64, int64));
	FGetMaxPackageEventIdAndTransactionEventIdAtSavePerPackageNameId Statement_GetMaxPackageEventIdAndTransactionEventIdAtSavePerPackageNameId;
	bool GetMaxPackageEventIdAndTransactionEventIdAtSavePerPackageNameId(TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64, int64, int64)> InCallback)
	{
		return Statement_GetMaxPackageEventIdAndTransactionEventIdAtSavePerPackageNameId.Execute([&InCallback](const FGetMaxPackageEventIdAndTransactionEventIdAtSavePerPackageNameId& InStatement)
		{
			int64 PackageNameId = 0, MaxPackageEventId = 0, TransactionEventIdAtSave = 0;
			if (InStatement.GetColumnValues(PackageNameId, MaxPackageEventId, TransactionEventIdAtSave))
			{
				return InCallback(PackageNameId, MaxPackageEventId, TransactionEventIdAtSave);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the largest package_revision currently in package_events for the given package_name_id */
	SQLITE_PREPARED_STATEMENT(FGetPackageHeadRevison, "SELECT MAX(package_revision) FROM package_events WHERE package_name_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetPackageHeadRevison Statement_GetPackageHeadRevison;
	bool GetPackageHeadRevision(const int64 InPackageNameId, int64& OutRevision)
	{
		return Statement_GetPackageHeadRevison.BindAndExecuteSingle(InPackageNameId, OutRevision);
	}

	/** Get the largest transaction_event_id_at_save currently in package_events for the given package_name_id */
	SQLITE_PREPARED_STATEMENT(FGetPackageTransactionEventIdAtLastSave, "SELECT MAX(transaction_event_id_at_save) FROM package_events WHERE package_name_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetPackageTransactionEventIdAtLastSave Statement_GetPackageTransactionEventIdAtLastSave;
	bool GetPackageTransactionEventIdAtLastSave(const int64 InPackageNameId, int64& OutTransactionEventId)
	{
		return Statement_GetPackageTransactionEventIdAtLastSave.BindAndExecuteSingle(InPackageNameId, OutTransactionEventId);
	}

	/**
	 * Statements working on activities
	 */
	
	/** Add the activity data to activities and get its activity_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FAddActivityData, "INSERT INTO activities(endpoint_id, event_time, event_type, event_id, event_summary_type, event_summary_size_bytes, event_summary_data) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);", SQLITE_PREPARED_STATEMENT_BINDINGS(FGuid, FDateTime, EConcertSyncActivityEventType, int64, FName, int32, TArray<uint8>));
	FAddActivityData Statement_AddActivityData;
	bool AddActivityData(const FGuid& InEndpointId, const EConcertSyncActivityEventType InEventType, const int64 InEventId, const FConcertSessionSerializedCborPayload& InEventSummary, int64& OutActivityId)
	{
		if (Statement_AddActivityData.BindAndExecute(InEndpointId, FDateTime::UtcNow(), InEventType, InEventId, InEventSummary.PayloadTypeName, InEventSummary.UncompressedPayloadSize, InEventSummary.CompressedPayload))
		{
			OutActivityId = Database.GetLastInsertRowId();
			return true;
		}
		return false;
	}

	/** Set the activity data in activities for the given activity_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FSetActivityData, "INSERT OR REPLACE INTO activities(activity_id, endpoint_id, event_time, event_type, event_id, event_summary_type, event_summary_size_bytes, event_summary_data) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);", SQLITE_PREPARED_STATEMENT_BINDINGS(int64, FGuid, FDateTime, EConcertSyncActivityEventType, int64, FName, int32, TArray<uint8>));
	FSetActivityData Statement_SetActivityData;
	bool SetActivityData(const int64 InActivityId, const FGuid& InEndpointId, const FDateTime InEventTime, const EConcertSyncActivityEventType InEventType, const int64 InEventId, const FConcertSessionSerializedCborPayload& InEventSummary)
	{
		return Statement_SetActivityData.BindAndExecute(InActivityId, InEndpointId, InEventTime, InEventType, InEventId, InEventSummary.PayloadTypeName, InEventSummary.UncompressedPayloadSize, InEventSummary.CompressedPayload);
	}

	/** Get the activity data from activities for the given activity_id */
	SQLITE_PREPARED_STATEMENT(FGetActivityDataForId, "SELECT endpoint_id, event_time, event_type, event_id, event_summary_type, event_summary_size_bytes, event_summary_data FROM activities WHERE activity_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(FGuid, FDateTime, EConcertSyncActivityEventType, int64, FName, int32, TArray<uint8>), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetActivityDataForId Statement_GetActivityDataForId;
	bool GetActivityDataForId(const int64 InActivityId, FGuid& OutEndpointId, FDateTime& OutEventTime, EConcertSyncActivityEventType& OutEventType, int64& OutEventId, FConcertSessionSerializedCborPayload& OutEventSummary)
	{
		return Statement_GetActivityDataForId.BindAndExecuteSingle(InActivityId, OutEndpointId, OutEventTime, OutEventType, OutEventId, OutEventSummary.PayloadTypeName, OutEventSummary.UncompressedPayloadSize, OutEventSummary.CompressedPayload);
	}

	/** Get the activity data from activities for the given event_id and event_type */
	SQLITE_PREPARED_STATEMENT(FGetActivityDataForEvent, "SELECT activity_id, endpoint_id, event_time, event_summary_type, event_summary_size_bytes, event_summary_data FROM activities WHERE event_id = ?1 AND event_type = ?2;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, FGuid, FDateTime, FName, int32, TArray<uint8>), SQLITE_PREPARED_STATEMENT_BINDINGS(int64, EConcertSyncActivityEventType));
	FGetActivityDataForEvent Statement_GetActivityDataForEvent;
	bool GetActivityDataForEvent(const int64 InEventId, const EConcertSyncActivityEventType InEventType, int64& OutActivityId, FGuid& OutEndpointId, FDateTime& OutEventTime, FConcertSessionSerializedCborPayload& OutEventSummary)
	{
		return Statement_GetActivityDataForEvent.BindAndExecuteSingle(InEventId, InEventType, OutActivityId, OutEndpointId, OutEventTime, OutEventSummary.PayloadTypeName, OutEventSummary.UncompressedPayloadSize, OutEventSummary.CompressedPayload);
	}

	/** Get the event_type from activities for the given activity_id */
	SQLITE_PREPARED_STATEMENT(FGetActivityEventTypeForId, "SELECT event_type FROM activities WHERE activity_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(EConcertSyncActivityEventType), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetActivityEventTypeForId Statement_GetActivityEventTypeForId;
	bool GetActivityEventTypeForId(const int64 InActivityId, EConcertSyncActivityEventType& OutEventType)
	{
		return Statement_GetActivityEventTypeForId.BindAndExecuteSingle(InActivityId, OutEventType);
	}

	/** Get the activity data from activities for all activity_ids */
	SQLITE_PREPARED_STATEMENT_COLUMNS_ONLY(FGetAllActivityData, "SELECT activity_id, endpoint_id, event_time, event_type, event_id, event_summary_type, event_summary_size_bytes, event_summary_data FROM activities ORDER BY activity_id;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, FGuid, FDateTime, EConcertSyncActivityEventType, int64, FName, int32, TArray<uint8>));
	FGetAllActivityData Statement_GetAllActivityData;
	bool GetAllActivityData(TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64, const FGuid&, FDateTime, EConcertSyncActivityEventType, int64, FConcertSessionSerializedCborPayload&&)> InCallback)
	{
		return Statement_GetAllActivityData.Execute([&InCallback](const FGetAllActivityData& InStatement)
		{
			int64 ActivityId = 0;
			FGuid EndpointId;
			FDateTime EventTime;
			EConcertSyncActivityEventType EventType = EConcertSyncActivityEventType::Connection;
			int64 EventId = 0;
			FConcertSessionSerializedCborPayload EventSummary;
			if (InStatement.GetColumnValues(ActivityId, EndpointId, EventTime, EventType, EventId, EventSummary.PayloadTypeName, EventSummary.UncompressedPayloadSize, EventSummary.CompressedPayload))
			{
				return InCallback(ActivityId, EndpointId, EventTime, EventType, EventId, MoveTemp(EventSummary));
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the activity data from activities for all activities of event_type */
	SQLITE_PREPARED_STATEMENT(FGetAllActivityDataForEventType, "SELECT activity_id, endpoint_id, event_time, event_type, event_id, event_summary_type, event_summary_size_bytes, event_summary_data FROM activities WHERE event_type = ?1 ORDER BY activity_id;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, FGuid, FDateTime, int64, FName, int32, TArray<uint8>), SQLITE_PREPARED_STATEMENT_BINDINGS(EConcertSyncActivityEventType));
	FGetAllActivityDataForEventType Statement_GetAllActivityDataForEventType;
	bool GetAllActivityDataForEventType(const EConcertSyncActivityEventType InEventType, TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64, const FGuid&, FDateTime, int64, FConcertSessionSerializedCborPayload&&)> InCallback)
	{
		return Statement_GetAllActivityDataForEventType.BindAndExecute(InEventType, [&InCallback](const FGetAllActivityDataForEventType& InStatement)
		{
			int64 ActivityId = 0;
			FGuid EndpointId;
			FDateTime EventTime;
			int64 EventId = 0;
			FConcertSessionSerializedCborPayload EventSummary;
			if (InStatement.GetColumnValues(ActivityId, EndpointId, EventTime, EventId, EventSummary.PayloadTypeName, EventSummary.UncompressedPayloadSize, EventSummary.CompressedPayload))
			{
				return InCallback(ActivityId, EndpointId, EventTime, EventId, MoveTemp(EventSummary));
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the activity data from activities for all activities in the given range */
	SQLITE_PREPARED_STATEMENT(FGetActivityDataInRange, "SELECT activity_id, endpoint_id, event_time, event_type, event_id, event_summary_type, event_summary_size_bytes, event_summary_data FROM activities WHERE activity_id >= ?1 ORDER BY activity_id LIMIT ?2;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, FGuid, FDateTime, EConcertSyncActivityEventType, int64, FName, int32, TArray<uint8>), SQLITE_PREPARED_STATEMENT_BINDINGS(int64, int64));
	FGetActivityDataInRange Statement_GetActivityDataInRange;
	bool GetActivityDataInRange(const int64 InFirstActivityId, const int64 InMaxNumActivities, TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64, const FGuid&, FDateTime, EConcertSyncActivityEventType, int64, FConcertSessionSerializedCborPayload&&)> InCallback)
	{
		return Statement_GetActivityDataInRange.BindAndExecute(InFirstActivityId, InMaxNumActivities, [&InCallback](const FGetActivityDataInRange& InStatement)
		{
			int64 ActivityId = 0;
			FGuid EndpointId;
			FDateTime EventTime;
			EConcertSyncActivityEventType EventType = EConcertSyncActivityEventType::Connection;
			int64 EventId = 0;
			FConcertSessionSerializedCborPayload EventSummary;
			if (InStatement.GetColumnValues(ActivityId, EndpointId, EventTime, EventType, EventId, EventSummary.PayloadTypeName, EventSummary.UncompressedPayloadSize, EventSummary.CompressedPayload))
			{
				return InCallback(ActivityId, EndpointId, EventTime, EventType, EventId, MoveTemp(EventSummary));
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the activity_id and event_type from activities for all activities */
	SQLITE_PREPARED_STATEMENT_COLUMNS_ONLY(FGetAllActivityIdAndEventTypes, "SELECT activity_id, event_type FROM activities ORDER BY activity_id;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, EConcertSyncActivityEventType));
	FGetAllActivityIdAndEventTypes Statement_GetAllActivityIdAndEventTypes;
	bool GetAllActivityIdAndEventTypes(TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64, EConcertSyncActivityEventType)> InCallback)
	{
		return Statement_GetAllActivityIdAndEventTypes.Execute([&InCallback](const FGetAllActivityIdAndEventTypes& InStatement)
		{
			int64 ActivityId = 0;
			EConcertSyncActivityEventType EventType = EConcertSyncActivityEventType::Connection;
			if (InStatement.GetColumnValues(ActivityId, EventType))
			{
				return InCallback(ActivityId, EventType);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the activity_id and event_type from activities for all activities in the given range */
	SQLITE_PREPARED_STATEMENT(FGetActivityIdAndEventTypesInRange, "SELECT activity_id, event_type FROM activities WHERE activity_id >= ?1 ORDER BY activity_id LIMIT ?2;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, EConcertSyncActivityEventType), SQLITE_PREPARED_STATEMENT_BINDINGS(int64, int64));
	FGetActivityIdAndEventTypesInRange Statement_GetActivityIdAndEventTypesInRange;
	bool GetActivityIdAndEventTypesInRange(const int64 InFirstActivityId, const int64 InMaxNumActivities, TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64, EConcertSyncActivityEventType)> InCallback)
	{
		return Statement_GetActivityIdAndEventTypesInRange.BindAndExecute(InFirstActivityId, InMaxNumActivities, [&InCallback](const FGetActivityIdAndEventTypesInRange& InStatement)
		{
			int64 ActivityId = 0;
			EConcertSyncActivityEventType EventType = EConcertSyncActivityEventType::Connection;
			if (InStatement.GetColumnValues(ActivityId, EventType))
			{
				return InCallback(ActivityId, EventType);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the largest activity_id currently in activities */
	SQLITE_PREPARED_STATEMENT_COLUMNS_ONLY(FGetActivityMaxId, "SELECT MAX(activity_id) FROM activities;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64));
	FGetActivityMaxId Statement_GetActivityMaxId;
	bool GetActivityMaxId(int64& OutActivityId)
	{
		return Statement_GetActivityMaxId.ExecuteSingle(OutActivityId);
	}

	/**
	 * Statements working on ignored_activities
	 */
	
	/** Add the activity_id to ignored_activities */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FIgnoreActivity, "INSERT OR REPLACE INTO ignored_activities(activity_id) VALUES(?1);", SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FIgnoreActivity Statement_IgnoreActivity;
	bool IgnoreActivity(const int64 InActivityId)
	{
		return Statement_IgnoreActivity.BindAndExecute(InActivityId);
	}

	/** Remove the activity_id from ignored_activities */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FPerceiveActivity, "DELETE FROM ignored_activities WHERE activity_id = ?1;", SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FPerceiveActivity Statement_PerceiveActivity;
	bool PerceiveActivity(const int64 InActivityId)
	{
		return Statement_PerceiveActivity.BindAndExecute(InActivityId);
	}

	/** See if the given activity_id is in ignored_activities */
	SQLITE_PREPARED_STATEMENT(FIsActivityIgnored, "SELECT activity_id FROM ignored_activities WHERE activity_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FIsActivityIgnored Statement_IsActivityIgnored;
	bool IsActivityIgnored(const int64 InActivityId)
	{
		int64 OutActivityId = 0;
		return Statement_IsActivityIgnored.BindAndExecuteSingle(InActivityId, OutActivityId);
	}

	/**
	 * Statements working on resource_locks
	 */

	/** Map the object_name_id in resource_locks to the the given lock_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FMapObjectNameIdToLockEventId, "INSERT INTO resource_locks(object_name_id, lock_event_id) VALUES(?1, ?2);", SQLITE_PREPARED_STATEMENT_BINDINGS(int64, int64));
	FMapObjectNameIdToLockEventId Statement_MapObjectNameIdToLockEventId;
	bool MapObjectNameIdToLockEventId(const int64 InObjectNameId, const int64 InLockEventId)
	{
		return Statement_MapObjectNameIdToLockEventId.BindAndExecute(InObjectNameId, InLockEventId);
	}

	/** Unmap all object_name_id entries from resource_locks for the given lock_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FUnmapObjectNameIdsForLockEventId, "DELETE FROM resource_locks WHERE lock_event_id = ?1;", SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FUnmapObjectNameIdsForLockEventId Statement_UnmapObjectNameIdsForLockEventId;
	bool UnmapObjectNameIdsForLockEventId(const int64 InLockEventId)
	{
		return Statement_UnmapObjectNameIdsForLockEventId.BindAndExecute(InLockEventId);
	}

	/** Get the lock_event_id entries from resource_locks for the given object_name_id */
	SQLITE_PREPARED_STATEMENT(FGetLockEventIdsForObjectNameId, "SELECT lock_event_id FROM resource_locks WHERE object_name_id = ?1 ORDER BY lock_event_id;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetLockEventIdsForObjectNameId Statement_GetLockEventIdsForObjectNameId;
	bool GetLockEventIdsForObjectNameId(const int64 InObjectNameId, TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64)> InCallback)
	{
		return Statement_GetLockEventIdsForObjectNameId.BindAndExecute(InObjectNameId, [&InCallback](const FGetLockEventIdsForObjectNameId& InStatement)
		{
			int64 LockEventId = 0;
			if (InStatement.GetColumnValues(LockEventId))
			{
				return InCallback(LockEventId);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the object_name_ids from resource_locks for the given lock_event_id */
	SQLITE_PREPARED_STATEMENT(FGetObjectNameIdsForLockEventId, "SELECT object_name_id FROM resource_locks WHERE lock_event_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetObjectNameIdsForLockEventId Statement_GetObjectNameIdsForLockEventId;
	bool GetObjectNameIdsForLockEventId(const int64 InLockEventId, TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64)> InCallback)
	{
		return Statement_GetObjectNameIdsForLockEventId.BindAndExecute(InLockEventId, [&InCallback](const FGetObjectNameIdsForLockEventId& InStatement)
		{
			int64 ObjectNameId = 0;
			if (InStatement.GetColumnValues(ObjectNameId))
			{
				return InCallback(ObjectNameId);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/**
	 * Statements working on package_transactions
	 */

	/** Map the package_name_id in package_transactions to the the given transaction_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FMapPackageNameIdToTransactionEventId, "INSERT INTO package_transactions(package_name_id, transaction_event_id) VALUES(?1, ?2);", SQLITE_PREPARED_STATEMENT_BINDINGS(int64, int64));
	FMapPackageNameIdToTransactionEventId Statement_MapPackageNameIdToTransactionEventId;
	bool MapPackageNameIdToTransactionEventId(const int64 InPackageNameId, const int64 InTransactionEventId)
	{
		return Statement_MapPackageNameIdToTransactionEventId.BindAndExecute(InPackageNameId, InTransactionEventId);
	}

	/** Unmap all package_name_id entries from package_transactions for the given transaction_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FUnmapPackageNameIdsForTransactionEventId, "DELETE FROM package_transactions WHERE transaction_event_id = ?1;", SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FUnmapPackageNameIdsForTransactionEventId Statement_UnmapPackageNameIdsForTransactionEventId;
	bool UnmapPackageNameIdsForTransactionEventId(const int64 InTransactionEventId)
	{
		return Statement_UnmapPackageNameIdsForTransactionEventId.BindAndExecute(InTransactionEventId);
	}

	/** Get the transaction_event_id entries from package_transactions for the given package_name_id */
	SQLITE_PREPARED_STATEMENT(FGetTransactionEventIdsForPackageNameId, "SELECT transaction_event_id FROM package_transactions WHERE package_name_id = ?1 ORDER BY transaction_event_id;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetTransactionEventIdsForPackageNameId Statement_GetTransactionEventIdsForPackageNameId;
	bool GetTransactionIdsForPackageNameId(const int64 InPackageNameId, TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64)> InCallback)
	{
		return Statement_GetTransactionEventIdsForPackageNameId.BindAndExecute(InPackageNameId, [&InCallback](const FGetTransactionEventIdsForPackageNameId& InStatement)
		{
			int64 TransactionEventId = 0;
			if (InStatement.GetColumnValues(TransactionEventId))
			{
				return InCallback(TransactionEventId);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the transaction_event_id entries from package_transactions for the given package_name_id and a transaction_event_id >= the given mininum transaction_event_id */
	SQLITE_PREPARED_STATEMENT(FGetTransactionEventIdsInRangeForPackageNameId, "SELECT transaction_event_id FROM package_transactions WHERE package_name_id = ?1 AND transaction_event_id >= ?2 ORDER BY transaction_event_id;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64, int64));
	FGetTransactionEventIdsInRangeForPackageNameId Statement_GetTransactionEventIdsInRangeForPackageNameId;
	bool GetTransactionEventIdsInRangeForPackageNameId(const int64 InPackageNameId, const int64 InMinTransactionEventId, TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64)> InCallback)
	{
		return Statement_GetTransactionEventIdsInRangeForPackageNameId.BindAndExecute(InPackageNameId, InMinTransactionEventId, [&InCallback](const FGetTransactionEventIdsInRangeForPackageNameId& InStatement)
		{
			int64 TransactionEventId = 0;
			if (InStatement.GetColumnValues(TransactionEventId))
			{
				return InCallback(TransactionEventId);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the max transaction_event_id for each package_name_ids from package_transactions */
	SQLITE_PREPARED_STATEMENT_COLUMNS_ONLY(FGetPackageNameIdsMaxTransactionId, "SELECT package_name_id, MAX(transaction_event_id) FROM package_transactions GROUP BY package_name_id;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64, int64));
	FGetPackageNameIdsMaxTransactionId Statement_GetPackageNameIdsMaxTransactionId;
	bool GetPackageNameIdsMaxTransactionId(TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64, int64)> InCallback)
	{
		return Statement_GetPackageNameIdsMaxTransactionId.Execute([&InCallback](const FGetPackageNameIdsMaxTransactionId& InStatement)
		{
			int64 PackageNameId = 0, MaxTransactionEventId = 0;
			if (InStatement.GetColumnValues(PackageNameId, MaxTransactionEventId))
			{
				return InCallback(PackageNameId, MaxTransactionEventId);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the unique package_name_ids from package_transactions */
	SQLITE_PREPARED_STATEMENT_COLUMNS_ONLY(FGetPackageNameIdsWithTransactions, "SELECT DISTINCT package_name_id FROM package_transactions;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64));
	FGetPackageNameIdsWithTransactions Statement_GetPackageNameIdsWithTransactions;
	bool GetPackageNameIdsWithTransactions(TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64)> InCallback)
	{
		return Statement_GetPackageNameIdsWithTransactions.Execute([&InCallback](const FGetPackageNameIdsWithTransactions& InStatement)
		{
			int64 PackageNameId = 0;
			if (InStatement.GetColumnValues(PackageNameId))
			{
				return InCallback(PackageNameId);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/** Get the package_name_ids from package_transactions for the given transaction_event_id */
	SQLITE_PREPARED_STATEMENT(FGetPackageNameIdsForTransactionEventId, "SELECT package_name_id FROM package_transactions WHERE transaction_event_id = ?1;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetPackageNameIdsForTransactionEventId Statement_GetPackageNameIdsForTransactionEventId;
	bool GetPackageNameIdsForTransactionEventId(const int64 InTransactionEventId, TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64)> InCallback)
	{
		return Statement_GetPackageNameIdsForTransactionEventId.BindAndExecute(InTransactionEventId, [&InCallback](const FGetPackageNameIdsForTransactionEventId& InStatement)
		{
			int64 PackageNameId = 0;
			if (InStatement.GetColumnValues(PackageNameId))
			{
				return InCallback(PackageNameId);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	/**
	 * Statements working on object_transactions
	 */

	/** Map the object_name_id in object_transactions to the the given transaction_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FMapObjectNameIdToTransactionEventId, "INSERT INTO object_transactions(object_name_id, transaction_event_id) VALUES(?1, ?2);", SQLITE_PREPARED_STATEMENT_BINDINGS(int64, int64));
	FMapObjectNameIdToTransactionEventId Statement_MapObjectNameIdToTransactionEventId;
	bool MapObjectNameIdToTransactionEventId(const int64 InPackageNameId, const int64 InTransactionEventId)
	{
		return Statement_MapObjectNameIdToTransactionEventId.BindAndExecute(InPackageNameId, InTransactionEventId);
	}

	/** Unmap all object_name_id entries from object_transactions for the given transaction_event_id */
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FUnmapObjectNameIdsForTransactionEventId, "DELETE FROM object_transactions WHERE transaction_event_id = ?1;", SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FUnmapObjectNameIdsForTransactionEventId Statement_UnmapObjectNameIdsForTransactionEventId;
	bool UnmapObjectNameIdsForTransactionEventId(const int64 InTransactionEventId)
	{
		return Statement_UnmapObjectNameIdsForTransactionEventId.BindAndExecute(InTransactionEventId);
	}

	/** Get the transaction_event_id entries from object_transactions for the given object_name_id */
	SQLITE_PREPARED_STATEMENT(FGetTransactionEventIdsForObjectNameId, "SELECT transaction_event_id FROM object_transactions WHERE object_name_id = ?1 ORDER BY transaction_event_id;", SQLITE_PREPARED_STATEMENT_COLUMNS(int64), SQLITE_PREPARED_STATEMENT_BINDINGS(int64));
	FGetTransactionEventIdsForObjectNameId Statement_GetTransactionEventIdsForObjectNameId;
	bool GetTransactionEventIdsForObjectNameId(const int64 InPackageNameId, TFunctionRef<ESQLitePreparedStatementExecuteRowResult(int64)> InCallback)
	{
		return Statement_GetTransactionEventIdsForObjectNameId.BindAndExecute(InPackageNameId, [&InCallback](const FGetTransactionEventIdsForObjectNameId& InStatement)
		{
			int64 TransactionEventId = 0;
			if (InStatement.GetColumnValues(TransactionEventId))
			{
				return InCallback(TransactionEventId);
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

private:
	FSQLiteDatabase& Database;
};

class FConcertSyncSessionDatabaseScopedTransaction
{
public:
	explicit FConcertSyncSessionDatabaseScopedTransaction(FConcertSyncSessionDatabaseStatements& InStatements)
		: Statements(InStatements)
		, bHasTransaction(Statements.BeginTransaction()) // This will fail if a transaction is already open
	{
	}

	~FConcertSyncSessionDatabaseScopedTransaction()
	{
		Commit();
	}

	bool CommitOrRollback(const bool bShouldCommit)
	{
		if (bShouldCommit)
		{
			Commit();
			return true;
		}
		
		Rollback();
		return false;
	}

	void Commit()
	{
		if (bHasTransaction)
		{
			verify(Statements.CommitTransaction());
			bHasTransaction = false;
		}
	}

	void Rollback()
	{
		if (bHasTransaction)
		{
			verify(Statements.RollbackTransaction());
			bHasTransaction = false;
		}
	}

private:
	FConcertSyncSessionDatabaseStatements& Statements;
	bool bHasTransaction;
};

/** Defined here where TUniquePtr can see the definition of FConcertFileCache and FConcertSyncSessionDatabaseStatements as the TUniquePtr constructor/destructor cannot work with a forward declared type */
FConcertSyncSessionDatabase::FConcertSyncSessionDatabase()
	: Database(MakeUnique<FSQLiteDatabase>())
{
}
FConcertSyncSessionDatabase::~FConcertSyncSessionDatabase() = default;

bool FConcertSyncSessionDatabase::IsValid() const
{
	return Database->IsValid();
}

bool FConcertSyncSessionDatabase::Open(const FString& InSessionPath)
{
	return Open(InSessionPath, ESQLiteDatabaseOpenMode::ReadWriteCreate);
}

bool FConcertSyncSessionDatabase::Open(const FString& InSessionPath, const ESQLiteDatabaseOpenMode InOpenMode)
{
	if (Database->IsValid())
	{
		return false;
	}

	if (!Database->Open(*(InSessionPath / TEXT("Session.db")), InOpenMode))
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to open session database for '%s': %s"), *InSessionPath, *GetLastError());
		return false;
	}

	SessionPath = InSessionPath;
	TransactionFileCache = MakeUnique<FConcertFileCache>(TransactionDataUtil::MinFilesToCache, TransactionDataUtil::MaxFileSizeBytesToCache);
	PackageFileCache = MakeUnique<FConcertFileCache>(PackageDataUtil::MinFilesToCache, PackageDataUtil::MaxFileSizeBytesToCache);

	// Set the database to use exclusive WAL mode for performance (exclusive works even on platforms without a mmap implementation)
	// Set the database "NORMAL" fsync mode to only perform a fsync when checkpointing the WAL to the main database file (fewer fsync calls are better for performance, with a very slight loss of WAL durability if the power fails)
	Database->Execute(TEXT("PRAGMA locking_mode=EXCLUSIVE;"));
	Database->Execute(TEXT("PRAGMA journal_mode=WAL;"));
	Database->Execute(TEXT("PRAGMA synchronous=NORMAL;"));

	int32 LoadedDatabaseVersion = 0;
	Database->GetUserVersion(LoadedDatabaseVersion);
	if (LoadedDatabaseVersion > (int32)FConcertSyncSessionDatabaseVersion::Current)
	{
		Close();
		UE_LOG(LogConcert, Error, TEXT("Failed to open session database for '%s': Database is too new (version %d, expected <= %d)"), *InSessionPath, LoadedDatabaseVersion, (int32)FConcertSyncSessionDatabaseVersion::Current);
		return false;
	}

	// Create our required tables
#define CREATE_TABLE(NAME, STATEMENT)																										\
	if (!Database->Execute(TEXT("CREATE TABLE IF NOT EXISTS ") TEXT(NAME) TEXT("(") TEXT(STATEMENT) TEXT(");")))							\
	{																																		\
		Close();																															\
		return false;																														\
	}
	CREATE_TABLE("object_names", "object_name_id INTEGER PRIMARY KEY, object_path_name TEXT UNIQUE NOT NULL");
	CREATE_TABLE("package_names", "package_name_id INTEGER PRIMARY KEY, package_name TEXT UNIQUE NOT NULL");
	CREATE_TABLE("endpoints", "endpoint_id BLOB PRIMARY KEY, user_id TEXT NOT NULL, client_info_size_bytes INTEGER NOT NULL, client_info_data BLOB");
	CREATE_TABLE("connection_events", "connection_event_id INTEGER PRIMARY KEY, connection_event_type INTEGER NOT NULL");
	CREATE_TABLE("lock_events", "lock_event_id INTEGER PRIMARY KEY, lock_event_type INTEGER NOT NULL");
	CREATE_TABLE("transaction_events", "transaction_event_id INTEGER PRIMARY KEY, data_filename TEXT NOT NULL");
	CREATE_TABLE("package_events", "package_event_id INTEGER PRIMARY KEY, package_name_id INTEGER NOT NULL, package_revision INTEGER NOT NULL, package_info_size_bytes INTEGER NOT NULL, package_info_data BLOB, transaction_event_id_at_save INTEGER NOT NULL, data_filename TEXT NOT NULL, FOREIGN KEY(package_name_id) REFERENCES package_names(package_name_id)");
	CREATE_TABLE("persist_events", "persist_event_id INTEGER PRIMARY KEY, package_event_id INTEGER NOT NULL, transaction_event_id_at_persist INTEGER NOT NULL, FOREIGN KEY(package_event_id) REFERENCES package_events(package_event_id)");
	CREATE_TABLE("activities", "activity_id INTEGER PRIMARY KEY, endpoint_id BLOB NOT NULL, event_time INTEGER NOT NULL, event_type INTEGER NOT NULL, event_id INTEGER NOT NULL, event_summary_type TEXT NOT NULL, event_summary_size_bytes INTEGER NOT NULL, event_summary_data BLOB, FOREIGN KEY(endpoint_id) REFERENCES endpoints(endpoint_id)");
	CREATE_TABLE("ignored_activities", "activity_id INTEGER NOT NULL, FOREIGN KEY(activity_id) REFERENCES activities(activity_id)");
	CREATE_TABLE("resource_locks", "object_name_id INTEGER NOT NULL, lock_event_id INTEGER NOT NULL, FOREIGN KEY(object_name_id) REFERENCES object_names(object_name_id), FOREIGN KEY(lock_event_id) REFERENCES lock_events(lock_event_id)");
	CREATE_TABLE("package_transactions", "package_name_id INTEGER NOT NULL, transaction_event_id INTEGER NOT NULL, FOREIGN KEY(package_name_id) REFERENCES package_names(package_name_id), FOREIGN KEY(transaction_event_id) REFERENCES transaction_events(transaction_event_id)");
	CREATE_TABLE("object_transactions", "object_name_id INTEGER NOT NULL, transaction_event_id INTEGER NOT NULL, FOREIGN KEY(object_name_id) REFERENCES object_names(object_name_id), FOREIGN KEY(transaction_event_id) REFERENCES transaction_events(transaction_event_id)");
#undef CREATE_TABLE

	// Create our required indexes
#define CREATE_INDEX(NAME, TABLE, COLS)																										\
	if (!Database->Execute(TEXT("CREATE INDEX IF NOT EXISTS ") TEXT(NAME) TEXT(" ON ") TEXT(TABLE) TEXT("(") TEXT(COLS) TEXT(");")))		\
	{																																		\
		Close();																															\
		return false;																														\
	}
#define CREATE_UNIQUE_INDEX(NAME, TABLE, COLS)																								\
	if (!Database->Execute(TEXT("CREATE UNIQUE INDEX IF NOT EXISTS ") TEXT(NAME) TEXT(" ON ") TEXT(TABLE) TEXT("(") TEXT(COLS) TEXT(");")))	\
	{																																		\
		Close();																															\
		return false;																														\
	}
	CREATE_UNIQUE_INDEX("idx_object_path_names_in_object_names", "object_names", "object_path_name");
	CREATE_UNIQUE_INDEX("idx_package_names_in_package_names", "package_names", "package_name");
	CREATE_INDEX("idx_package_name_ids_in_package_events", "package_events", "package_name_id");
	CREATE_INDEX("idx_package_event_ids_in_persist_events", "persist_events", "package_event_id");
	CREATE_INDEX("idx_event_ids_in_activities", "activities", "event_id");
	CREATE_UNIQUE_INDEX("idx_activity_ids_in_ignored_activities", "ignored_activities", "activity_id");
	CREATE_INDEX("idx_object_name_ids_in_resource_locks", "resource_locks", "object_name_id");
	CREATE_INDEX("idx_lock_event_ids_in_resource_locks", "resource_locks", "lock_event_id");
	CREATE_INDEX("idx_package_name_ids_in_package_transactions", "package_transactions", "package_name_id");
	CREATE_INDEX("idx_transaction_event_ids_in_package_transactions", "package_transactions", "transaction_event_id");
	CREATE_INDEX("idx_object_name_ids_in_object_transactions", "object_transactions", "object_name_id");
	CREATE_INDEX("idx_transaction_event_ids_in_object_transactions", "object_transactions", "transaction_event_id");
#undef CREATE_INDEX
#undef CREATE_UNIQUE_INDEX

	// The database will have the latest schema at this point, so update the user-version
	if (!Database->SetUserVersion((int32)FConcertSyncSessionDatabaseVersion::Current))
	{
		Close();
		return false;
	}

	// Create our required prepared statements
	Statements = MakeUnique<FConcertSyncSessionDatabaseStatements>(*Database);
	if (!Statements->CreatePreparedStatements())
	{
		Close();
		return false;
	}

	return true;
}

bool FConcertSyncSessionDatabase::Close(const bool InDeleteDatabase)
{
	if (!Database->IsValid())
	{
		return false;
	}

	// Need to destroy prepared statements before the database can be closed
	Statements.Reset();

	if (!Database->Close())
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to close session database for '%s': %s"), *SessionPath, *GetLastError());
		return false;
	}
	
	TransactionFileCache.Reset();
	PackageFileCache.Reset();

	if (InDeleteDatabase)
	{
		ConcertUtil::DeleteDirectoryTree(*TransactionDataUtil::GetDataPath(SessionPath), *SessionPath);
		ConcertUtil::DeleteDirectoryTree(*PackageDataUtil::GetDataPath(SessionPath), *SessionPath);
		IFileManager::Get().Delete(*(SessionPath / TEXT("Session.db")), false);
	}

	SessionPath.Reset();

	return true;
}

FString FConcertSyncSessionDatabase::GetFilename() const
{
	return Database->GetFilename();
}

FString FConcertSyncSessionDatabase::GetLastError() const
{
	return Database->GetLastError();
}

bool FConcertSyncSessionDatabase::AddConnectionActivity(const FConcertSyncConnectionActivity& InConnectionActivity, int64& OutActivityId, int64& OutConnectionEventId)
{
	FConcertSyncSessionDatabaseScopedTransaction ScopedTransaction(*Statements);
	return ScopedTransaction.CommitOrRollback(
		AddConnectionEvent(InConnectionActivity.EventData, OutConnectionEventId) &&
		Statements->AddActivityData(InConnectionActivity.EndpointId, EConcertSyncActivityEventType::Connection, OutConnectionEventId, InConnectionActivity.EventSummary, OutActivityId) &&
		SetActivityIgnoredState(OutActivityId, InConnectionActivity.bIgnored)
		);
}

bool FConcertSyncSessionDatabase::AddLockActivity(const FConcertSyncLockActivity& InLockActivity, int64& OutActivityId, int64& OutLockEventId)
{
	FConcertSyncSessionDatabaseScopedTransaction ScopedTransaction(*Statements);
	return ScopedTransaction.CommitOrRollback(
		AddLockEvent(InLockActivity.EventData, OutLockEventId) &&
		Statements->AddActivityData(InLockActivity.EndpointId, EConcertSyncActivityEventType::Lock, OutLockEventId, InLockActivity.EventSummary, OutActivityId) &&
		SetActivityIgnoredState(OutActivityId, InLockActivity.bIgnored)
		);
}

bool FConcertSyncSessionDatabase::AddTransactionActivity(const FConcertSyncTransactionActivity& InTransactionActivity, int64& OutActivityId, int64& OutTransactionEventId)
{
	FConcertSyncSessionDatabaseScopedTransaction ScopedTransaction(*Statements);
	return ScopedTransaction.CommitOrRollback(
		AddTransactionEvent(InTransactionActivity.EventData, OutTransactionEventId) &&
		Statements->AddActivityData(InTransactionActivity.EndpointId, EConcertSyncActivityEventType::Transaction, OutTransactionEventId, InTransactionActivity.EventSummary, OutActivityId) &&
		SetActivityIgnoredState(OutActivityId, InTransactionActivity.bIgnored)
		);
}

bool FConcertSyncSessionDatabase::AddPackageActivity(const FConcertSyncPackageActivity& InPackageActivity, int64& OutActivityId, int64& OutPackageEventId)
{
	FConcertSyncSessionDatabaseScopedTransaction ScopedTransaction(*Statements);
	return ScopedTransaction.CommitOrRollback(
		AddPackageEvent(InPackageActivity.EventData, OutPackageEventId) &&
		Statements->AddActivityData(InPackageActivity.EndpointId, EConcertSyncActivityEventType::Package, OutPackageEventId, InPackageActivity.EventSummary, OutActivityId) &&
		SetActivityIgnoredState(OutActivityId, InPackageActivity.bIgnored)
		);
}

bool FConcertSyncSessionDatabase::SetConnectionActivity(const FConcertSyncConnectionActivity& InConnectionActivity)
{
	FConcertSyncSessionDatabaseScopedTransaction ScopedTransaction(*Statements);
	return ScopedTransaction.CommitOrRollback(
		SetConnectionEvent(InConnectionActivity.EventId, InConnectionActivity.EventData) &&
		Statements->SetActivityData(InConnectionActivity.ActivityId, InConnectionActivity.EndpointId, InConnectionActivity.EventTime, InConnectionActivity.EventType, InConnectionActivity.EventId, InConnectionActivity.EventSummary) &&
		SetActivityIgnoredState(InConnectionActivity.ActivityId, InConnectionActivity.bIgnored)
		);
}

bool FConcertSyncSessionDatabase::SetLockActivity(const FConcertSyncLockActivity& InLockActivity)
{
	FConcertSyncSessionDatabaseScopedTransaction ScopedTransaction(*Statements);
	return ScopedTransaction.CommitOrRollback(
		SetLockEvent(InLockActivity.EventId, InLockActivity.EventData) &&
		Statements->SetActivityData(InLockActivity.ActivityId, InLockActivity.EndpointId, InLockActivity.EventTime, InLockActivity.EventType, InLockActivity.EventId, InLockActivity.EventSummary) &&
		SetActivityIgnoredState(InLockActivity.ActivityId, InLockActivity.bIgnored)
		);
}

bool FConcertSyncSessionDatabase::SetTransactionActivity(const FConcertSyncTransactionActivity& InTransactionActivity, const bool bMetaDataOnly)
{
	FConcertSyncSessionDatabaseScopedTransaction ScopedTransaction(*Statements);
	return ScopedTransaction.CommitOrRollback(
		SetTransactionEvent(InTransactionActivity.EventId, InTransactionActivity.EventData, bMetaDataOnly) &&
		Statements->SetActivityData(InTransactionActivity.ActivityId, InTransactionActivity.EndpointId, InTransactionActivity.EventTime, InTransactionActivity.EventType, InTransactionActivity.EventId, InTransactionActivity.EventSummary) &&
		SetActivityIgnoredState(InTransactionActivity.ActivityId, InTransactionActivity.bIgnored)
		);
}

bool FConcertSyncSessionDatabase::SetPackageActivity(const FConcertSyncPackageActivity& InPackageActivity, const bool bMetaDataOnly)
{
	FConcertSyncSessionDatabaseScopedTransaction ScopedTransaction(*Statements);
	return ScopedTransaction.CommitOrRollback(
		SetPackageEvent(InPackageActivity.EventId, InPackageActivity.EventData, bMetaDataOnly) &&
		Statements->SetActivityData(InPackageActivity.ActivityId, InPackageActivity.EndpointId, InPackageActivity.EventTime, InPackageActivity.EventType, InPackageActivity.EventId, InPackageActivity.EventSummary) &&
		SetActivityIgnoredState(InPackageActivity.ActivityId, InPackageActivity.bIgnored)
		);
}

bool FConcertSyncSessionDatabase::GetActivity(const int64 InActivityId, FConcertSyncActivity& OutActivity) const
{
	OutActivity.ActivityId = InActivityId;
	OutActivity.bIgnored = Statements->IsActivityIgnored(InActivityId);
	return Statements->GetActivityDataForId(InActivityId, OutActivity.EndpointId, OutActivity.EventTime, OutActivity.EventType, OutActivity.EventId, OutActivity.EventSummary);
}

bool FConcertSyncSessionDatabase::GetConnectionActivity(const int64 InActivityId, FConcertSyncConnectionActivity& OutConnectionActivity) const
{
	return GetActivity(InActivityId, OutConnectionActivity)
		&& GetConnectionEvent(OutConnectionActivity.EventId, OutConnectionActivity.EventData);
}

bool FConcertSyncSessionDatabase::GetLockActivity(const int64 InActivityId, FConcertSyncLockActivity& OutLockActivity) const
{
	return GetActivity(InActivityId, OutLockActivity)
		&& GetLockEvent(OutLockActivity.EventId, OutLockActivity.EventData);
}

bool FConcertSyncSessionDatabase::GetTransactionActivity(const int64 InActivityId, FConcertSyncTransactionActivity& OutTransactionActivity) const
{
	return GetActivity(InActivityId, OutTransactionActivity)
		&& GetTransactionEvent(OutTransactionActivity.EventId, OutTransactionActivity.EventData);
}

bool FConcertSyncSessionDatabase::GetPackageActivity(const int64 InActivityId, FConcertSyncPackageActivity& OutPackageActivity) const
{
	return GetActivity(InActivityId, OutPackageActivity)
		&& GetPackageEvent(OutPackageActivity.EventId, OutPackageActivity.EventData);
}

bool FConcertSyncSessionDatabase::GetActivityEventType(const int64 InActivityId, EConcertSyncActivityEventType& OutEventType) const
{
	return Statements->GetActivityEventTypeForId(InActivityId, OutEventType);
}

bool FConcertSyncSessionDatabase::GetActivityForEvent(const int64 InEventId, const EConcertSyncActivityEventType InEventType, FConcertSyncActivity& OutActivity) const
{
	OutActivity.EventId = InEventId;
	OutActivity.EventType = InEventType;
	if (Statements->GetActivityDataForEvent(InEventId, InEventType, OutActivity.ActivityId, OutActivity.EndpointId, OutActivity.EventTime, OutActivity.EventSummary))
	{
		OutActivity.bIgnored = Statements->IsActivityIgnored(OutActivity.ActivityId);
		return true;
	}
	return false;
}

bool FConcertSyncSessionDatabase::GetConnectionActivityForEvent(const int64 InConnectionEventId, FConcertSyncConnectionActivity& OutConnectionActivity) const
{
	return GetActivityForEvent(InConnectionEventId, EConcertSyncActivityEventType::Connection, OutConnectionActivity)
		&& GetConnectionEvent(InConnectionEventId, OutConnectionActivity.EventData);
}

bool FConcertSyncSessionDatabase::GetLockActivityForEvent(const int64 InLockEventId, FConcertSyncLockActivity& OutLockActivity) const
{
	return GetActivityForEvent(InLockEventId, EConcertSyncActivityEventType::Lock, OutLockActivity)
		&& GetLockEvent(InLockEventId, OutLockActivity.EventData);
}

bool FConcertSyncSessionDatabase::GetTransactionActivityForEvent(const int64 InTransactionEventId, FConcertSyncTransactionActivity& OutTransactionActivity) const
{
	return GetActivityForEvent(InTransactionEventId, EConcertSyncActivityEventType::Transaction, OutTransactionActivity)
		&& GetTransactionEvent(InTransactionEventId, OutTransactionActivity.EventData);
}

bool FConcertSyncSessionDatabase::GetPackageActivityForEvent(const int64 InPackageEventId, FConcertSyncPackageActivity& OutPackageActivity) const
{
	return GetActivityForEvent(InPackageEventId, EConcertSyncActivityEventType::Package, OutPackageActivity)
		&& GetPackageEvent(InPackageEventId, OutPackageActivity.EventData);
}

bool FConcertSyncSessionDatabase::EnumerateActivities(TFunctionRef<bool(FConcertSyncActivity&&)> InCallback) const
{
	return Statements->GetAllActivityData([this, &InCallback](const int64 InActivityId, const FGuid& InEndpointId, const FDateTime InEventTime, const EConcertSyncActivityEventType InEventType, const int64 InEventId, FConcertSessionSerializedCborPayload&& InEventSummary)
	{
		FConcertSyncActivity Activity;
		Activity.ActivityId = InActivityId;
		Activity.bIgnored = Statements->IsActivityIgnored(InActivityId);
		Activity.EndpointId = InEndpointId;
		Activity.EventTime = InEventTime;
		Activity.EventType = InEventType;
		Activity.EventId = InEventId;
		Activity.EventSummary = MoveTemp(InEventSummary);
		return InCallback(MoveTemp(Activity))
			? ESQLitePreparedStatementExecuteRowResult::Continue
			: ESQLitePreparedStatementExecuteRowResult::Stop;
	});
}

bool FConcertSyncSessionDatabase::EnumerateConnectionActivities(TFunctionRef<bool(FConcertSyncConnectionActivity&&)> InCallback) const
{
	return Statements->GetAllActivityDataForEventType(EConcertSyncActivityEventType::Connection, [this, &InCallback](const int64 InActivityId, const FGuid& InEndpointId, const FDateTime InEventTime, const int64 InEventId, FConcertSessionSerializedCborPayload&& InEventSummary)
	{
		FConcertSyncConnectionActivity ConnectionActivity;
		ConnectionActivity.ActivityId = InActivityId;
		ConnectionActivity.bIgnored = Statements->IsActivityIgnored(InActivityId);
		ConnectionActivity.EndpointId = InEndpointId;
		ConnectionActivity.EventTime = InEventTime;
		ConnectionActivity.EventType = EConcertSyncActivityEventType::Connection;
		ConnectionActivity.EventId = InEventId;
		ConnectionActivity.EventSummary = MoveTemp(InEventSummary);
		if (GetConnectionEvent(ConnectionActivity.EventId, ConnectionActivity.EventData))
		{
			return InCallback(MoveTemp(ConnectionActivity))
				? ESQLitePreparedStatementExecuteRowResult::Continue
				: ESQLitePreparedStatementExecuteRowResult::Stop;
		}
		return ESQLitePreparedStatementExecuteRowResult::Error;
	});
}

bool FConcertSyncSessionDatabase::EnumerateLockActivities(TFunctionRef<bool(FConcertSyncLockActivity&&)> InCallback) const
{
	return Statements->GetAllActivityDataForEventType(EConcertSyncActivityEventType::Lock, [this, &InCallback](const int64 InActivityId, const FGuid& InEndpointId, const FDateTime InEventTime, const int64 InEventId, FConcertSessionSerializedCborPayload&& InEventSummary)
	{
		FConcertSyncLockActivity LockActivity;
		LockActivity.ActivityId = InActivityId;
		LockActivity.bIgnored = Statements->IsActivityIgnored(InActivityId);
		LockActivity.EndpointId = InEndpointId;
		LockActivity.EventTime = InEventTime;
		LockActivity.EventType = EConcertSyncActivityEventType::Lock;
		LockActivity.EventId = InEventId;
		LockActivity.EventSummary = MoveTemp(InEventSummary);
		if (GetLockEvent(LockActivity.EventId, LockActivity.EventData))
		{
			return InCallback(MoveTemp(LockActivity))
				? ESQLitePreparedStatementExecuteRowResult::Continue
				: ESQLitePreparedStatementExecuteRowResult::Stop;
		}
		return ESQLitePreparedStatementExecuteRowResult::Error;
	});
}

bool FConcertSyncSessionDatabase::EnumerateTransactionActivities(TFunctionRef<bool(FConcertSyncTransactionActivity&&)> InCallback) const
{
	return Statements->GetAllActivityDataForEventType(EConcertSyncActivityEventType::Transaction, [this, &InCallback](const int64 InActivityId, const FGuid& InEndpointId, const FDateTime InEventTime, const int64 InEventId, FConcertSessionSerializedCborPayload&& InEventSummary)
	{
		FConcertSyncTransactionActivity TransactionActivity;
		TransactionActivity.ActivityId = InActivityId;
		TransactionActivity.bIgnored = Statements->IsActivityIgnored(InActivityId);
		TransactionActivity.EndpointId = InEndpointId;
		TransactionActivity.EventTime = InEventTime;
		TransactionActivity.EventType = EConcertSyncActivityEventType::Transaction;
		TransactionActivity.EventId = InEventId;
		TransactionActivity.EventSummary = MoveTemp(InEventSummary);
		if (GetTransactionEvent(TransactionActivity.EventId, TransactionActivity.EventData))
		{
			return InCallback(MoveTemp(TransactionActivity))
				? ESQLitePreparedStatementExecuteRowResult::Continue
				: ESQLitePreparedStatementExecuteRowResult::Stop;
		}
		return ESQLitePreparedStatementExecuteRowResult::Error;
	});
}

bool FConcertSyncSessionDatabase::EnumeratePackageActivities(TFunctionRef<bool(FConcertSyncPackageActivity&&)> InCallback) const
{
	return Statements->GetAllActivityDataForEventType(EConcertSyncActivityEventType::Package, [this, &InCallback](const int64 InActivityId, const FGuid& InEndpointId, const FDateTime InEventTime, const int64 InEventId, FConcertSessionSerializedCborPayload&& InEventSummary)
	{
		FConcertSyncPackageActivity PackageActivity;
		PackageActivity.ActivityId = InActivityId;
		PackageActivity.bIgnored = Statements->IsActivityIgnored(InActivityId);
		PackageActivity.EndpointId = InEndpointId;
		PackageActivity.EventTime = InEventTime;
		PackageActivity.EventType = EConcertSyncActivityEventType::Package;
		PackageActivity.EventId = InEventId;
		PackageActivity.EventSummary = MoveTemp(InEventSummary);
		if (GetPackageEvent(PackageActivity.EventId, PackageActivity.EventData))
		{
			return InCallback(MoveTemp(PackageActivity))
				? ESQLitePreparedStatementExecuteRowResult::Continue
				: ESQLitePreparedStatementExecuteRowResult::Stop;
		}
		return ESQLitePreparedStatementExecuteRowResult::Error;
	});
}

bool FConcertSyncSessionDatabase::EnumerateActivitiesForEventType(const EConcertSyncActivityEventType InEventType, TFunctionRef<bool(FConcertSyncActivity&&)> InCallback) const
{
	return Statements->GetAllActivityDataForEventType(InEventType, [this, InEventType, &InCallback](const int64 InActivityId, const FGuid& InEndpointId, const FDateTime InEventTime, const int64 InEventId, FConcertSessionSerializedCborPayload&& InEventSummary)
	{
		FConcertSyncActivity Activity;
		Activity.ActivityId = InActivityId;
		Activity.bIgnored = Statements->IsActivityIgnored(InActivityId);
		Activity.EndpointId = InEndpointId;
		Activity.EventTime = InEventTime;
		Activity.EventType = InEventType;
		Activity.EventId = InEventId;
		Activity.EventSummary = MoveTemp(InEventSummary);
		return InCallback(MoveTemp(Activity))
			? ESQLitePreparedStatementExecuteRowResult::Continue
			: ESQLitePreparedStatementExecuteRowResult::Stop;
	});
}

bool FConcertSyncSessionDatabase::EnumerateActivitiesInRange(const int64 InFirstActivityId, const int64 InMaxNumActivities, TFunctionRef<bool(FConcertSyncActivity&&)> InCallback) const
{
	return Statements->GetActivityDataInRange(InFirstActivityId, InMaxNumActivities, [this, &InCallback](const int64 InActivityId, const FGuid& InEndpointId, const FDateTime InEventTime, const EConcertSyncActivityEventType InEventType, const int64 InEventId, FConcertSessionSerializedCborPayload&& InEventSummary)
	{
		FConcertSyncActivity Activity;
		Activity.ActivityId = InActivityId;
		Activity.bIgnored = Statements->IsActivityIgnored(InActivityId);
		Activity.EndpointId = InEndpointId;
		Activity.EventTime = InEventTime;
		Activity.EventType = InEventType;
		Activity.EventId = InEventId;
		Activity.EventSummary = MoveTemp(InEventSummary);
		return InCallback(MoveTemp(Activity))
			? ESQLitePreparedStatementExecuteRowResult::Continue
			: ESQLitePreparedStatementExecuteRowResult::Stop;
	});
}

bool FConcertSyncSessionDatabase::EnumerateActivityIdsAndEventTypes(TFunctionRef<bool(int64, EConcertSyncActivityEventType)> InCallback) const
{
	return Statements->GetAllActivityIdAndEventTypes([&InCallback](const int64 InActivityId, const EConcertSyncActivityEventType InEventType)
	{
		return InCallback(InActivityId, InEventType)
			? ESQLitePreparedStatementExecuteRowResult::Continue
			: ESQLitePreparedStatementExecuteRowResult::Stop;
	});
}

bool FConcertSyncSessionDatabase::EnumerateActivityIdsAndEventTypesInRange(const int64 InFirstActivityId, const int64 InMaxNumActivities, TFunctionRef<bool(int64, EConcertSyncActivityEventType)> InCallback) const
{
	return Statements->GetActivityIdAndEventTypesInRange(InFirstActivityId, InMaxNumActivities, [&InCallback](const int64 InActivityId, const EConcertSyncActivityEventType InEventType)
	{
		return InCallback(InActivityId, InEventType)
			? ESQLitePreparedStatementExecuteRowResult::Continue
			: ESQLitePreparedStatementExecuteRowResult::Stop;
	});
}

bool FConcertSyncSessionDatabase::GetActivityMaxId(int64& OutActivityId) const
{
	return Statements->GetActivityMaxId(OutActivityId);
}

bool FConcertSyncSessionDatabase::SetEndpoint(const FGuid& InEndpointId, const FConcertSyncEndpointData& InEndpointData)
{
	return Statements->SetEndpointData(InEndpointId, InEndpointData.ClientInfo);
}

bool FConcertSyncSessionDatabase::GetEndpoint(const FGuid& InEndpointId, FConcertSyncEndpointData& OutEndpointData) const
{
	return Statements->GetEndpointDataForId(InEndpointId, OutEndpointData.ClientInfo);
}

bool FConcertSyncSessionDatabase::EnumerateEndpoints(TFunctionRef<bool(FConcertSyncEndpointIdAndData&&)> InCallback) const
{
	return Statements->GetAllEndpointData([&InCallback](const FGuid& InEndpointId, FConcertClientInfo&& InClientInfo)
	{
		FConcertSyncEndpointIdAndData EndpointData;
		EndpointData.EndpointId = InEndpointId;
		EndpointData.EndpointData.ClientInfo = MoveTemp(InClientInfo);
		return InCallback(MoveTemp(EndpointData))
			? ESQLitePreparedStatementExecuteRowResult::Continue
			: ESQLitePreparedStatementExecuteRowResult::Stop;
	});
}

bool FConcertSyncSessionDatabase::EnumerateEndpointIds(TFunctionRef<bool(FGuid)> InCallback) const
{
	return Statements->GetAllEndpointIds([&InCallback](const FGuid& InEndpointId)
	{
		return InCallback(InEndpointId)
			? ESQLitePreparedStatementExecuteRowResult::Continue
			: ESQLitePreparedStatementExecuteRowResult::Stop;
	});
}

bool FConcertSyncSessionDatabase::SetActivityIgnoredState(const int64 InActivityId, const bool InIsIgnored)
{
	return InIsIgnored
		? Statements->IgnoreActivity(InActivityId)
		: Statements->PerceiveActivity(InActivityId);
}

bool FConcertSyncSessionDatabase::AddConnectionEvent(const FConcertSyncConnectionEvent& InConnectionEvent, int64& OutConnectionEventId)
{
	return Statements->AddConnectionEvent(InConnectionEvent.ConnectionEventType, OutConnectionEventId);
}

bool FConcertSyncSessionDatabase::SetConnectionEvent(const int64 InConnectionEventId, const FConcertSyncConnectionEvent& InConnectionEvent)
{
	return Statements->SetConnectionEvent(InConnectionEventId, InConnectionEvent.ConnectionEventType);
}

bool FConcertSyncSessionDatabase::GetConnectionEvent(const int64 InConnectionEventId, FConcertSyncConnectionEvent& OutConnectionEvent) const
{
	return Statements->GetConnectionEventForId(InConnectionEventId, OutConnectionEvent.ConnectionEventType);
}

bool FConcertSyncSessionDatabase::AddLockEvent(const FConcertSyncLockEvent& InLockEvent, int64& OutLockEventId)
{
	if (Statements->AddLockEvent(InLockEvent.LockEventType, OutLockEventId))
	{
		return MapResourceNamesForLock(OutLockEventId, InLockEvent.ResourceNames);
	}
	return false;
}

bool FConcertSyncSessionDatabase::SetLockEvent(const int64 InLockEventId, const FConcertSyncLockEvent& InLockEvent)
{
	if (Statements->SetLockEvent(InLockEventId, InLockEvent.LockEventType))
	{
		return MapResourceNamesForLock(InLockEventId, InLockEvent.ResourceNames);
	}
	return false;
}

bool FConcertSyncSessionDatabase::GetLockEvent(const int64 InLockEventId, FConcertSyncLockEvent& OutLockEvent) const
{
	if (Statements->GetLockEventForId(InLockEventId, OutLockEvent.LockEventType))
	{
		OutLockEvent.ResourceNames.Reset();
		return Statements->GetObjectNameIdsForLockEventId(InLockEventId, [this, &OutLockEvent](const int64 InObjectNameId)
		{
			return GetObjectPathName(InObjectNameId, OutLockEvent.ResourceNames.AddDefaulted_GetRef())
				? ESQLitePreparedStatementExecuteRowResult::Continue
				: ESQLitePreparedStatementExecuteRowResult::Error;
		});
	}
	return false;
}

bool FConcertSyncSessionDatabase::AddTransactionEvent(const FConcertSyncTransactionEvent& InTransactionEvent, int64& OutTransactionEventId)
{
	// Get the next transaction ID
	if (!GetTransactionMaxEventId(OutTransactionEventId) || OutTransactionEventId == MAX_int64)
	{
		return false;
	}
	++OutTransactionEventId;

	return SetTransactionEvent(OutTransactionEventId, InTransactionEvent);
}

bool FConcertSyncSessionDatabase::UpdateTransactionEvent(const int64 InTransactionEventId, const FConcertSyncTransactionEvent& InTransactionEvent)
{
	int64 MaxTransactionEventId;
	if (GetTransactionMaxEventId(MaxTransactionEventId) && InTransactionEventId <= MaxTransactionEventId) // Ensure the transaction ID is in bound.
	{
		FConcertSyncSessionDatabaseScopedTransaction ScopedTransaction(*Statements);
		return ScopedTransaction.CommitOrRollback(
			SetTransactionEvent(InTransactionEventId, InTransactionEvent)
		);
	}

	return false;
}

bool FConcertSyncSessionDatabase::SetTransactionEvent(const int64 InTransactionEventId, const FConcertSyncTransactionEvent& InTransactionEvent, const bool bMetaDataOnly)
{
	// Write the data blob file
	const FString TransactionDataFilename = TransactionDataUtil::GetDataFilename(InTransactionEventId);
	const FString TransactionDataPathname = TransactionDataUtil::GetDataPath(SessionPath) / TransactionDataFilename;

	FStructOnScope Transaction(FConcertTransactionFinalizedEvent::StaticStruct(), (uint8*)&InTransactionEvent.Transaction);
	if (!bMetaDataOnly)
	{
		if (!SaveTransaction(TransactionDataPathname, Transaction))
		{
			return false;
		}
	}

	// Add the database entry
	if (Statements->SetTransactionEvent(InTransactionEventId, TransactionDataFilename))
	{
		return MapPackageNamesForTransaction(InTransactionEventId, InTransactionEvent.Transaction)
			&& MapObjectNamesForTransaction(InTransactionEventId, InTransactionEvent.Transaction);
	}

	return false;
}

bool FConcertSyncSessionDatabase::GetTransactionEvent(const int64 InTransactionEventId, FConcertSyncTransactionEvent& OutTransactionEvent, const bool InMetaDataOnly) const
{
	FString DataFilename;
	if (Statements->GetTransactionEventForId(InTransactionEventId, DataFilename))
	{
		if (InMetaDataOnly)
		{
			OutTransactionEvent.Transaction = FConcertTransactionFinalizedEvent();
			return true;
		}

		const FString TransactionDataPathname = TransactionDataUtil::GetDataPath(SessionPath) / DataFilename;

		FStructOnScope Transaction(FConcertTransactionFinalizedEvent::StaticStruct(), (uint8*)&OutTransactionEvent.Transaction);
		if (LoadTransaction(TransactionDataPathname, Transaction))
		{
			return true;
		}
	}

	return false;
}

bool FConcertSyncSessionDatabase::GetTransactionMaxEventId(int64& OutTransactionEventId) const
{
	return Statements->GetTransactionMaxEventId(OutTransactionEventId);
}

bool FConcertSyncSessionDatabase::IsLiveTransactionEvent(const int64 InTransactionEventId, bool& OutIsLive) const
{
	OutIsLive = false;
	return Statements->GetPackageNameIdsForTransactionEventId(InTransactionEventId, [this, InTransactionEventId, &OutIsLive](const int64 InPackageNameId)
	{
		int64 HeadTransactionEventIdAtLastSave = 0;
		if (!Statements->GetPackageTransactionEventIdAtLastSave(InPackageNameId, HeadTransactionEventIdAtLastSave))
		{
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}
		if (InTransactionEventId > HeadTransactionEventIdAtLastSave)
		{
			OutIsLive = true;
			return ESQLitePreparedStatementExecuteRowResult::Stop;
		}
		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});
}

bool FConcertSyncSessionDatabase::GetLiveTransactionEventIds(TArray<int64>& OutTransactionEventIds) const
{
	OutTransactionEventIds.Reset();

	bool bResult = false;
	{
		TSet<int64> TransactionEventIdsSet;
		bResult = Statements->GetPackageNameIdsWithTransactions([this, &TransactionEventIdsSet](const int64 InPackageNameId)
		{
			const bool bInnerResult = EnumerateLiveTransactionEventIdsForPackage(InPackageNameId, [&TransactionEventIdsSet](const int64 InTransactionEventId)
			{
				TransactionEventIdsSet.Add(InTransactionEventId);
				return true;
			});
			return bInnerResult
				? ESQLitePreparedStatementExecuteRowResult::Continue
				: ESQLitePreparedStatementExecuteRowResult::Error;
		});

		if (bResult)
		{
			OutTransactionEventIds = TransactionEventIdsSet.Array();
			OutTransactionEventIds.Sort();
		}
	}
	return bResult;
}

bool FConcertSyncSessionDatabase::GetLiveTransactionEventIdsForPackage(const FName InPackageName, TArray<int64>& OutTransactionEventIds) const
{
	OutTransactionEventIds.Reset();
	return EnumerateLiveTransactionEventIdsForPackage(InPackageName, [&OutTransactionEventIds](const int64 InTransactionEventId)
	{
		OutTransactionEventIds.Add(InTransactionEventId);
		return true;
	});
}

bool FConcertSyncSessionDatabase::PackageHasLiveTransactions(const FName InPackageName, bool& OutHasLiveTransaction) const
{
	OutHasLiveTransaction = false;
	return EnumerateLiveTransactionEventIdsForPackage(InPackageName, [&OutHasLiveTransaction](const int64)
	{
		OutHasLiveTransaction = true;
		return false;
	});
}

bool FConcertSyncSessionDatabase::EnumerateLiveTransactionEventIdsForPackage(const FName InPackageName, TFunctionRef<bool(int64)> InCallback) const
{
	int64 PackageNameId = 0;
	if (!GetPackageNameId(InPackageName, PackageNameId))
	{
		// If the package name isn't mapped in the database, then there's no transactions for this package
		return true;
	}
	return EnumerateLiveTransactionEventIdsForPackage(PackageNameId, InCallback);
}

bool FConcertSyncSessionDatabase::GetPackageNamesWithLiveTransactions(TArray<FName>& OutPackageNames) const
{
	OutPackageNames.Reset();
	return EnumeratePackageNamesWithLiveTransactions([&OutPackageNames](const FName InPackageName)
	{
		OutPackageNames.Add(InPackageName);
		return true;
	});
}

bool FConcertSyncSessionDatabase::EnumeratePackageNamesWithLiveTransactions(TFunctionRef<bool(FName)> InCallback) const
{
	return Statements->GetPackageNameIdsMaxTransactionId([this, &InCallback](const int64 InPackageNameId, const int64 InMaxTransactionEventId)
	{
		// Get the transaction id at last save, if the max transaction id for a package name id is greater than its transaction id at last save, it has live transactions
		int64 HeadTransactionEventIdAtLastSave = 0;
		Statements->GetPackageTransactionEventIdAtLastSave(InPackageNameId, HeadTransactionEventIdAtLastSave);
		
		if (InMaxTransactionEventId > HeadTransactionEventIdAtLastSave)
		{
			FName PackageName;
			if (GetPackageName(InPackageNameId, PackageName))
			{
				return InCallback(PackageName)
					? ESQLitePreparedStatementExecuteRowResult::Continue
					: ESQLitePreparedStatementExecuteRowResult::Stop;
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}
		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});
}

bool FConcertSyncSessionDatabase::EnumerateLiveTransactionEventIdsForPackage(const int64 InPackageNameId, TFunctionRef<bool(int64)> InCallback) const
{
	int64 HeadTransactionEventIdAtLastSave = 0;
	if (!Statements->GetPackageTransactionEventIdAtLastSave(InPackageNameId, HeadTransactionEventIdAtLastSave) || HeadTransactionEventIdAtLastSave == MAX_int64)
	{
		return false;
	}

	return Statements->GetTransactionEventIdsInRangeForPackageNameId(InPackageNameId, HeadTransactionEventIdAtLastSave + 1, [&InCallback](const int64 InTransactionEventId)
	{
		return InCallback(InTransactionEventId)
			? ESQLitePreparedStatementExecuteRowResult::Continue
			: ESQLitePreparedStatementExecuteRowResult::Stop;
	});
}

bool FConcertSyncSessionDatabase::AddDummyPackageEvent(const FName InPackageName, int64& OutPackageEventId)
{
	auto AddDummyPackageEventImpl = [this, InPackageName, &OutPackageEventId]()
	{
		// Find the head package event info
		{
			int64 PackageNameId = 0;
			if (GetPackageNameId(InPackageName, PackageNameId) && Statements->GetPackageHeadEventId(PackageNameId, OutPackageEventId) && OutPackageEventId > 0)
			{
				// If the head package event is a dummy event with no activity associated with it, we'll re-use the head package event, otherwise we'll add a new one
				FString FoundDataFilename;
				FConcertSyncPackageEvent FoundPackageEvent;
				if (Statements->GetPackageEventForId(OutPackageEventId, FoundPackageEvent.PackageRevision, FoundPackageEvent.Package.Info, FoundDataFilename))
				{
					if (FoundPackageEvent.Package.Info.PackageUpdateType == EConcertPackageUpdateType::Dummy)
					{
						// Does this package have associated activity? If so, we need to keep it as-is
						FConcertSyncActivity FoundActivity;
						if (!GetActivityForEvent(OutPackageEventId, EConcertSyncActivityEventType::Package, FoundActivity))
						{
							// Update this package event
							return Statements->GetTransactionMaxEventId(FoundPackageEvent.Package.Info.TransactionEventIdAtSave)
								&& Statements->SetPackageEvent(OutPackageEventId, PackageNameId, FoundPackageEvent.PackageRevision, FoundPackageEvent.Package.Info.TransactionEventIdAtSave, FoundPackageEvent.Package.Info, FoundDataFilename);
						}
					}
				}
			}
		}

		// Add a new package event
		FConcertSyncPackageEvent DummyPackageEvent;
		DummyPackageEvent.Package.Info.PackageName = InPackageName;
		DummyPackageEvent.Package.Info.PackageUpdateType = EConcertPackageUpdateType::Dummy;
		return Statements->GetTransactionMaxEventId(DummyPackageEvent.Package.Info.TransactionEventIdAtSave)
			&& AddPackageEvent(DummyPackageEvent, OutPackageEventId);
	};

	FConcertSyncSessionDatabaseScopedTransaction ScopedTransaction(*Statements);
	return ScopedTransaction.CommitOrRollback(AddDummyPackageEventImpl());
}

bool FConcertSyncSessionDatabase::AddPackageEvent(const FConcertSyncPackageEvent& InPackageEvent, int64& OutPackageEventId)
{
	// Get the next package ID
	if (!GetPackageMaxEventId(OutPackageEventId) || OutPackageEventId == MAX_int64)
	{
		return false;
	}
	++OutPackageEventId;

	// Get the next package revision
	int64 PackageRevision = 0;
	if (!GetPackageHeadRevision(InPackageEvent.Package.Info.PackageName, PackageRevision) || PackageRevision == MAX_int64)
	{
		return false;
	}
	++PackageRevision;

	return SetPackageEvent(OutPackageEventId, PackageRevision, InPackageEvent.Package);
}

bool FConcertSyncSessionDatabase::UpdatePackageEvent(const int64 InPackageEventId, const FConcertSyncPackageEvent& InPackageEvent)
{
	int64 MaxPackageEventId;
	if (GetPackageMaxEventId(MaxPackageEventId) && InPackageEventId <= MaxPackageEventId) // Ensure the package ID is in bound.
	{
		FConcertSyncSessionDatabaseScopedTransaction ScopedTransaction(*Statements);
		return ScopedTransaction.CommitOrRollback(
			SetPackageEvent(InPackageEventId, InPackageEvent)
		);
	}

	return false;
}

bool FConcertSyncSessionDatabase::SetPackageEvent(const int64 InPackageEventId, const FConcertSyncPackageEvent& InPackageEvent, const bool bMetaDataOnly)
{
	return SetPackageEvent(InPackageEventId, InPackageEvent.PackageRevision, InPackageEvent.Package, bMetaDataOnly);
}

bool FConcertSyncSessionDatabase::SetPackageEvent(const int64 InPackageEventId, const int64 InPackageRevision, const FConcertPackage& InPackage, const bool bMetaDataOnly)
{
	if (!ensureAlwaysMsgf(InPackageRevision > 0, TEXT("Invalid package revision! Must be greater than zero.")))
	{
		return false;
	}
	if (!ensureAlwaysMsgf(!InPackage.Info.PackageName.IsNone(), TEXT("Invalid package name! Must be set.")))
	{
		return false;
	}

	// Ensure an entry for this package name
	int64 PackageNameId = 0;
	if (!EnsurePackageNameId(InPackage.Info.PackageName, PackageNameId))
	{
		return false;
	}

	// Write the data blob file
	const FString PackageDataFilename = PackageDataUtil::GetDataFilename(InPackage.Info.PackageName, InPackageRevision);

	if (!bMetaDataOnly)
	{
		const FString PackageDataPathname = PackageDataUtil::GetDataPath(SessionPath) / PackageDataFilename;
		if (!SavePackage(PackageDataPathname, InPackage.Info, InPackage.PackageData))
		{
			return false;
		}
	}

	// Add the database entry
	return Statements->SetPackageEvent(InPackageEventId, PackageNameId, InPackageRevision, InPackage.Info.TransactionEventIdAtSave, InPackage.Info, PackageDataFilename);
}

bool FConcertSyncSessionDatabase::GetPackageEvent(const int64 InPackageEventId, FConcertSyncPackageEvent& OutPackageEvent, const bool InMetaDataOnly) const
{
	FString DataFilename;
	if (Statements->GetPackageEventForId(InPackageEventId, OutPackageEvent.PackageRevision, OutPackageEvent.Package.Info, DataFilename))
	{
		if (InMetaDataOnly)
		{
			OutPackageEvent.Package.PackageData.Reset();
			return true;
		}

		const FString PackageDataPathname = PackageDataUtil::GetDataPath(SessionPath) / DataFilename;
		if (LoadPackage(PackageDataPathname, nullptr, &OutPackageEvent.Package.PackageData))
		{
			return true;
		}
	}

	return false;
}

bool FConcertSyncSessionDatabase::GetPackageNamesWithHeadRevision(TArray<FName>& OutPackageNames, bool IgnorePersisted) const
{
	OutPackageNames.Reset();
	return EnumeratePackageNamesWithHeadRevision([&OutPackageNames](const FName InPackageName)
	{
		OutPackageNames.Add(InPackageName);
		return true;
	}, IgnorePersisted);
}

bool FConcertSyncSessionDatabase::EnumeratePackageNamesWithHeadRevision(TFunctionRef<bool(FName)> InCallback, bool IgnorePersisted) const
{
	// if we ignore packages with persist event we need to compare head revision against entry in the persist table.
	if (IgnorePersisted)
	{
		return Statements->GetMaxPackageEventIdAndTransactionEventIdAtSavePerPackageNameId([this, &InCallback](int64 InPackageNameId, int64 InMaxPackageEventId, int64 TransactionEventIdAtSave)
		{
			// We enumerate the packages if there isn't an entry in persist events with this MaxPackageEventId or 
			// if the TransactionEventIdAtPersist and TransactionEventIdAtSave doesn't match (in case a dummy event got squashed)
			int64 PersistEventId = 0, TransactionEventIdAtPersist = 0;
			if (!Statements->GetPersistEventId(InMaxPackageEventId, PersistEventId, TransactionEventIdAtPersist)
				|| TransactionEventIdAtPersist != TransactionEventIdAtSave)
			{
				FName PackageName;
				if (GetPackageName(InPackageNameId, PackageName))
				{
					return InCallback(PackageName)
						? ESQLitePreparedStatementExecuteRowResult::Continue
						: ESQLitePreparedStatementExecuteRowResult::Stop;
				}
				return ESQLitePreparedStatementExecuteRowResult::Error;
			}
			return ESQLitePreparedStatementExecuteRowResult::Continue;
		});
	}
	
	// otherwise we can just gather distinct packages in the package events table
	return Statements->GetUniquePackageNameIdsForPackageEvents([this, &InCallback](int64 InPackageNameId)
	{
		FName PackageName;
		if (GetPackageName(InPackageNameId, PackageName))
		{
			return InCallback(PackageName)
				? ESQLitePreparedStatementExecuteRowResult::Continue
				: ESQLitePreparedStatementExecuteRowResult::Stop;
		}
		return ESQLitePreparedStatementExecuteRowResult::Error;
	});
}

bool FConcertSyncSessionDatabase::EnumerateHeadRevisionPackageData(TFunctionRef<bool(FConcertPackage&&)> InCallback, const bool InMetaDataOnly) const
{
	return Statements->GetUniquePackageNameIdsForPackageEvents([this, &InCallback, InMetaDataOnly](int64 InPackageNameId)
	{
		int64 PackageHeadRevision = 0;
		if (Statements->GetPackageHeadRevision(InPackageNameId, PackageHeadRevision))
		{
			FString DataFilename;
			FConcertPackage Package;
			if (Statements->GetPackageDataForRevision(InPackageNameId, PackageHeadRevision, Package.Info, DataFilename))
			{
				const FString PackageDataPathname = PackageDataUtil::GetDataPath(SessionPath) / DataFilename;
				if (InMetaDataOnly || LoadPackage(PackageDataPathname, nullptr, &Package.PackageData))
				{
					return InCallback(MoveTemp(Package))
						? ESQLitePreparedStatementExecuteRowResult::Continue
						: ESQLitePreparedStatementExecuteRowResult::Stop;
				}
			}
		}
		return ESQLitePreparedStatementExecuteRowResult::Error;
	});
}

bool FConcertSyncSessionDatabase::GetPackageMaxEventId(int64& OutPackageEventId) const
{
	return Statements->GetPackageMaxEventId(OutPackageEventId);
}

bool FConcertSyncSessionDatabase::AddPersistEventForHeadRevision(FName InPackageName, int64& OutPersistEventId)
{
	int64 PackageNameId = 0, HeadPackageEventId = 0, TransactionEventIdAtSave = 0;
	if (GetPackageNameId(InPackageName, PackageNameId) 
		&& Statements->GetPackageHeadEventIdAndTransactionIdAtSave(PackageNameId, HeadPackageEventId, TransactionEventIdAtSave)
		&& HeadPackageEventId > 0)
	{
		Statements->AddPersistEvent(HeadPackageEventId, TransactionEventIdAtSave, OutPersistEventId);
	}
	return false;
}

bool FConcertSyncSessionDatabase::GetPackageDataForRevision(const FName InPackageName, FConcertPackage& OutPackage, const int64* InPackageRevision) const
{
	return GetPackageDataForRevision(InPackageName, OutPackage.Info, &OutPackage.PackageData, InPackageRevision);
}

bool FConcertSyncSessionDatabase::GetPackageDataForRevision(const FName InPackageName, FConcertPackageInfo& OutPackageInfo, TArray<uint8>* OutPackageData, const int64* InPackageRevision) const
{
	int64 PackageRevision = 0;
	if (InPackageRevision)
	{
		PackageRevision = *InPackageRevision;
	}
	else if (!GetPackageHeadRevision(InPackageName, PackageRevision))
	{
		return false;
	}
	if (PackageRevision == 0)
	{
		return false;
	}

	int64 PackageNameId = 0;
	if (!GetPackageNameId(InPackageName, PackageNameId))
	{
		return false;
	}

	FString DataFilename;
	if (Statements->GetPackageDataForRevision(PackageNameId, PackageRevision, OutPackageInfo, DataFilename))
	{
		if (!OutPackageData)
		{
			return true;
		}

		const FString PackageDataPathname = PackageDataUtil::GetDataPath(SessionPath) / DataFilename;
		if (LoadPackage(PackageDataPathname, nullptr, OutPackageData))
		{
			return true;
		}
	}

	return false;
}

bool FConcertSyncSessionDatabase::GetPackageHeadRevision(const FName InPackageName, int64& OutRevision) const
{
	int64 PackageNameId = 0;
	if (!GetPackageNameId(InPackageName, PackageNameId))
	{
		// If the package name isn't mapped in the database, then there's no history for this package which means it's at revision zero
		OutRevision = 0;
		return true;
	}

	return Statements->GetPackageHeadRevision(PackageNameId, OutRevision);
}

bool FConcertSyncSessionDatabase::IsHeadRevisionPackageEvent(const int64 InPackageEventId, bool& OutIsHeadRevision) const
{
	int64 PackageNameId = 0;
	int64 PackageRevision = 0;
	int64 PackageHeadRevision = 0;
	if (Statements->GetPackageNameIdAndRevisonForId(InPackageEventId, PackageNameId, PackageRevision) && Statements->GetPackageHeadRevision(PackageNameId, PackageHeadRevision))
	{
		OutIsHeadRevision = PackageRevision == PackageHeadRevision;
		return true;
	}
	return false;
}

bool FConcertSyncSessionDatabase::GetObjectPathName(const int64 InObjectNameId, FName& OutObjectPathName) const
{
	return Statements->GetObjectPathName(InObjectNameId, OutObjectPathName);
}

bool FConcertSyncSessionDatabase::GetObjectNameId(const FName InObjectPathName, int64& OutObjectNameId) const
{
	return Statements->GetObjectNameId(InObjectPathName, OutObjectNameId);
}

bool FConcertSyncSessionDatabase::EnsureObjectNameId(const FName InObjectPathName, int64& OutObjectNameId)
{
	return GetObjectNameId(InObjectPathName, OutObjectNameId)
		|| Statements->AddObjectPathName(InObjectPathName, OutObjectNameId);
}

bool FConcertSyncSessionDatabase::GetPackageName(const int64 InPackageNameId, FName& OutPackageName) const
{
	return Statements->GetPackageName(InPackageNameId, OutPackageName);
}

bool FConcertSyncSessionDatabase::GetPackageNameId(const FName InPackageName, int64& OutPackageNameId) const
{
	return Statements->GetPackageNameId(InPackageName, OutPackageNameId);
}

bool FConcertSyncSessionDatabase::EnsurePackageNameId(const FName InPackageName, int64& OutPackageNameId)
{
	return GetPackageNameId(InPackageName, OutPackageNameId)
		|| Statements->AddPackageName(InPackageName, OutPackageNameId);
}

bool FConcertSyncSessionDatabase::MapResourceNamesForLock(const int64 InLockEventId, const TArray<FName>& InResourceNames)
{
	bool bResult = Statements->UnmapObjectNameIdsForLockEventId(InLockEventId);
	for (const FName ResourceName : InResourceNames)
	{
		int64 ObjectNameId = 0;
		bResult &= EnsureObjectNameId(ResourceName, ObjectNameId) && Statements->MapObjectNameIdToLockEventId(ObjectNameId, InLockEventId);
	}
	return bResult;
}

bool FConcertSyncSessionDatabase::MapPackageNamesForTransaction(const int64 InTransactionEventId, const FConcertTransactionFinalizedEvent& InTransactionEvent)
{
	bool bResult = Statements->UnmapPackageNameIdsForTransactionEventId(InTransactionEventId);
	for (const FName PackageName : InTransactionEvent.ModifiedPackages)
	{
		int64 PackageNameId = 0;
		bResult &= EnsurePackageNameId(PackageName, PackageNameId) && Statements->MapPackageNameIdToTransactionEventId(PackageNameId, InTransactionEventId);
	}
	return bResult;
}

bool FConcertSyncSessionDatabase::MapObjectNamesForTransaction(const int64 InTransactionEventId, const FConcertTransactionFinalizedEvent& InTransactionEvent)
{
	bool bResult = Statements->UnmapObjectNameIdsForTransactionEventId(InTransactionEventId);
	for (const FConcertExportedObject Object : InTransactionEvent.ExportedObjects)
	{
		// TODO: This isn't always the correct way to build the object path (re: SUBOBJECT_DELIMITER)
		const FName ObjectPathName = *FString::Printf(TEXT("%s.%s"), *Object.ObjectId.ObjectOuterPathName.ToString(), *Object.ObjectId.ObjectName.ToString());

		int64 ObjectNameId = 0;
		bResult &= EnsureObjectNameId(ObjectPathName, ObjectNameId) && Statements->MapObjectNameIdToTransactionEventId(ObjectNameId, InTransactionEventId);
	}
	return bResult;
}

bool FConcertSyncSessionDatabase::SaveTransaction(const FString& InTransactionFilename, const FStructOnScope& InTransaction) const
{
	TArray<uint8> SerializedTransactionData;
	return TransactionDataUtil::WriteTransaction(InTransaction, SerializedTransactionData) && TransactionFileCache->SaveAndCacheFile(InTransactionFilename, MoveTemp(SerializedTransactionData));
}

bool FConcertSyncSessionDatabase::LoadTransaction(const FString& InTransactionFilename, FStructOnScope& OutTransaction) const
{
	TArray<uint8> SerializedTransactionData;
	if (TransactionFileCache->FindOrCacheFile(InTransactionFilename, SerializedTransactionData) && TransactionDataUtil::ReadTransaction(SerializedTransactionData, OutTransaction))
	{
		if (ensureAlwaysMsgf(OutTransaction.GetStruct()->IsChildOf(FConcertTransactionEventBase::StaticStruct()), TEXT("LoadTransaction can only be used with types deriving from FConcertTransactionEventBase")))
		{
			return true;
		}
	}
	return false;
}

bool FConcertSyncSessionDatabase::SavePackage(const FString& InPackageFilename, const FConcertPackageInfo& InPackageInfo, const TArray<uint8>& InPackageData) const
{
	TArray<uint8> SerializedPackageData;
	return PackageDataUtil::WritePackage(InPackageInfo, InPackageData, SerializedPackageData) && PackageFileCache->SaveAndCacheFile(InPackageFilename, MoveTemp(SerializedPackageData));
}

bool FConcertSyncSessionDatabase::LoadPackage(const FString& InPackageFilename, FConcertPackageInfo* OutPackageInfo, TArray<uint8>* OutPackageData) const
{
	TArray<uint8> SerializedPackageData;
	return PackageFileCache->FindOrCacheFile(InPackageFilename, SerializedPackageData) && PackageDataUtil::ReadPackage(SerializedPackageData, OutPackageInfo, OutPackageData);
}

bool ConcertSyncSessionDatabaseFilterUtil::TransactionEventPassesFilter(const int64 InTransactionEventId, const FConcertSessionFilter& InSessionFilter, const FConcertSyncSessionDatabase& InDatabase)
{
	check(InDatabase.IsValid());

	if (!InSessionFilter.bOnlyLiveData)
	{
		return true;
	}

	bool bIsLive = false;
	InDatabase.IsLiveTransactionEvent(InTransactionEventId, bIsLive);
	return bIsLive;
}

bool ConcertSyncSessionDatabaseFilterUtil::PackageEventPassesFilter(const int64 InPackageEventId, const FConcertSessionFilter& InSessionFilter, const FConcertSyncSessionDatabase& InDatabase)
{
	check(InDatabase.IsValid());

	if (!InSessionFilter.bOnlyLiveData)
	{
		return true;
	}

	bool bIsHeadRevision = false;
	InDatabase.IsHeadRevisionPackageEvent(InPackageEventId, bIsHeadRevision);
	return bIsHeadRevision;
}
