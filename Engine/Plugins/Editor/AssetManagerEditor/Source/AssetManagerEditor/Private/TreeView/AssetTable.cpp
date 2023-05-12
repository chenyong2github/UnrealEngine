// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTable.h"

#include "Styling/StyleColors.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

// AssetManagerEditor
#include "AssetTreeNode.h"
#include "Insights/Table/ViewModels/TableCellValueFormatter.h"
#include "Insights/Table/ViewModels/TableCellValueGetter.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"
#include "Insights/Table/ViewModels/TableColumn.h"

#define LOCTEXT_NAMESPACE "FAssetTable"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Column identifiers

const FName FAssetTableColumns::CountColumnId(TEXT("Count"));
const FName FAssetTableColumns::NameColumnId(TEXT("Name"));
const FName FAssetTableColumns::TypeColumnId(TEXT("Type"));
const FName FAssetTableColumns::PathColumnId(TEXT("Path"));
//const FName FAssetTableColumns::PackageTypeColumnId(TEXT("PackageType"));
//const FName FAssetTableColumns::PackageNameColumnId(TEXT("PackageName"));
const FName FAssetTableColumns::PrimaryTypeColumnId(TEXT("PrimaryType"));
const FName FAssetTableColumns::PrimaryNameColumnId(TEXT("PrimaryName"));
//const FName FAssetTableColumns::ManagedDiskSizeColumnId(TEXT("ManagedDiskSize"));
//const FName FAssetTableColumns::DiskSizeColumnId(TEXT("DiskSize"));
const FName FAssetTableColumns::StagedCompressedSizeColumnId(TEXT("StagedCompressedSize"));
const FName FAssetTableColumns::TotalUsageCountColumnId(TEXT("TotalUsageCount"));
//const FName FAssetTableColumns::CookRuleColumnId(TEXT("CookRule"));
//const FName FAssetTableColumns::ChunksColumnId(TEXT("Chunks"));
const FName FAssetTableColumns::NativeClassColumnId(TEXT("NativeClass"));
const FName FAssetTableColumns::GameFeaturePluginColumnId(TEXT("GameFeaturePlugin"));

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAssetTableStringStore
////////////////////////////////////////////////////////////////////////////////////////////////////

