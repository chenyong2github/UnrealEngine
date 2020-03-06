// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/StrongObjectPtr.h"

class FDMXEditor;
class UDMXEntityFader;

/**
 * Widget for the Output Console tab, to configure output faders 
 */
class SDMXOutputConsole
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXOutputConsole)
		: _DMXEditor(nullptr)
	{}
		
	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)
	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	~SDMXOutputConsole();

private:
	/** The fader used in the output console to display fader properties */
	TStrongObjectPtr<UDMXEntityFader> OutputConsoleFaderTemplateGuard;

	/** Weak reference to DMX editor */
	TWeakPtr<FDMXEditor> DMXEditorPtr;
};
