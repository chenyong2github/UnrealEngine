// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FDataflowEditorCommandsImpl : public TCommands<FDataflowEditorCommandsImpl>
{
public:

	FDataflowEditorCommandsImpl()
		: TCommands<FDataflowEditorCommandsImpl>( TEXT("DataflowEditor"), NSLOCTEXT("Contexts", "DataflowEditor", "Scene Graph Editor"), NAME_None, FAppStyle::GetAppStyleSetName() )
	{
	}	

	virtual ~FDataflowEditorCommandsImpl()
	{
	}

	DATAFLOWEDITOR_API virtual void RegisterCommands() override;

	TSharedPtr< FUICommandInfo > EvaluateNode;
	TMap< FName, TSharedPtr<FUICommandInfo> > CreateNodesMap;
};

class DATAFLOWEDITOR_API FDataflowEditorCommands
{
public:
	static void Register();

	static const FDataflowEditorCommandsImpl& Get();

	static void Unregister();
};
