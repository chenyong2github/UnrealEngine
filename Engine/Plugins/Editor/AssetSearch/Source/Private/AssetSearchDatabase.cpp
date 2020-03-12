// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetSearchDatabase.h"

#include "SQLiteDatabase.h"
#include "HAL/FileManager.h"
#include "UObject/StructOnScope.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "AssetData.h"
#include "Misc/TextFilterExpressionEvaluator.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Engine/World.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/StringBuilder.h"

PRAGMA_DISABLE_OPTIMIZATION

DECLARE_LOG_CATEGORY_CLASS(LogAssetSearch, Log, All);

enum class EAssetSearchDatabaseVersion
{
	Empty = 0,
	Initial = 1,
	IndexingAssetIdsAssetPathsUnique = 2,
	IntroducingFileHashing = 2,

	// -----<new versions can be added above this line>-------------------------------------------------
	VersionPlusOne,
	LatestVersion = VersionPlusOne - 1
};

class FAssetSearchDatabaseStatements
{
private:
	FSQLiteDatabase& Database;

public:
	explicit FAssetSearchDatabaseStatements(FSQLiteDatabase& InDatabase)
		: Database(InDatabase)
	{
		check(Database.IsValid());
	}

	bool CreatePreparedStatements()
	{
		check(Database.IsValid());

#define PREPARE_STATEMENT(VAR)																				\
			(VAR) = Database.PrepareStatement<decltype(VAR)>(ESQLitePreparedStatementFlags::Persistent);	\
			if (!(VAR).IsValid()) { return false; }

		PREPARE_STATEMENT(Statement_BeginTransaction);
		PREPARE_STATEMENT(Statement_CommitTransaction);
		PREPARE_STATEMENT(Statement_RollbackTransaction);

		PREPARE_STATEMENT(Statement_GetAllAssets);
		PREPARE_STATEMENT(Statement_GetAssetIdForAssetPath);
		PREPARE_STATEMENT(Statement_IsAssetUpToDate);
		PREPARE_STATEMENT(Statement_GetTotalSearchRecords);
		PREPARE_STATEMENT(Statement_AddAssetToAssetTable);
		PREPARE_STATEMENT(Statement_AddAssetProperty);
		PREPARE_STATEMENT(Statement_DeleteEntriesForAsset);

		PREPARE_STATEMENT(Statement_SearchAssetsFTS);

		PREPARE_STATEMENT(Statement_AddFileInfo);
		PREPARE_STATEMENT(Statement_UpdateFileInfo);
		PREPARE_STATEMENT(Statement_GetFileInfo);
		PREPARE_STATEMENT(Statement_GetAllFileInfos);

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
	 * Application Statements
	 */

	SQLITE_PREPARED_STATEMENT(FIsAssetUpToDate,
		"SELECT index_hash FROM table_assets WHERE asset_path = ?1;",
		SQLITE_PREPARED_STATEMENT_COLUMNS(FString),
		SQLITE_PREPARED_STATEMENT_BINDINGS(FString)
	);
	private: FIsAssetUpToDate Statement_IsAssetUpToDate;
	public: bool IsAssetUpToDate(const FAssetData& InAssetData, const FString& IndexedJsonHash)
	{
		FString OutIndexedJsonHash;
		if (Statement_IsAssetUpToDate.BindAndExecuteSingle(InAssetData.ObjectPath.ToString(), OutIndexedJsonHash))
		{
			return OutIndexedJsonHash.Equals(IndexedJsonHash, ESearchCase::CaseSensitive);
		}

		return false;
	}

	 SQLITE_PREPARED_STATEMENT(FGetTotalSearchRecords,
		"SELECT COUNT(rowid) FROM table_asset_properties;",
		SQLITE_PREPARED_STATEMENT_COLUMNS(int64),
		SQLITE_PREPARED_STATEMENT_BINDINGS()
	);
	private: FGetTotalSearchRecords Statement_GetTotalSearchRecords;
	public: int64 GetTotalSearchRecords()
	{
		int64 OutTotalSearchRecords;
		if (Statement_GetTotalSearchRecords.BindAndExecuteSingle(OutTotalSearchRecords))
		{
			return OutTotalSearchRecords;
		}

		return INDEX_NONE;
	}

	SQLITE_PREPARED_STATEMENT(FGetAssetIdForAssetPath,
		"SELECT assetid FROM table_assets WHERE asset_path = ?1;",
		SQLITE_PREPARED_STATEMENT_COLUMNS(int64),
		SQLITE_PREPARED_STATEMENT_BINDINGS(FString)
	);
	private: FGetAssetIdForAssetPath Statement_GetAssetIdForAssetPath;
	public: int64 GetAssetIdForAsset(const FAssetData& InAssetData)
	{
		int64 OutAssetId = INDEX_NONE;
		if (Statement_GetAssetIdForAssetPath.BindAndExecuteSingle(InAssetData.ObjectPath.ToString(), OutAssetId))
		{
			return OutAssetId;
		}

		return INDEX_NONE;
	}

	struct FCachedFileInfo
	{
		int64 FileId;
		FString FilePath;
		FDateTime LastModifed;
		FString Hash;

		FAssetFileInfo ToAssetFileInfo() const
		{
			FAssetFileInfo AssetFileInfo;
			AssetFileInfo.LastModified = LastModifed;
			AssetFileInfo.PackageName = *FilePath;
			LexFromString(AssetFileInfo.Hash, *Hash);

			return AssetFileInfo;
		}
	};

	SQLITE_PREPARED_STATEMENT(FGetFileInfo,
		"SELECT fileid, file_last_modified, file_hash FROM table_files WHERE file_path = ?1;",
		SQLITE_PREPARED_STATEMENT_COLUMNS(int64, FDateTime, FString),
		SQLITE_PREPARED_STATEMENT_BINDINGS(FString)
	);
	private: FGetFileInfo Statement_GetFileInfo;
	public: bool GetFileInfo(const FString& InFullFilePath, FCachedFileInfo& OutFileInfo)
	{
		OutFileInfo.FilePath = InFullFilePath.ToLower();
		if (Statement_GetFileInfo.BindAndExecuteSingle(OutFileInfo.FilePath, OutFileInfo.FileId, OutFileInfo.LastModifed, OutFileInfo.Hash))
		{
			return true;
		}

		return false;
	}

	SQLITE_PREPARED_STATEMENT(FGetAllFileInfos,
		"SELECT file_path, file_last_modified, file_hash FROM table_files;",
		SQLITE_PREPARED_STATEMENT_COLUMNS(FString, FDateTime, FString),
		SQLITE_PREPARED_STATEMENT_BINDINGS()
	);
	FGetAllFileInfos Statement_GetAllFileInfos;
	bool GetAllFileInfos(TFunctionRef<ESQLitePreparedStatementExecuteRowResult(FAssetFileInfo&&)> InCallback)
	{
		return Statement_GetAllFileInfos.BindAndExecute([&InCallback](const FGetAllFileInfos& InStatement)
		{
			FCachedFileInfo FileInfo;
			if (InStatement.GetColumnValues(FileInfo.FilePath, FileInfo.LastModifed, FileInfo.Hash))
			{
				return InCallback(FileInfo.ToAssetFileInfo());
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(
		FUpdateFileInfo,
		" UPDATE table_files SET file_last_modified = ?2, file_hash = ?3 WHERE file_path = ?1;",
		SQLITE_PREPARED_STATEMENT_BINDINGS(FString, FDateTime, FString)
	);
	private: FUpdateFileInfo Statement_UpdateFileInfo;
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(
		FAddFileInfo,
		" INSERT INTO table_files(file_path, file_last_modified, file_hash)"
		" VALUES(?1, ?2, ?3);",
		SQLITE_PREPARED_STATEMENT_BINDINGS(FString, FDateTime, FString)
	);
	private: FAddFileInfo Statement_AddFileInfo;
	public: bool AddOrUpdateFileInfo(const FAssetData& InAssetData, FAssetFileInfo& OutFileInfo)
	{
		const FString PackageName = InAssetData.PackageName.ToString();
		const bool bIsWorldAsset = (InAssetData.AssetClass == UWorld::StaticClass()->GetFName());
		const FString Extension = bIsWorldAsset ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
		const FString FilePath = FPackageName::LongPackageNameToFilename(PackageName, Extension);
		const FString FullFilePath = FPaths::ConvertRelativePathToFull(FilePath);

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		const FDateTime CurrentLastModified = PlatformFile.GetTimeStamp(*FullFilePath);

		FCachedFileInfo FileInfo;
		if (GetFileInfo(PackageName.ToLower(), FileInfo))
		{
			if (CurrentLastModified == FileInfo.LastModifed)
			{
				OutFileInfo = FileInfo.ToAssetFileInfo();
				return false;
			}

			OutFileInfo = FileInfo.ToAssetFileInfo();
			OutFileInfo.LastModified = CurrentLastModified;
			OutFileInfo.Hash = FMD5Hash::HashFile(*FullFilePath);
			Statement_UpdateFileInfo.BindAndExecuteSingle(PackageName.ToLower(), OutFileInfo.LastModified, LexToString(OutFileInfo.Hash));
			return true;
		}
		else
		{
			OutFileInfo = FileInfo.ToAssetFileInfo();
			OutFileInfo.LastModified = CurrentLastModified;
			OutFileInfo.Hash = FMD5Hash::HashFile(*FullFilePath);
			Statement_AddFileInfo.BindAndExecuteSingle(PackageName.ToLower(), OutFileInfo.LastModified, LexToString(OutFileInfo.Hash));
			return true;
		}
	}

	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(
		FAddAssetToAssetTable,
		" INSERT INTO table_assets(asset_name, asset_class, asset_path, index_hash)"
		" VALUES(?1, ?2, ?3, ?4);",
		SQLITE_PREPARED_STATEMENT_BINDINGS(FString, FString, FString, FString)
	);
	private: FAddAssetToAssetTable Statement_AddAssetToAssetTable;
	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(
		FAddAssetPropertiesFromJson,
		" INSERT INTO table_asset_properties(assetid, object_name, object_path, object_native_class, property_name, property_field, property_class, value_text, value_hidden)"
		" VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);",
		SQLITE_PREPARED_STATEMENT_BINDINGS(int64, FString, FString, FString, FString, FString, FString, FString, FString)
	);
	private: FAddAssetPropertiesFromJson Statement_AddAssetProperty;
	public: bool AddSearchRecord(const FAssetData& InAssetData, const FString& IndexedJson, const FString& IndexedJsonHash)
	{
		if (Statement_AddAssetToAssetTable.BindAndExecute(InAssetData.AssetName.ToString(), InAssetData.AssetClass.ToString(), InAssetData.ObjectPath.ToString(), IndexedJsonHash))
		{
			int64 AssetId = Database.GetLastInsertRowId();

			BeginTransaction();

			TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(IndexedJson);

			EJsonNotation Notation;

			ensure(JsonReader->ReadNext(Notation) && Notation == EJsonNotation::ObjectStart);

			ensure(JsonReader->ReadNext(Notation) && Notation == EJsonNotation::Number && JsonReader->GetIdentifier() == TEXT("version"));
			const int32 SerializerVersion = (int32)JsonReader->GetValueAsNumber();

			ensure(JsonReader->ReadNext(Notation) && Notation == EJsonNotation::ObjectStart && JsonReader->GetIdentifier() == TEXT("indexers"));

			while (JsonReader->ReadNext(Notation))
			{
				// Indexer
				if (Notation == EJsonNotation::ObjectStart)
				{
					const FString IndexerName = JsonReader->GetIdentifier();

					ensure(JsonReader->ReadNext(Notation) && Notation == EJsonNotation::Number && JsonReader->GetIdentifier() == TEXT("version"));
					const int32 IndexerVersion = (int32)JsonReader->GetValueAsNumber();

					ensure(JsonReader->ReadNext(Notation) && Notation == EJsonNotation::ArrayStart && JsonReader->GetIdentifier() == TEXT("objects"));

					// Objects[]
					while (JsonReader->ReadNext(Notation) && Notation != EJsonNotation::ArrayEnd)
					{
						if (Notation == EJsonNotation::ObjectStart)
						{
							FString object_name;
							FString object_path;
							FString object_native_class;

							ensure(JsonReader->ReadNext(Notation) && Notation == EJsonNotation::String && JsonReader->GetIdentifier() == TEXT("name"));
							object_name = JsonReader->GetValueAsString();
							ensure(JsonReader->ReadNext(Notation) && Notation == EJsonNotation::String && JsonReader->GetIdentifier() == TEXT("path"));
							object_path = JsonReader->GetValueAsString();
							ensure(JsonReader->ReadNext(Notation) && Notation == EJsonNotation::String && JsonReader->GetIdentifier() == TEXT("native_class"));
							object_native_class = JsonReader->GetValueAsString();

							ensure(JsonReader->ReadNext(Notation) && Notation == EJsonNotation::ArrayStart && JsonReader->GetIdentifier() == TEXT("properties"));

							FString property_name;
							FString property_field;
							FString property_class;
							FString value_text;
							FString value_hidden;

							// Begin Properties[]
							while (JsonReader->ReadNext(Notation) && Notation != EJsonNotation::ArrayEnd)
							{
								if (Notation == EJsonNotation::ObjectStart)
								{
									// Read all attributes of a property
									while (JsonReader->ReadNext(Notation) && Notation != EJsonNotation::ObjectEnd)
									{
										if (Notation == EJsonNotation::String)
										{
											if (JsonReader->GetIdentifier() == TEXT("name"))
											{
												property_name = JsonReader->GetValueAsString();
											}
											else if (JsonReader->GetIdentifier() == TEXT("field"))
											{
												property_field = JsonReader->GetValueAsString();
											}
											else if (JsonReader->GetIdentifier() == TEXT("class"))
											{
												property_class = JsonReader->GetValueAsString();
											}
											else if (JsonReader->GetIdentifier() == TEXT("value_text"))
											{
												value_text = JsonReader->GetValueAsString();
											}
											else if (JsonReader->GetIdentifier() == TEXT("value_hidden"))
											{
												value_hidden = JsonReader->GetValueAsString();
											}
										}
									}

									if (!Statement_AddAssetProperty.BindAndExecute(AssetId, object_name, object_path, object_native_class, property_name, property_field, property_class, value_text, value_hidden))
									{
										//Log error?
									}
								}
							}
							// End Properties[]
						}
					}
					// End Objects[]
				}
			}

			CommitTransaction();

			return true;
		}

		return false;
	}

	SQLITE_PREPARED_STATEMENT_BINDINGS_ONLY(FDeleteEntriesForAsset, "DELETE FROM table_assets WHERE asset_path = ?1;", SQLITE_PREPARED_STATEMENT_BINDINGS(FString));
	private: FDeleteEntriesForAsset Statement_DeleteEntriesForAsset;
	public: bool DeleteEntriesForAsset(const FAssetData& InAssetData)
	{
		return DeleteEntriesForAsset(InAssetData.ObjectPath.ToString());
	}
	public: bool DeleteEntriesForAsset(const FString& InAssetObjectPath)
	{
		if (Statement_DeleteEntriesForAsset.BindAndExecute(InAssetObjectPath))
		{
			return true;
		}
		return false;
	}

	SQLITE_PREPARED_STATEMENT(FSearchAssetsFTS,
		" SELECT "
		"     asset_name, "
		"     asset_class, "
		"     asset_path, "
		"     object_name, "
		"     object_path, "
		"     object_native_class, "
		"     property_name, "
		"     property_field, "
		"     property_class, "
		"     value_text, "
		"     value_hidden, "
		"     rank as score "
		" FROM table_asset_properties_fts "
		" WHERE table_asset_properties_fts MATCH ?1 "
		//" ORDER BY rank "
		";",
		SQLITE_PREPARED_STATEMENT_COLUMNS(FString, FString, FString, FString, FString, FString, FString, FString, FString, FString, FString, float),
		SQLITE_PREPARED_STATEMENT_BINDINGS(FString)
	);
	FSearchAssetsFTS Statement_SearchAssetsFTS;
	bool SearchAssets(const FSearchQuery& Query, TFunctionRef<ESQLitePreparedStatementExecuteRowResult(FSearchRecord&&)> InCallback)
	{
		const FString Q = Query.ConvertToDatabaseQuery();

		return Statement_SearchAssetsFTS.BindAndExecute(Q, [&InCallback](const FSearchAssetsFTS& InStatement)
		{
			FSearchRecord Result;
			if (InStatement.GetColumnValues(
				Result.AssetName, Result.AssetClass, Result.AssetPath,
				Result.object_name, Result.object_path, Result.object_native_class,
				Result.property_name, Result.property_field, Result.property_class,
				Result.value_text, Result.value_hidden,
				Result.Score))
			{
				return InCallback(MoveTemp(Result));
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}

	SQLITE_PREPARED_STATEMENT(FGetAllAssets,
		"SELECT asset_path FROM table_assets;",
		SQLITE_PREPARED_STATEMENT_COLUMNS(FString),
		SQLITE_PREPARED_STATEMENT_BINDINGS()
	);
	FGetAllAssets Statement_GetAllAssets;
	bool GetAllAssets(TFunctionRef<ESQLitePreparedStatementExecuteRowResult(FString&&)> InCallback)
	{
		return Statement_GetAllAssets.BindAndExecute([&InCallback](const FGetAllAssets& InStatement)
		{
			FString AssetPath;
			if (InStatement.GetColumnValues(AssetPath))
			{
				return InCallback(MoveTemp(AssetPath));
			}
			return ESQLitePreparedStatementExecuteRowResult::Error;
		}) != INDEX_NONE;
	}
};

class FAssetSearchDatabaseScopedTransaction
{
public:
	explicit FAssetSearchDatabaseScopedTransaction(FAssetSearchDatabaseStatements& InStatements)
		: Statements(InStatements)
		, bHasTransaction(Statements.BeginTransaction()) // This will fail if a transaction is already open
	{
	}

	~FAssetSearchDatabaseScopedTransaction()
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
	FAssetSearchDatabaseStatements& Statements;
	bool bHasTransaction;
};

FAssetSearchDatabase::FAssetSearchDatabase()
	: Database(MakeUnique<FSQLiteDatabase>())
	, DatabaseFileName(TEXT("AssetSearch.db"))
{
}

FAssetSearchDatabase::~FAssetSearchDatabase()
{
	Close();
}

bool FAssetSearchDatabase::IsValid() const
{
	return Database->IsValid();
}

bool FAssetSearchDatabase::Open(const FString& InSessionPath)
{
	return Open(InSessionPath, ESQLiteDatabaseOpenMode::ReadWriteCreate);
}

bool FAssetSearchDatabase::Open(const FString& InSessionPath, const ESQLiteDatabaseOpenMode InOpenMode)
{
	if (Database->IsValid())
	{
		return false;
	}

	if (!Database->Open(*(InSessionPath / DatabaseFileName), ESQLiteDatabaseOpenMode::ReadWriteCreate))
	{
		UE_LOG(LogAssetSearch, Error, TEXT("Failed to open database for '%s': %s"), *InSessionPath, *GetLastError());
		return false;
	}

	SessionPath = InSessionPath;

	// Set the database to use exclusive WAL mode for performance (exclusive works even on platforms without a mmap implementation)
	// Set the database "NORMAL" fsync mode to only perform a fsync when check-pointing the WAL to the main database file (fewer fsync calls are better for performance, with a very slight loss of WAL durability if the power fails)
	Database->Execute(TEXT("PRAGMA cache_size=1000;"));
	Database->Execute(TEXT("PRAGMA page_size=65535;"));
	Database->Execute(TEXT("PRAGMA locking_mode=EXCLUSIVE;"));

	//Database->Execute(TEXT("PRAGMA journal_mode=WAL;"));
	//Database->Execute(TEXT("PRAGMA synchronous=NORMAL;"));

	Database->Execute(TEXT("PRAGMA journal_mode=NORMAL;"));
	Database->Execute(TEXT("PRAGMA synchronous=OFF;"));

	int32 LoadedDatabaseVersion = 0;
	Database->GetUserVersion(LoadedDatabaseVersion);
	if (LoadedDatabaseVersion != (int32)EAssetSearchDatabaseVersion::Empty)
	{
		if (LoadedDatabaseVersion > (int32)EAssetSearchDatabaseVersion::LatestVersion)
		{
			Close();
			UE_LOG(LogAssetSearch, Error, TEXT("Failed to open database for '%s': Database is too new (version %d, expected = %d)"), *InSessionPath, LoadedDatabaseVersion, (int32)EAssetSearchDatabaseVersion::LatestVersion);
			return false;
		}
		else if (LoadedDatabaseVersion < (int32)EAssetSearchDatabaseVersion::LatestVersion)
		{
			Close(true);
			UE_LOG(LogAssetSearch, Log, TEXT("Opened database '%s': Database is too old (version %d, expected = %d), creating new database"), *InSessionPath, LoadedDatabaseVersion, (int32)EAssetSearchDatabaseVersion::LatestVersion);
			return Open(InSessionPath, InOpenMode);
		}
	}

	// Create our required tables
	//========================================================================
	if (!ensure(Database->Execute(TEXT("CREATE TABLE IF NOT EXISTS table_files(fileid INTEGER PRIMARY KEY, file_path TEXT UNIQUE, file_last_modified INTEGER NOT NULL, file_hash);"))))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(TEXT("CREATE TABLE IF NOT EXISTS table_assets(assetid INTEGER PRIMARY KEY, asset_name, asset_class, asset_path TEXT UNIQUE, index_hash);"))))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(TEXT("CREATE TABLE IF NOT EXISTS table_asset_properties(rowid INTEGER PRIMARY KEY, object_name, object_path, object_native_class, property_name, property_field, property_class, value_text, value_hidden, assetid INTEGER, FOREIGN KEY(assetid) REFERENCES table_assets(assetid));"))))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(TEXT("CREATE VIEW IF NOT EXISTS view_asset_properties AS SELECT rowid, object_name, object_path, object_native_class, property_name, property_field, property_class, value_text, value_hidden, table_asset_properties.assetid as assetid, table_assets.asset_name AS asset_name, table_assets.asset_class AS asset_class, table_assets.asset_path AS asset_path, table_assets.index_hash AS index_hash FROM table_asset_properties INNER JOIN table_assets on table_assets.assetid = table_asset_properties.assetid;"))))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(TEXT("CREATE VIRTUAL TABLE IF NOT EXISTS table_asset_properties_fts USING FTS5(asset_name, asset_class UNINDEXED, asset_path UNINDEXED, object_name UNINDEXED, object_path UNINDEXED, object_native_class UNINDEXED, property_name UNINDEXED, property_field UNINDEXED, property_class UNINDEXED, value_text, value_hidden, assetid UNINDEXED, content=view_asset_properties, content_rowid=rowid);"))))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(TEXT(" DROP TRIGGER IF EXISTS table_assets_delete;"))))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(
		TEXT(" CREATE TRIGGER table_assets_delete BEFORE DELETE ON table_assets BEGIN")
		TEXT("     DELETE FROM table_asset_properties WHERE assetid == old.assetid;")
		TEXT(" END;")
	)))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(TEXT(" DROP TRIGGER IF EXISTS table_asset_properties_insert;"))))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(
		TEXT(" CREATE TRIGGER table_asset_properties_insert AFTER INSERT ON table_asset_properties BEGIN")
		TEXT("     INSERT INTO table_asset_properties_fts(rowid, object_name, object_path, object_native_class, property_name, property_field, property_class, value_text, value_hidden, assetid) VALUES (new.rowid, new.object_name, new.object_path, new.object_native_class, new.property_name, new.property_field, new.property_class, new.value_text, new.value_hidden, new.assetid);")
		TEXT(" END;")
		)))
	{
		LogLastError();
		Close();
	}

	if (!ensure(Database->Execute(
		TEXT(" DROP TRIGGER IF EXISTS table_asset_properties_delete;")
	)))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(
		TEXT(" CREATE TRIGGER table_asset_properties_delete AFTER DELETE ON table_asset_properties BEGIN")
		TEXT("     INSERT INTO table_asset_properties_fts(table_asset_properties_fts, rowid, object_name, object_path, object_native_class, property_name, property_field, property_class, value_text, value_hidden, assetid) VALUES('delete', old.rowid, old.object_name, old.object_path, old.object_native_class, old.property_name, old.property_field, old.property_class, old.value_text, old.value_hidden, old.assetid);")
		TEXT(" END;")
	)))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(
		TEXT(" DROP TRIGGER IF EXISTS table_asset_properties_update;")
	)))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(
		TEXT(" CREATE TRIGGER table_asset_properties_update AFTER UPDATE ON table_asset_properties BEGIN")
		TEXT("     INSERT INTO table_asset_properties_fts(table_asset_properties_fts, rowid, object_name, object_path, object_native_class, property_name, property_field, property_class, value_text, value_hidden, assetid) VALUES('delete', old.rowid, old.object_name, old.object_path, old.object_native_class, old.property_name, old.property_field, old.property_class, old.value_text, old.value_hidden, old.assetid);")
		TEXT("     INSERT INTO table_asset_properties_fts(rowid, object_name, object_path, object_native_class, property_name, property_field, property_class, value_text, value_hidden, assetid) VALUES (new.rowid, new.object_name, new.object_path, new.object_native_class, new.property_name, new.property_field, new.property_class, new.value_text, new.value_hidden, new.assetid);")
		TEXT(" END;")
	)))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(
		TEXT("CREATE UNIQUE INDEX IF NOT EXISTS file_path_index ON table_files(file_path);")
	)))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(
		TEXT("CREATE UNIQUE INDEX IF NOT EXISTS asset_path_index ON table_assets(asset_path);")
	)))
	{
		LogLastError();
		Close();
		return false;
	}

	if (!ensure(Database->Execute(
		TEXT("CREATE INDEX IF NOT EXISTS assetid_index ON table_asset_properties(assetid);")
	)))
	{
		LogLastError();
		Close();
		return false;
	}

	//CREATE INDEX blueprint_nodes ON table_assets(json_extract(json, '$.Blueprint.Nodes'))

	// The database will have the latest schema at this point, so update the user-version
	if (!Database->SetUserVersion((int32)EAssetSearchDatabaseVersion::LatestVersion))
	{
		Close();
		return false;
	}

	// Create our required prepared statements
	Statements = MakeUnique<FAssetSearchDatabaseStatements>(*Database);
	if (!ensure(Statements->CreatePreparedStatements()))
	{
		Close();
		return false;
	}

	return true;
}

