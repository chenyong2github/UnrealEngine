// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "ISourceControlOperation.h"
#include "ISourceControlProvider.h"

class FUICommandList;
class SWidget;

class FSourceControlCommands : public TCommands<FSourceControlCommands>
{
public:
	FSourceControlCommands();

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;

private:

	static void ConnectToSourceControl_Clicked();
	static bool ViewChangelists_CanExecute();
	static void ViewChangelists_Clicked();
	static bool CheckOutModifiedFiles_CanExecute();
	static void CheckOutModifiedFiles_Clicked();

public:
	/**
	 * Source Control Commands
	 */
	TSharedPtr< FUICommandInfo > ConnectToSourceControl;
	TSharedPtr< FUICommandInfo > ChangeSourceControlSettings;
	TSharedPtr< FUICommandInfo > ViewChangelists;
	TSharedPtr< FUICommandInfo > SubmitContent;
	TSharedPtr< FUICommandInfo > CheckOutModifiedFiles;

	static TSharedRef<FUICommandList> ActionList;
};


struct FSourceControlMenuHelpers
{
	enum EQueryState
	{
		NotQueried,
		Querying,
		Queried,
	};


	static EQueryState QueryState;

	static void CheckSourceControlStatus();
	static void OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult);
	static TSharedRef<SWidget> GenerateSourceControlMenuContent();
	static FText GetSourceControlStatusText();
	static FText GetSourceControlTooltip();
	static const FSlateBrush* GetSourceControlIcon();
	static TSharedRef<SWidget> MakeSourceControlStatusWidget();
};

