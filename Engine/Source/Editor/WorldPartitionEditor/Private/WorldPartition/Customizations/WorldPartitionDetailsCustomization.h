// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "IDetailCustomization.h"
#include "WorldPartition/WorldPartition.h"

class IDetailLayoutBuilder;

enum class ECheckBoxState : uint8;

class FWorldPartitionDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	FWorldPartitionDetails()
	{}

private:
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	// Callback for changes in the world partition enable streaming.
	void HandleWorldPartitionEnableStreamingChanged(ECheckBoxState CheckState);

	// Callback for changes in the world partition editor cell size.
	void HandleWorldPartitionEditorCellSizeChanged(uint32 NewValue);

	// Callback for getting the world partition editor cell size.
	TOptional<uint32> HandleWorldPartitionEditorCellSizeValue() const;

	TWeakObjectPtr<UWorldPartition> WorldPartition;
};