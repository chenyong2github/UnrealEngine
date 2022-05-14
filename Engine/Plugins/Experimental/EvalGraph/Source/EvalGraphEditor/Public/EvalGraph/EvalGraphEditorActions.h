// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FEvalGraphEditorCommandsImpl : public TCommands<FEvalGraphEditorCommandsImpl>
{
public:

	FEvalGraphEditorCommandsImpl()
		: TCommands<FEvalGraphEditorCommandsImpl>( TEXT("EvalGraphEditor"), NSLOCTEXT("Contexts", "EvalGraphEditor", "Scene Graph Editor"), NAME_None, FAppStyle::GetAppStyleSetName() )
	{
	}	

	virtual ~FEvalGraphEditorCommandsImpl()
	{
	}

	EVALGRAPHEDITOR_API virtual void RegisterCommands() override;

	TSharedPtr< FUICommandInfo > EvaluateNode;
	TMap< FName, TSharedPtr<FUICommandInfo> > CreateNodesMap;
};

class EVALGRAPHEDITOR_API FEvalGraphEditorCommands
{
public:
	static void Register();

	static const FEvalGraphEditorCommandsImpl& Get();

	static void Unregister();
};
