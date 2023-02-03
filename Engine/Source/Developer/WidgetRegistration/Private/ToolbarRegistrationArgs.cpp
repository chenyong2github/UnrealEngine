// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ToolbarRegistrationArgs.h"


FToolbarRegistrationArgs::FToolbarRegistrationArgs(TSharedRef<FToolBarBuilder> InToolBarBuilder) :
		FToolElementRegistrationArgs(EToolElement::Toolbar),
		ToolBarBuilder(InToolBarBuilder)
{
		
}

TSharedPtr<SWidget> FToolbarRegistrationArgs::GenerateWidget()
{
	return ToolBarBuilder->MakeWidget();
}