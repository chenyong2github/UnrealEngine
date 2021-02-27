// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "IDetailCustomization.h"
#include "Features/IPluginsEditorFeature.h"

class IDetailLayoutBuilder;
struct FPluginEditingContext;
class IPlugin;
struct FPluginDescriptor;
enum class EGameFeaturePluginState : uint8;

//////////////////////////////////////////////////////////////////////////
// FGameFeaturePluginMetadataCustomization

class FGameFeaturePluginMetadataCustomization : public FPluginEditorExtension
{
public:
	void CustomizeDetails(FPluginEditingContext& InPluginContext, IDetailLayoutBuilder& DetailBuilder);

	virtual void CommitEdits(FPluginDescriptor& Descriptor) override;
private:
	EGameFeaturePluginState GetDefaultState() const;

	void ChangeDefaultState(EGameFeaturePluginState DesiredState);

	TSharedPtr<IPlugin> Plugin;

	EGameFeaturePluginState InitialState;
};
