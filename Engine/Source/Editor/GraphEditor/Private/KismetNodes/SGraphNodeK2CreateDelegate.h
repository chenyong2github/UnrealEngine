// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "KismetNodes/SGraphNodeK2Base.h"

class ITableRow;
class SComboButton;
class STableViewBase;
class SVerticalBox;
class UK2Node;

class SGraphNodeK2CreateDelegate : public SGraphNodeK2Base
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeK2CreateDelegate) {}
	SLATE_END_ARGS()

	/** Data that defines a delegate function */
	struct FFunctionItemData
	{
		FName Name;
		FText Description;
	};

	/** Collection of function items that have matching function delegates to this node */
	TArray<TSharedPtr<FFunctionItemData>> FunctionDataItems;
	TWeakPtr<SComboButton> SelectFunctionWidget;

	/** Data that can be used to create a matching function based on the parameters of a create event node */
	TSharedPtr<FFunctionItemData> CreateMatchingFunctionData;
	
	/** Data that can be used to create a matching event based on based on the parameters of a create event node */
	TSharedPtr<FFunctionItemData> CreateMatchingEventData;

public:
	virtual ~SGraphNodeK2CreateDelegate();
	void Construct(const FArguments& InArgs, UK2Node* InNode);
	virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override;

protected:
	static FText FunctionDescription(const UFunction* Function, const bool bOnlyDescribeSignature = false, const int32 CharacterLimit = 32);

	FText GetCurrentFunctionDescription() const;
	TSharedRef<ITableRow> HandleGenerateRowFunction(TSharedPtr<FFunctionItemData> FunctionItemData, const TSharedRef<STableViewBase>& OwnerTable);
	void OnFunctionSelected(TSharedPtr<FFunctionItemData> FunctionItemData, ESelectInfo::Type SelectInfo);

private:

	/**
	* Adds a FunctionItemData with a given description to the array of FunctionDataItems. 
	* 
	* @param	DescriptionName		Description of the option to give the user
	* @return	Shared pointer to the FunctionItemData
	*/
	TSharedPtr<SGraphNodeK2CreateDelegate::FFunctionItemData> AddDefaultFunctionDataOption(const FText& DescriptionName);
};