bool FAssetSearchDatabase::Close(const bool InDeleteDatabase)
{
	if (!Database->IsValid())
	{
		return false;
	}

	// Need to destroy prepared statements before the database can be closed
	Statements.Reset();

	if (!Database->Close())
	{
		UE_LOG(LogAssetSearch, Error, TEXT("Failed to close database for '%s': %s"), *SessionPath, *GetLastError());
		return false;
	}

	if (InDeleteDatabase)
	{
		IFileManager::Get().Delete(*(SessionPath / DatabaseFileName), false);
	}

	SessionPath.Reset();

	return true;
}

FString FAssetSearchDatabase::GetFilename() const
{
	return Database->GetFilename();
}

FString FAssetSearchDatabase::GetLastError() const
{
	return Database->GetLastError();
}

void FAssetSearchDatabase::LogLastError() const
{
	UE_LOG(LogAssetSearch, Error, TEXT("Database Error: %s"), *SessionPath, *GetLastError());
}

bool FAssetSearchDatabase::AddOrUpdateFileInfo(const FAssetData& InAssetData, FAssetFileInfo& OutFileInfo)
{
	if (ensure(Statements))
	{
		return Statements->AddOrUpdateFileInfo(InAssetData, OutFileInfo);
	}

	return false;
}

bool FAssetSearchDatabase::IsAssetUpToDate(const FAssetData& InAssetData, const FString& IndexedJsonHash)
{
	if (ensure(Statements))
	{
		return Statements->IsAssetUpToDate(InAssetData, IndexedJsonHash);
	}

	return false;
}

