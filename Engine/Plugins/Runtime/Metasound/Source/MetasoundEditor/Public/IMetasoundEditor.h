// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Metasound.h"
#include "UObject/Object.h"


class IMetasoundEditor : public FAssetEditorToolkit
{
	virtual UMetasound* GetMetasound() const = 0;
	virtual void SetSelection(TArray<UObject*> SelectedObjects) = 0;
	virtual bool GetBoundsForSelectedNodes(FSlateRect& Rect, float Padding) = 0;
	virtual int32 GetNumberOfSelectedNodes() const = 0;
	virtual TSet<UObject*> GetSelectedNodes() const = 0;
};
