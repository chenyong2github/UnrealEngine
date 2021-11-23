// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SCompoundWidget.h"

struct FConsoleVariablesEditorListRow;

class SConsoleVariablesEditorListValueInput : public SCompoundWidget
{
public:
	virtual ~SConsoleVariablesEditorListValueInput() override;

	static TSharedRef<SConsoleVariablesEditorListValueInput> GetInputWidget(const TWeakPtr<FConsoleVariablesEditorListRow> InRow);
	
	virtual void SetInputValue(const FString& InValueAsString) = 0;
	virtual FString GetInputValueAsString() = 0;
	
	[[nodiscard]] const FString& GetCachedValue() const;
	void SetCachedValue(const FString& CachedValue);

	bool IsRowChecked() const;

protected:
	
	TWeakPtr<FConsoleVariablesEditorListRow> Item = nullptr;
	FString CachedValue = "";
};

class SConsoleVariablesEditorListValueInput_Float : public SConsoleVariablesEditorListValueInput
{
public:
	
	SLATE_BEGIN_ARGS(SConsoleVariablesEditorListValueInput_Float)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TWeakPtr<FConsoleVariablesEditorListRow> InRow);
	
	virtual void SetInputValue(const FString& InValueAsString) override;
	virtual FString GetInputValueAsString() override;

	float GetInputValue() const;

private:
	
	TSharedPtr<SSpinBox<float>> InputWidget;
};

class SConsoleVariablesEditorListValueInput_Int : public SConsoleVariablesEditorListValueInput
{
public:
	
	SLATE_BEGIN_ARGS(SConsoleVariablesEditorListValueInput_Int)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TWeakPtr<FConsoleVariablesEditorListRow> InRow);
	
	virtual void SetInputValue(const FString& InValueAsString) override;
	virtual FString GetInputValueAsString() override;

	int32 GetInputValue() const;

private:
	
	TSharedPtr<SSpinBox<int32>> InputWidget;
};

class SConsoleVariablesEditorListValueInput_String : public SConsoleVariablesEditorListValueInput
{
public:
	
	SLATE_BEGIN_ARGS(SConsoleVariablesEditorListValueInput_String)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TWeakPtr<FConsoleVariablesEditorListRow> InRow);
	
	virtual void SetInputValue(const FString& InValueAsString) override;
	virtual FString GetInputValueAsString() override;

	FString GetInputValue() const;

private:
	
	TSharedPtr<SEditableText> InputWidget;
};

class SConsoleVariablesEditorListValueInput_Bool : public SConsoleVariablesEditorListValueInput
{
public:
	
	SLATE_BEGIN_ARGS(SConsoleVariablesEditorListValueInput_Bool)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TWeakPtr<FConsoleVariablesEditorListRow> InRow);
	
	virtual void SetInputValue(const FString& InValueAsString) override;
	virtual FString GetInputValueAsString() override;

	int32 GetInputValue() const;
	bool GetInputValueAsBool() const;

	FString GetBoolValueAsString() const;

private:
	
	TSharedPtr<SSpinBox<int32>> InputWidget;
};