void FAssetSearchDatabase::AddOrUpdateAsset(const FAssetData& InAssetData, const FString& IndexedJson, const FString& IndexedJsonHash)
{
	if (ensure(Statements))
	{
		if (!ensure(Statements->DeleteEntriesForAsset(InAssetData)))
		{
			LogLastError();
		}

		if (!ensure(Statements->AddSearchRecord(InAssetData, IndexedJson, IndexedJsonHash)))
		{
			LogLastError();
		}
	}
}

bool FAssetSearchDatabase::EnumerateSearchResults(const FSearchQuery& Query, TFunctionRef<bool(FSearchRecord&&)> InCallback)
{
	bool bSuccess = Statements->SearchAssets(Query, [&InCallback](FSearchRecord&& InResult)
	{
		return InCallback(MoveTemp(InResult))
			? ESQLitePreparedStatementExecuteRowResult::Continue
			: ESQLitePreparedStatementExecuteRowResult::Stop;
	});

	return bSuccess;
}

int64 FAssetSearchDatabase::GetTotalSearchRecords() const
{
	if (ensure(Statements))
	{
		return Statements->GetTotalSearchRecords();
	}

	return INDEX_NONE;
}

void FAssetSearchDatabase::RemoveAsset(const FAssetData& InAssetData)
{
	if (!ensure(Statements->DeleteEntriesForAsset(InAssetData)))
	{
		LogLastError();
	}
}

