// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Layout/SlateRect.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/Object.h"


class IMetasoundEditor : public FAssetEditorToolkit
{
	virtual UObject* GetMetasoundObject() const = 0;
	virtual UObject* GetMetasoundAudioBusObject() const = 0;
	virtual void SetSelection(const TArray<UObject*>& SelectedObjects) = 0;
	virtual bool GetBoundsForSelectedNodes(FSlateRect& Rect, float Padding) = 0;
};
