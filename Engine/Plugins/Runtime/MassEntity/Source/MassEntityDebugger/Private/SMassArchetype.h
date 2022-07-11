// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Widgets/SCompoundWidget.h"


struct FMassDebuggerArchetypeData;

class SMassArchetype : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMassArchetype){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FMassDebuggerArchetypeData> InArchetypeData);

protected:
	TSharedPtr<FMassDebuggerArchetypeData> ArchetypeData;
};