FAssetTableStringStore::FAssetTableStringStore()
	: Chunks()
	, Cache()
	, TotalInputStringSize(0)
	, TotalStoredStringSize(0)
	, NumInputStrings(0)
	, NumStoredStrings(0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAssetTableStringStore::~FAssetTableStringStore()
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAssetTableStringStore::Reset()
{
	for (FChunk& Chunk : Chunks)
	{
		delete[] Chunk.Buffer;
	}
	Chunks.Reset();
	Cache.Reset();
	TotalInputStringSize = 0;
	TotalStoredStringSize = 0;
	NumInputStrings = 0;
	NumStoredStrings = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FStringView FAssetTableStringStore::Store(const TCHAR* InStr)
{
	if (!InStr)
	{
		return FStringView();
	}
	return Store(FStringView(InStr));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FStringView FAssetTableStringStore::Store(const FStringView InStr)
{
	if (InStr.IsEmpty())
	{
		return FStringView();
	}

	check(InStr.Len() <= GetMaxStringLength());

	TotalInputStringSize += (InStr.Len() + 1) * sizeof(TCHAR);
	++NumInputStrings;

	uint32 Hash = GetTypeHash(InStr);

	TArray<FStringView> CachedStrings;
	Cache.MultiFind(Hash, CachedStrings);
	for (const FStringView& CachedString : CachedStrings)
	{
		if (CachedString.Equals(InStr, SearchCase))
		{
			return CachedString;
		}
	}

	if (Chunks.Num() == 0 ||
		Chunks.Last().Used + InStr.Len() + 1 > ChunkBufferLen)
	{
		AddChunk();
	}

	TotalStoredStringSize += (InStr.Len() + 1) * sizeof(TCHAR);
	++NumStoredStrings;

	FChunk& Chunk = Chunks.Last();
	FStringView StoredStr(Chunk.Buffer + Chunk.Used, InStr.Len());
	FMemory::Memcpy((void*)(Chunk.Buffer + Chunk.Used), (const void*)InStr.GetData(), InStr.Len() * sizeof(TCHAR));
	Chunk.Used += InStr.Len();
	Chunk.Buffer[Chunk.Used] = TEXT('\0');
	Chunk.Used ++;
	Cache.Add(Hash, StoredStr);
	return StoredStr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAssetTableStringStore::AddChunk()
{
	FChunk& Chunk = Chunks.AddDefaulted_GetRef();
	Chunk.Buffer = new TCHAR[ChunkBufferLen];
	Chunk.Used = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAssetTableStringStore::EnumerateStrings(TFunction<void(const FStringView Str)> Callback) const
{
	for (const auto& Pair : Cache)
	{
		Callback(Pair.Value);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAssetTable
////////////////////////////////////////////////////////////////////////////////////////////////////

FAssetTable::FAssetTable()
{
#if 0 // debug, mock data

	VisibleAssetCount = 100;
	constexpr int32 HiddenAssetCount = 50;
	const int32 TotalAssetCount = VisibleAssetCount + HiddenAssetCount;
	
	// Create assets.
	Assets.Reserve(TotalAssetCount);
	for (int32 AssetIndex = 0; AssetIndex < TotalAssetCount; ++AssetIndex)
	{
		FAssetTableRow& Asset = Assets.AddDefaulted_GetRef();

		int32 Id = FMath::Rand();
		int32 Id2 = FMath::Rand();

		Asset.Type = FString::Printf(TEXT("Type%02d"), Id % 10);
		Asset.Name = FString::Printf(TEXT("Name%d"), Id);
		Asset.Path = FString::Printf(TEXT("A%02d/B%02d/C%02d/D%02d"), Id % 11, Id % 13, Id % 17, Id % 19);
		Asset.PrimaryType = FString::Printf(TEXT("PT_%02d"), Id2 % 10);
		Asset.PrimaryName = FString::Printf(TEXT("PN%d"), Id2);
		//Asset.ManagedDiskSize = FMath::Abs(Id * Id);
		//Asset.DiskSize = FMath::Abs(Id * Id * Id);
		Asset.StagedCompressedSize = Asset.DiskSize / 2;
		Asset.TotalUsageCount = Id % 1000;
		//Asset.CookRule = FString::Printf(TEXT("CookRule%02d"), (Id * Id) % 8);
		//Asset.Chunks = FString::Printf(TEXT("Chunks%02d"), (Id * Id + 1) % 41);
		Asset.NativeClass = FString::Printf(TEXT("NativeClass%02d"), (Id * Id * Id) % 8);
		Asset.GameFeaturePlugin = FString::Printf(TEXT("GFP_%02d"), (Id * Id) % 7);
	}

	// Set dependencies (only for visible assets)
	for (int32 AssetIndex = 0; AssetIndex < VisibleAssetCount; ++AssetIndex)
	{
		if (FMath::Rand() % 100 > 60) // 60% chance to have dependencies
		{
			continue;
		}

		FAssetTableRow& Asset = Assets[AssetIndex];

		int32 NumDependents = FMath::Rand() % 30; // max 30 dependent assets
		while (--NumDependents >= 0)
		{
			int32 DepIndex = -1;
			if (FMath::Rand() % 100 <= 10) // 10% chance to be another asset that is visible by default
			{
				DepIndex = FMath::Rand() % VisibleAssetCount;
			}
			else // 90% chance to be a dependet only asset (not visible by default)
			{
				DepIndex = VisibleAssetCount + FMath::Rand() % HiddenAssetCount;
			}
			if (!Asset.Dependencies.Contains(DepIndex))
			{
				Asset.Dependencies.Add(DepIndex);
			}
		}
	}

#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAssetTable::~FAssetTable()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAssetTable::Reset()
{
	//...

	FTable::Reset();

	AddDefaultColumns();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAssetTable::AddDefaultColumns()
{
	using namespace UE::Insights;

	//////////////////////////////////////////////////
	// Hierarchy Column
	{
		const int32 HierarchyColumnIndex = -1;
		const TCHAR* HierarchyColumnName = nullptr;
		AddHierarchyColumn(HierarchyColumnIndex, HierarchyColumnName);

		const TSharedRef<FTableColumn>& ColumnRef = GetColumns()[0];
		ColumnRef->SetInitialWidth(200.0f);
		ColumnRef->SetShortName(LOCTEXT("HierarchyColumnName", "Hierarchy"));
		ColumnRef->SetTitleName(LOCTEXT("HierarchyColumnTitle", "Asset Hierarchy"));
		ColumnRef->SetDescription(LOCTEXT("HierarchyColumnDesc", "Hierarchy of the asset tree"));
	}

	int32 ColumnIndex = 0;

	//////////////////////////////////////////////////
	// Count Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::CountColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("CountColumnName", "Count"));
		Column.SetTitleName(LOCTEXT("CountColumnTitle", "Asset Count"));
		Column.SetDescription(LOCTEXT("CountColumnDesc", "Number of assets"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Right);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FAssetCountValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					//const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					//const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue((int64)1);
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FAssetCountValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsNumber>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);
		Column.SetInitialSortMode(EColumnSortMode::Descending);

		Column.SetAggregation(ETableColumnAggregation::Sum);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Name Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::NameColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("NameColumnName", "Name"));
		Column.SetTitleName(LOCTEXT("NameColumnTitle", "Name"));
		Column.SetDescription(LOCTEXT("NameColumnDesc", "Asset's name"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::CString);

		class FAssetNameValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(Asset.GetName());
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FAssetNameValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FCStringValueFormatterAsText>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::SameValue);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Type Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::TypeColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("TypeColumnName", "Type"));
		Column.SetTitleName(LOCTEXT("TypeColumnTitle", "Type"));
		Column.SetDescription(LOCTEXT("TypeColumnDesc", "Asset's type"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::CString);

		class FAssetTypeValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(Asset.GetType());
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FAssetTypeValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FCStringValueFormatterAsText>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::SameValue);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Path Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::PathColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("PathColumnName", "Path"));
		Column.SetTitleName(LOCTEXT("PathColumnTitle", "Path"));
		Column.SetDescription(LOCTEXT("PathColumnDesc", "Asset's path"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::CString);

		class FAssetPathValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(Asset.GetPath());
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FAssetPathValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FCStringValueFormatterAsText>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::SameValue);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Primary Type Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::PrimaryTypeColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("PrimaryTypeColumnName", "Primary Type"));
		Column.SetTitleName(LOCTEXT("PrimaryTypeColumnTitle", "Primary Type"));
		Column.SetDescription(LOCTEXT("PrimaryTypeColumnDesc", "Primary Asset Type of this asset, if set"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::CString);

		class FAssetPrimaryTypeValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(Asset.GetPrimaryType());
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FAssetPrimaryTypeValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FCStringValueFormatterAsText>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::SameValue);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Primary Name Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::PrimaryNameColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("PrimaryNameColumnName", "Primary Name"));
		Column.SetTitleName(LOCTEXT("PrimaryNameColumnTitle", "Primary Name"));
		Column.SetDescription(LOCTEXT("PrimaryNameColumnDesc", "Primary Asset Name of this asset, if set"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::CString);

		class FAssetPrimaryNameValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(Asset.GetPrimaryName());
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FAssetPrimaryNameValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FCStringValueFormatterAsText>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::SameValue);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Managed Disk Size Column
#if 0
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::ManagedDiskSizeColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("ManagedDiskSizeColumnName", "Disk Size"));
		Column.SetTitleName(LOCTEXT("ManagedDiskSizeColumnTitle", "Disk Size"));
		Column.SetDescription(LOCTEXT("ManagedDiskSizeColumnDesc", "Total disk space used by both this and all managed assets"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Right);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FManagedDiskSizeValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(static_cast<int64>(Asset.GetManagedDiskSize()));
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FManagedDiskSizeValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsMemory>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);
		Column.SetInitialSortMode(EColumnSortMode::Descending);

		//TSharedRef<IFilterValueConverter> Converter = MakeShared<FMemoryFilterValueConverter>();
		//Column.SetValueConverter(Converter);

		Column.SetAggregation(ETableColumnAggregation::Sum);

		AddColumn(ColumnRef);
	}
#endif
	//////////////////////////////////////////////////
	// Exclusive Disk Size Column
#if 0
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::DiskSizeColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("DiskSizeColumnName", "Exclusive Disk Size"));
		Column.SetTitleName(LOCTEXT("DiskSizeColumnTitle", "Exclusive Disk Size"));
		Column.SetDescription(LOCTEXT("DiskSizeColumnDesc", "Size of saved file(s) on disk for this asset's package. If Asset Registry Writeback is enabled and a platform is selected, this will be the uncompressed size of the iostore chunks for the asset's package."));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Right);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FDiskSizeValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(static_cast<int64>(Asset.GetDiskSize()));
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FDiskSizeValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsMemory>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);
		Column.SetInitialSortMode(EColumnSortMode::Descending);

		//TSharedRef<IFilterValueConverter> Converter = MakeShared<FMemoryFilterValueConverter>();
		//Column.SetValueConverter(Converter);

		Column.SetAggregation(ETableColumnAggregation::Sum);

		AddColumn(ColumnRef);
	}
