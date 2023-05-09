// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Insights/Table/ViewModels/Table.h"

namespace UE
{
	namespace Insights
	{
		class FTableColumn;
	}
}

// Column identifiers
struct FAssetTableColumns
{
	static const FName CountColumnId;
	static const FName NameColumnId;
	static const FName TypeColumnId;
	static const FName PathColumnId;
	static const FName PrimaryTypeColumnId;
	static const FName PrimaryNameColumnId;
	//static const FName ManagedDiskSizeColumnId;
	//static const FName DiskSizeColumnId;
	static const FName StagedCompressedSizeColumnId;
	static const FName TotalUsageCountColumnId;
	//static const FName CookRuleColumnId;
	//static const FName ChunksColumnId;
	static const FName NativeClassColumnId;
	static const FName GameFeaturePluginColumnId;
};

class FAssetTableRow
{
	friend class FAssetTable;
	friend class SAssetAuditBrowser;

public:
	FAssetTableRow()
	{
	}
	~FAssetTableRow()
	{
	}

	const TCHAR* GetType() const { return *Type; }
	const TCHAR* GetName() const { return *Name; }
	const TCHAR* GetPath() const { return *Path; }
	const TCHAR* GetPrimaryType() const { return *PrimaryType; }
	const TCHAR* GetPrimaryName() const { return *PrimaryName; }
	//int64 GetManagedDiskSize() const { return ManagedDiskSize; }
	//int64 GetDiskSize() const { return DiskSize; }
	int64 GetStagedCompressedSize() const { return StagedCompressedSize; }
	int64 GetTotalUsageCount() const { return TotalUsageCount; }
	//const TCHAR* GetCookRule() const { return *CookRule; }
	//const TCHAR* GetChunks() const { return *Chunks; }
	const TCHAR* GetNativeClass() const { return *NativeClass; }
	const TArray<int32>& GetDependencies() const { return Dependencies; }
	const TCHAR* GetGameFeaturePlugin() const { return *GameFeaturePlugin; }
	FLinearColor GetColor() const { return Color; }

private:
	FString Type;
	FString Name;
	FString Path;
	FString PrimaryType;
	FString PrimaryName;
	//int64 ManagedDiskSize = 0;
	//int64 DiskSize = 0;
	int64 StagedCompressedSize = 0;
	int64 TotalUsageCount = 0;
	//FString CookRule;
	//FString Chunks;
	FString NativeClass;
	TArray<int32> Dependencies;
	FString GameFeaturePlugin;
	FLinearColor Color;
};

class FAssetTable : public UE::Insights::FTable
{
public:
	FAssetTable();
	virtual ~FAssetTable();

	virtual void Reset();

	TArray<FAssetTableRow>& GetAssets() { return Assets; }
	const TArray<FAssetTableRow>& GetAssets() const { return Assets; }

	bool IsValidRowIndex(int32 InIndex) const { return InIndex >= 0 && InIndex < Assets.Num(); }
	FAssetTableRow* GetAsset(int32 InIndex) { return IsValidRowIndex(InIndex) ? &Assets[InIndex] : nullptr; }
	const FAssetTableRow* GetAsset(int32 InIndex) const { return IsValidRowIndex(InIndex) ? &Assets[InIndex] : nullptr; }
	FAssetTableRow& GetAssetChecked(int32 InIndex) { check(IsValidRowIndex(InIndex)); return Assets[InIndex]; }
	const FAssetTableRow& GetAssetChecked(int32 InIndex) const { check(IsValidRowIndex(InIndex)); return Assets[InIndex]; }

	int32 GetTotalAssetCount() const { return Assets.Num(); }
	int32 GetVisibleAssetCount() const { return VisibleAssetCount; }
	int32 GetHiddenAssetCount() const { return Assets.Num() - VisibleAssetCount; }

	void SetVisibleAssetCount(int32 InVisibleAssetCount) { check(VisibleAssetCount <= Assets.Num()); VisibleAssetCount = InVisibleAssetCount; }

	void AddAsset(const FAssetTableRow& AssetRow) { Assets.Add(AssetRow); }

	void ClearAllData()
	{
		Assets.Empty();
		VisibleAssetCount = 0;
	}

private:
	void AddDefaultColumns();

private:
	TArray<FAssetTableRow> Assets;
	int32 VisibleAssetCount = 0;
};