void FAssetSearchDatabase::AddOrUpdateFileInfos(const TArray<FAssetData>& InAssets)
{
	for (const FAssetData& InAsset : InAssets)
	{
		// If it's a redirector act like it has been removed from the system,
		// we don't want old duplicate entries for it.
		if (InAsset.IsRedirector())
		{
			continue;
		}

		// Freshen hash cache
		FAssetFileInfo FileInfo;
		AddOrUpdateFileInfo(InAsset, FileInfo);
	}
}

TMap<FName, FAssetFileInfo> FAssetSearchDatabase::GetAllFileInfos()
{
	TMap<FName, FAssetFileInfo> FileInfos;
	Statements->GetAllFileInfos([&FileInfos](FAssetFileInfo&& InResult)
	{
		FileInfos.Add(InResult.PackageName, InResult);

		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});

	return FileInfos;
}

void FAssetSearchDatabase::RemoveAssetsNotInThisSet(const TArray<FAssetData>& InAssets)
{
	TSet<FString> InAssetPaths;
	for (const FAssetData& InAsset : InAssets)
	{
		// If it's a redirector act like it has been removed from the system,
		// we don't want old duplicate entries for it.
		if (InAsset.IsRedirector())
		{
			continue;
		}

		InAssetPaths.Add(InAsset.ObjectPath.ToString());
	}

	TArray<FString> MissingAssets;

	Statements->GetAllAssets([&MissingAssets, &InAssetPaths](FString&& InResult)
	{
		if (!InAssetPaths.Contains(InResult))
		{
			MissingAssets.Emplace(InResult);
		}

		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});

	for (const FString& MissingAsset : MissingAssets)
	{
		Statements->DeleteEntriesForAsset(MissingAsset);
	}
}

