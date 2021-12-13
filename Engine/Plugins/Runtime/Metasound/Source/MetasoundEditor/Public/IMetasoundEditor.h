// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Layout/SlateRect.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/NoExportTypes.h"
#include "UObject/Object.h"


class IMetasoundEditor : public FAssetEditorToolkit
{
	virtual UObject* GetMetasoundObject() const = 0;
	virtual void SetSelection(const TArray<UObject*>& SelectedObjects) = 0;
	virtual bool GetBoundsForSelectedNodes(FSlateRect& Rect, float Padding) = 0;
	virtual void Play() = 0;
	virtual void Stop() = 0;
};
