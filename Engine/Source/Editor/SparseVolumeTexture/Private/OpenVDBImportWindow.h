// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SCheckBox.h"

#include "SparseVolumeTexture/SparseVolumeTexture.h"

struct FOpenVDBGridInfo;
enum class ESparseVolumePackedDataFormat : uint8;

struct FOpenVDBGridComponentInfo
{
	uint32 Index;
	uint32 ComponentIndex;
	FString Name;
	FString DisplayString; // Contains source file grid index, name and component (if it is a multi component type like Float3)
};

class SOpenVDBGridInfoTableRow : public SMultiColumnTableRow<TSharedPtr<FOpenVDBGridInfo>>
{
public:
	SLATE_BEGIN_ARGS(SOpenVDBGridInfoTableRow) {}
		SLATE_ARGUMENT(TSharedPtr<FOpenVDBGridInfo>, OpenVDBGridInfo)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView);
	TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:
	TSharedPtr<FOpenVDBGridInfo> OpenVDBGridInfo;
};

class SOpenVDBComponentPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenVDBComponentPicker) {}
		SLATE_ARGUMENT(FSparseVolumeRawSourcePackedData*, PackedData)
		SLATE_ARGUMENT(uint32, ComponentIndex)
		SLATE_ARGUMENT(const TArray<TSharedPtr<FOpenVDBGridComponentInfo>>*, OpenVDBGridComponentInfo)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);
	void RefreshUIFromData();

private:
	FSparseVolumeRawSourcePackedData* PackedData;
	uint32 ComponentIndex;
	const TArray<TSharedPtr<FOpenVDBGridComponentInfo>>* OpenVDBGridComponentInfo;
	TSharedPtr<SComboBox<TSharedPtr<FOpenVDBGridComponentInfo>>> GridComboBox;
};

class SOpenVDBPackedDataConfigurator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenVDBPackedDataConfigurator) {}
		SLATE_ARGUMENT(FSparseVolumeRawSourcePackedData*, PackedData)
		SLATE_ARGUMENT(const TArray<TSharedPtr<FOpenVDBGridComponentInfo>>*, OpenVDBGridComponentInfo)
		SLATE_ARGUMENT(const TArray<TSharedPtr<ESparseVolumePackedDataFormat>>*, OpenVDBSupportedTargetFormats)
		SLATE_ARGUMENT(FText, PackedDataName)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);
	void RefreshUIFromData();

private:
	FSparseVolumeRawSourcePackedData* PackedData;
	TSharedPtr<SOpenVDBComponentPicker> ComponentPickers[4];
	const TArray<TSharedPtr<ESparseVolumePackedDataFormat>>* OpenVDBSupportedTargetFormats;
	TSharedPtr<SComboBox<TSharedPtr<ESparseVolumePackedDataFormat>>> FormatComboBox;
	TSharedPtr<SCheckBox> RemapUnormCheckBox;
};

class SOpenVDBImportWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenVDBImportWindow) {}
		SLATE_ARGUMENT(FSparseVolumeRawSourcePackedData*, PackedDataA)
		SLATE_ARGUMENT(FSparseVolumeRawSourcePackedData*, PackedDataB)
		SLATE_ARGUMENT(int32, NumFoundFiles)
		SLATE_ARGUMENT(const TArray<TSharedPtr<FOpenVDBGridInfo>>*, OpenVDBGridInfo)
		SLATE_ARGUMENT(const TArray<TSharedPtr<FOpenVDBGridComponentInfo>>*, OpenVDBGridComponentInfo)
		SLATE_ARGUMENT(const TArray<TSharedPtr<ESparseVolumePackedDataFormat>>*, OpenVDBSupportedTargetFormats)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
		SLATE_ARGUMENT(FText, FullPath)
		SLATE_ARGUMENT(float, MaxWindowHeight)
		SLATE_ARGUMENT(float, MaxWindowWidth)
	SLATE_END_ARGS()

public:
	virtual bool SupportsKeyboardFocus() const override { return true; }
	void Construct(const FArguments& InArgs);
	FReply OnImport();
	FReply OnCancel();
	bool ShouldImport() const;
	bool ShouldImportAsSequence() const;

private:
	FSparseVolumeRawSourcePackedData* PackedDataA;
	FSparseVolumeRawSourcePackedData* PackedDataB;
	FSparseVolumeRawSourcePackedData DefaultAssignmentA;
	FSparseVolumeRawSourcePackedData DefaultAssignmentB;
	bool bIsSequence;
	const TArray<TSharedPtr<FOpenVDBGridInfo>>* OpenVDBGridInfo;
	const TArray<TSharedPtr<FOpenVDBGridComponentInfo>>* OpenVDBGridComponentInfo;
	const TArray<TSharedPtr<ESparseVolumePackedDataFormat>>* OpenVDBSupportedTargetFormats;
	TSharedPtr<SOpenVDBPackedDataConfigurator> PackedDataAConfigurator;
	TSharedPtr<SOpenVDBPackedDataConfigurator> PackedDataBConfigurator;
	TSharedPtr<SCheckBox> ImportAsSequenceCheckBox;
	TSharedPtr<SButton> ImportButton;
	TWeakPtr<SWindow> WidgetWindow;
	bool bShouldImport;

	EActiveTimerReturnType SetFocusPostConstruct(double InCurrentTime, float InDeltaTime);
	TSharedRef<ITableRow> GenerateGridInfoItemRow(TSharedPtr<FOpenVDBGridInfo> Item, const TSharedRef<STableViewBase>& OwnerTable);
	bool CanImport() const;
	FReply OnResetToDefaultClick();
	FText GetImportTypeDisplayText() const;
	void SetDefaultGridAssignment();
};