#endif
	//////////////////////////////////////////////////
	// Staged Compressed Size Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::StagedCompressedSizeColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("StagedCompressedSizeColumnName", "Staged Compressed Size"));
		Column.SetTitleName(LOCTEXT("StagedCompressedSizeColumnTitle", "Staged Compressed Size"));
		Column.SetDescription(LOCTEXT("StagedCompressedSizeColumnDesc", "Compressed size of iostore chunks for this asset's package. Only visible after staging."));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Right);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FStagedCompressedSizeValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(static_cast<int64>(Asset.GetStagedCompressedSize()));
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FStagedCompressedSizeValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsMemory>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);
		Column.SetInitialSortMode(EColumnSortMode::Descending);

		//TSharedRef<IFilterValueConverter> Converter = MakeShared<FMemoryFilterValueConverter>();
		//Column.SetValueConverter(Converter);

		Column.SetAggregation(ETableColumnAggregation::Sum);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Total Usage Count Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::TotalUsageCountColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("TotalUsageCountColumnName", "Total Usage"));
		Column.SetTitleName(LOCTEXT("TotalUsageCountColumnTitle", "Total Usage Count"));
		Column.SetDescription(LOCTEXT("TotalUsageCountColumnDesc", "Weighted count of Primary Assets that use this\nA higher usage means it's more likely to be in memory at runtime."));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Right);
		Column.SetInitialWidth(100.0f);

		Column.SetDataType(ETableCellDataType::Int64);

		class FTotalUsageCountValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const override
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(static_cast<int64>(Asset.GetTotalUsageCount()));
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FTotalUsageCountValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FInt64ValueFormatterAsNumber>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByInt64Value>(ColumnRef);
		Column.SetValueSorter(Sorter);
		Column.SetInitialSortMode(EColumnSortMode::Descending);

		Column.SetAggregation(ETableColumnAggregation::Sum);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// Cook Rule Column
