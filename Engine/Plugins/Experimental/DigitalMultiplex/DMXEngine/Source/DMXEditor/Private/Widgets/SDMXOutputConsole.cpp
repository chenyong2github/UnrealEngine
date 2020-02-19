// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXOutputConsole.h"
#include "Widgets/OutputFader/SDMXOutputFaderList.h"
#include "Widgets/SDMXEntityInspector.h"

#include "DMXEditor.h"
#include "DMXEditorLog.h"
#include "Library/DMXEntityFader.h"
#include "DMXEditorUtils.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"

void SDMXOutputConsole::Construct(const FArguments& InArgs)
{
	DMXEditorPtr = InArgs._DMXEditor;
	check(DMXEditorPtr.Pin().IsValid());

	TSharedPtr<SDMXEntityInspectorFaders> Inspector = SNew(SDMXEntityInspectorFaders)
		.DMXEditor(DMXEditorPtr);

	// GC can't delete this object
	OutputConsoleFaderTemplateGuard = TStrongObjectPtr<UDMXEntityFader>(FDMXEditorUtils::CreateFaderTemplate(DMXEditorPtr.Pin()->GetDMXLibrary()));
	Inspector->ShowDetailsForSingleEntity(OutputConsoleFaderTemplateGuard.Get());

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(350)
		[
			SNew(SScrollBox)
			.Orientation(EOrientation::Orient_Vertical)
			+ SScrollBox::Slot()
			[
				Inspector.ToSharedRef()
			]
		]
		+SVerticalBox::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			SNew(SDMXOutputFaderList)
			.DMXEditor(DMXEditorPtr)
			.FaderTemplate(OutputConsoleFaderTemplateGuard.Get())
		]
	];
}

SDMXOutputConsole::~SDMXOutputConsole()
{
	OutputConsoleFaderTemplateGuard.Reset(); // GC can now delete this object
}
