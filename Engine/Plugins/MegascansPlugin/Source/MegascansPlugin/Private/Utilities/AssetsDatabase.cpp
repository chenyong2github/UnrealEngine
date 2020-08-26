// Copyright Epic Games, Inc. All Rights Reserved.
#include "AssetsDatabase.h"
#include "Misc/Paths.h"
//#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"
#include "SQLiteDatabaseConnection.h"
#include "SQLiteResultSet.h"

TSharedPtr<FAssetsDatabase> FAssetsDatabase::DBInst;

TSharedPtr<FAssetsDatabase> FAssetsDatabase::Get()
{
	if (!DBInst.IsValid())
	{
		DBInst = MakeShareable(new FAssetsDatabase);
	}
	return DBInst;
}

FAssetsDatabase::FAssetsDatabase()
{
	SQLiteDatabase = MakeShareable(new FSQLiteDatabaseConnection);	
	FString DatabasePath = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), TEXT("MSPresets"), TEXT("MSAssets.db"));	
	CreateAssetsDatabase(DatabasePath);	
}

FAssetsDatabase::~FAssetsDatabase()
{
	SQLiteDatabase->Close();
}

void FAssetsDatabase::CreateAssetsDatabase(const FString& DatabasePath)
{		
	SQLiteDatabase->Open(*DatabasePath, nullptr, nullptr);
	FString CreateStatement = TEXT("CREATE TABLE IF NOT EXISTS MegascansAssets("
		"ID TEXT PRIMARY KEY     NOT NULL,"
		"NAME           TEXT    NOT NULL,"
		"PATH            TEXT     NOT NULL,"
		"TYPE            TEXT     NOT NULL"
		");");

	SQLiteDatabase->Execute(*CreateStatement);
}

void FAssetsDatabase::AddRecord(const AssetRecord& Record)
{
	FString InsertStatement = FString::Printf(TEXT("INSERT OR REPLACE INTO MegascansAssets VALUES('%s', '%s', '%s', '%s');"), *Record.ID, *Record.Name, *Record.Path, *Record.Type);
	
	
	SQLiteDatabase->Execute(*InsertStatement);
}

bool FAssetsDatabase::RecordExists(const FString& AssetID, AssetRecord& Record)
{

	FString SelectStatement = FString::Printf(TEXT("SELECT * FROM MegascansAssets WHERE ID = '%s'"), *AssetID);
	
	FSQLiteResultSet* QueryResult = NULL;

	if (SQLiteDatabase->Execute(*SelectStatement, QueryResult))
	{
		for (FSQLiteResultSet::TIterator ResultIterator(QueryResult); ResultIterator; ++ResultIterator)
		{
			Record.ID = ResultIterator->GetString(TEXT("ID"));
			Record.Name = ResultIterator->GetString(TEXT("NAME"));
			Record.Path = ResultIterator->GetString(TEXT("PATH"));
			Record.Type = ResultIterator->GetString(TEXT("TYPE"));
			return true;
		}

	}	
	return false;
}
