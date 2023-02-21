// Copyright Epic Games, Inc. All Rights Reserved.
#include "StructView.h"
#include "SharedStruct.h"

///////////////////////////////////////////////////////////////// FStructView /////////////////////////////////////////////////////////////////

FStructView::FStructView(FSharedStruct& SharedStruct)
	: FStructView(SharedStruct.GetScriptStruct(), SharedStruct.GetMemory())
{}

///////////////////////////////////////////////////////////////// FConstStructView /////////////////////////////////////////////////////////////////

FConstStructView::FConstStructView(const FSharedStruct SharedStruct)
	: FConstStructView(SharedStruct.GetScriptStruct(), SharedStruct.GetMemory())
{}

FConstStructView::FConstStructView(const FConstSharedStruct SharedStruct)
	: FConstStructView(SharedStruct.GetScriptStruct(), SharedStruct.GetMemory())
{}