#if 0
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::CookRuleColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("CookRuleColumnName", "Cook Rule"));
		Column.SetTitleName(LOCTEXT("CookRuleColumnTitle", "Cook Rule"));
		Column.SetDescription(LOCTEXT("CookRuleColumnDesc", "Whether this asset will be cooked or not"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::CString);

		class FCookRuleValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(Asset.GetChunks());
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FCookRuleValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FCStringValueFormatterAsText>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::SameValue);

		AddColumn(ColumnRef);
	}
#endif
	//////////////////////////////////////////////////
	// Chunks Column
#if 0
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::ChunksColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("ChunksColumnName", "Chunks"));
		Column.SetTitleName(LOCTEXT("ChunksColumnTitle", "Chunks"));
		Column.SetDescription(LOCTEXT("ChunksColumnDesc", "List of chunks this will be added to when cooked"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::CString);

		class FChunksValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(Asset.GetChunks());
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FChunksValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FCStringValueFormatterAsText>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::SameValue);

		AddColumn(ColumnRef);
	}
#endif
	//////////////////////////////////////////////////
	// Native Class Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::NativeClassColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("NativeClassColumnName", "Native Class"));
		Column.SetTitleName(LOCTEXT("NativeClassColumnTitle", "Native Class"));
		Column.SetDescription(LOCTEXT("NativeClassColumnDesc", "Native class of the asset"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::CString);

		class FNativeClassValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(Asset.GetNativeClass());
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FNativeClassValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FCStringValueFormatterAsText>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::SameValue);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
	// GameFeaturePlugin Column
	{
		TSharedRef<FTableColumn> ColumnRef = MakeShared<FTableColumn>(FAssetTableColumns::GameFeaturePluginColumnId);
		FTableColumn& Column = *ColumnRef;

		Column.SetIndex(ColumnIndex++);

		Column.SetShortName(LOCTEXT("GameFeaturePluginColumnName", "GameFeaturePlugin"));
		Column.SetTitleName(LOCTEXT("GameFeaturePluginColumnTitle", "GameFeaturePlugin"));
		Column.SetDescription(LOCTEXT("GameFeaturePluginColumnDesc", "GameFeaturePlugin of the asset"));

		Column.SetFlags(ETableColumnFlags::ShouldBeVisible | ETableColumnFlags::CanBeHidden | ETableColumnFlags::CanBeFiltered);

		Column.SetHorizontalAlignment(HAlign_Left);
		Column.SetInitialWidth(120.0f);

		Column.SetDataType(ETableCellDataType::CString);

		class FNativeClassValueGetter : public FTableCellValueGetter
		{
		public:
			virtual const TOptional<FTableCellValue> GetValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
			{
				if (Node.IsGroup())
				{
					const FTableTreeNode& NodePtr = static_cast<const FTableTreeNode&>(Node);
					if (NodePtr.HasAggregatedValue(Column.GetId()))
					{
						return NodePtr.GetAggregatedValue(Column.GetId());
					}
				}
				else //if (Node->Is<FAssetTreeNode>())
				{
					const FAssetTreeNode& TreeNode = static_cast<const FAssetTreeNode&>(Node);
					const FAssetTableRow& Asset = TreeNode.GetAssetChecked();
					return FTableCellValue(Asset.GetGameFeaturePlugin());
				}

				return TOptional<FTableCellValue>();
			}
		};
		TSharedRef<ITableCellValueGetter> Getter = MakeShared<FNativeClassValueGetter>();
		Column.SetValueGetter(Getter);

		TSharedRef<ITableCellValueFormatter> Formatter = MakeShared<FCStringValueFormatterAsText>();
		Column.SetValueFormatter(Formatter);

		TSharedRef<ITableCellValueSorter> Sorter = MakeShared<FSorterByCStringValue>(ColumnRef);
		Column.SetValueSorter(Sorter);

		Column.SetAggregation(ETableColumnAggregation::SameValue);

		AddColumn(ColumnRef);
	}
	//////////////////////////////////////////////////
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
