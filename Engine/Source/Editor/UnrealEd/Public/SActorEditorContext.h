// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class UNREALED_API SActorEditorContext : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SActorEditorContext) {}
	SLATE_ARGUMENT(UWorld*, World)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~SActorEditorContext();

private:

	void OnEditorMapChange(uint32 MapChangeFlags = 0) { Rebuild(); }
	void Rebuild();
	
	UWorld* World;
};