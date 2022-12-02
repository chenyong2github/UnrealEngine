// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SCheckBox.h"

struct FOpenVDBGridInfo;
struct ENGINE_API FSparseVolumeRawSourcePackedData;
enum class ESparseVolumePackedDataFormat : uint8;

class SOpenVDBComponentPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenVDBComponentPicker)
		: _PackedData()
		, _ComponentIndex()
		, _OpenVDBGridInfo()
	{}

		SLATE_ARGUMENT(FSparseVolumeRawSourcePackedData*, PackedData)
		SLATE_ARGUMENT(uint32, ComponentIndex)
		SLATE_ARGUMENT(TArray<TSharedPtr<FOpenVDBGridInfo>>*, OpenVDBGridInfo)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);
	void RefreshUIFromData();

private:
	FSparseVolumeRawSourcePackedData* PackedData;
	uint32 ComponentIndex;
	TArray<TSharedPtr<FOpenVDBGridInfo>>* OpenVDBGridInfo;
	TSharedPtr<SComboBox<TSharedPtr<FOpenVDBGridInfo>>>	GridComboBox;
};

class SOpenVDBPackedDataConfigurator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenVDBPackedDataConfigurator)
		: _PackedData()
		, _OpenVDBGridInfo()
		, _OpenVDBSupportedTargetFormats()
		, _PackedDataName()
	{}

		SLATE_ARGUMENT(FSparseVolumeRawSourcePackedData*, PackedData)
		SLATE_ARGUMENT(TArray<TSharedPtr<FOpenVDBGridInfo>>*, OpenVDBGridInfo)
		SLATE_ARGUMENT(TArray<TSharedPtr<ESparseVolumePackedDataFormat>>*, OpenVDBSupportedTargetFormats)
		SLATE_ARGUMENT(FText, PackedDataName)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);
	void RefreshUIFromData();

private:
	FSparseVolumeRawSourcePackedData* PackedData;
	TSharedPtr<SOpenVDBComponentPicker> ComponentPickers[4];
	TArray<TSharedPtr<ESparseVolumePackedDataFormat>>* OpenVDBSupportedTargetFormats;
	TSharedPtr<SComboBox<TSharedPtr<ESparseVolumePackedDataFormat>>>	FormatComboBox;
	TSharedPtr<SCheckBox> RescaleUnormCheckBox;
};

class SOpenVDBImportWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenVDBImportWindow)
		: _PackedDataA()
		, _OpenVDBGridInfo()
		, _OpenVDBSupportedTargetFormats()
		, _WidgetWindow()
		, _FullPath()
		, _MaxWindowHeight(0.0f)
		, _MaxWindowWidth(0.0f)
	{}

		SLATE_ARGUMENT(FSparseVolumeRawSourcePackedData*, PackedDataA)
		SLATE_ARGUMENT(TArray<TSharedPtr<FOpenVDBGridInfo>>*, OpenVDBGridInfo)
		SLATE_ARGUMENT(TArray<TSharedPtr<ESparseVolumePackedDataFormat>>*, OpenVDBSupportedTargetFormats)
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

private:
	FSparseVolumeRawSourcePackedData*					PackedDataA;
	TArray<TSharedPtr<FOpenVDBGridInfo>>*				OpenVDBGridInfo;
	TArray<TSharedPtr<ESparseVolumePackedDataFormat>>*	OpenVDBSupportedTargetFormats;
	TSharedPtr<SOpenVDBPackedDataConfigurator>			PackedDataAConfigurator;
	TSharedPtr<SButton>									ImportButton;
	TWeakPtr<SWindow>									WidgetWindow;
	bool												bShouldImport;

	EActiveTimerReturnType SetFocusPostConstruct(double InCurrentTime, float InDeltaTime);
	bool CanImport()  const;
	FReply OnResetToDefaultClick();
	FText GetImportTypeDisplayText() const;
	void SetDefaultGridAssignment();
};