FString FSearchQuery::ConvertToDatabaseQuery() const
{
	TStringBuilder<512> Q;

	FTextFilterExpressionEvaluator Eval(ETextFilterExpressionEvaluatorMode::BasicString);
	const TArray<FExpressionToken>& Tokens = Eval.GetFilterExpressionTokens();
	if (Eval.SetFilterText(FText::FromString(Query)))
	{
		TArray<FString> TokenStreak;
		bool bBreakSteak = false;

		for (int32 i = 0; i < Tokens.Num(); i++)
		{
			const FExpressionToken& Token = Tokens[i];
			const FStringToken& TokenContext = Token.Context;
			const FString TokenString = TokenContext.GetString();
			
			TStringBuilder<64> Phrase;
			if (Token.Node.Cast<TextFilterExpressionParser::FTextToken>())
			{
				if (TokenString.StartsWith(TEXT("\"")) && TokenString.EndsWith(TEXT("\"")))
				{
					Phrase.Append(TokenString);
					Phrase.Append(TEXT(" "));
					bBreakSteak = true;
				}
				else
				{
					Phrase.Append(TEXT("\""));
					Phrase.Append(TokenString);
					Phrase.Append(TEXT("\" * "));

					TokenStreak.Add(TokenString);
				}
			}
			else if (Token.Node.Cast<TextFilterExpressionParser::FOr>())
			{
				Phrase.Append(TEXT(" OR "));
				bBreakSteak = true;
			}
			else if (Token.Node.Cast<TextFilterExpressionParser::FAnd>())
			{
				//Phrase.Append(TEXT(" AND "));
				//bBreakSteak = true;
			}
			else
			{
				bBreakSteak = true;
			}

			if (bBreakSteak)
			{
				if (TokenStreak.Num() > 1)
				{
					Q.Append(TEXT(" OR "));
					Q.Append(TEXT("\""));
					for (const FString SimpleString : TokenStreak)
					{
						Q.Append(SimpleString);
					}
					Q.Append(TEXT("\""));
				}

				bBreakSteak = false;
				TokenStreak.Reset();
			}

			Q.Append(Phrase);
		}

		if (TokenStreak.Num() > 1)
		{
			Q.Append(TEXT(" OR "));
			Q.Append(TEXT("\""));
			for (const FString SimpleString : TokenStreak)
			{
				Q.Append(SimpleString);
			}
			Q.Append(TEXT("\""));
		}
	}
	else
	{
		TArray<FString> Phrases;
		Query.ParseIntoArray(Phrases, TEXT(" "), 1);

		for (FString Phrase : Phrases)
		{
			Q.Append(TEXT("\""));
			Q.Append(Phrase);
			Q.Append(TEXT("\" * "));
		}

		Q.Append(TEXT(" OR "));
		Q.Append(TEXT("\""));
		Q.Append(Query.Replace(TEXT(" "), TEXT("")));
		Q.Append(TEXT("\""));
	}

	return Q.ToString();
}

PRAGMA_ENABLE_OPTIMIZATION
