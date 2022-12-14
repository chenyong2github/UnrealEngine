// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorSettings.h"
#include "PCGEditorCommon.h"

#include "EdGraph/EdGraphPin.h"

UPCGEditorSettings::UPCGEditorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultNodeColor = FLinearColor(0.4f, 0.62f, 1.0f);
	InstancedNodeBodyTintColor = FLinearColor(0.5f, 0.5f, 0.5f);
	InputOutputNodeColor = FLinearColor(1.0f, 0.0f, 0.0f);
	SetOperationNodeColor = FLinearColor(0.8f, 0.2f, 0.8f);
	DensityOperationNodeColor = FLinearColor(0.6f, 1.0f, 0.6f);
	BlueprintNodeColor = FLinearColor(0.0f, 0.6f, 1.0f);
	MetadataNodeColor = FLinearColor(0.4f, 0.4f, 0.8f);
	FilterNodeColor = FLinearColor(0.4f, 0.8f, 0.4f);
	SamplerNodeColor = FLinearColor(0.8f, 1.0f, 0.4f);
	SpawnerNodeColor = FLinearColor(1.0f, 0.6f, 0.4f);
	SubgraphNodeColor = FLinearColor(1.0f, 0.1f, 0.1f);
	ParamDataNodeColor = FLinearColor(1.0f, 0.6f, 0.0f);
	DebugNodeColor = FLinearColor(1.0f, 0.0f, 1.0f);

	DefaultPinColor = FLinearColor(1.0f, 1.0f, 1.0f);

	SpatialDataPinColor = FLinearColor(0.2f, 0.2f, 1.0f);
	CompositeDataPinColor = FLinearColor(0.2f, 0.2f, 1.0f);
	ConcreteDataPinColor = FLinearColor(0.2f, 0.2f, 1.0f);
	PointDataPinColor = FLinearColor(0.2f, 0.2f, 1.0f);
	PolyLineDataPinColor = FLinearColor(0.2f, 0.2f, 1.0f);
	LandscapeDataPinColor = FLinearColor(0.2f, 0.2f, 1.0f);
	TextureDataPinColor = FLinearColor(0.2f, 0.2f, 1.0f);
	RenderTargetDataPinColor = FLinearColor(1.0f, 0.3f, 0.f);
	SurfaceDataPinColor = FLinearColor(0.2f, 0.2f, 1.0f);
	VolumeDataPinColor = FLinearColor(0.2f, 0.2f, 1.0f);
	PrimitiveDataPinColor = FLinearColor(0.2f, 0.2f, 1.0f);

	ParamDataPinColor = FLinearColor(1.0f, 0.6f, 0.0f);
	UnknownDataPinColor = FLinearColor(0.3f, 0.3f, 0.3f);

	bEnableNavigateToNativeNodes = true;
}

FLinearColor UPCGEditorSettings::GetColor(UPCGSettings* Settings) const
{
	if (!Settings)
	{
		return DefaultNodeColor;
	}
	// First: check if there's an override
	else if (const FLinearColor* Override = OverrideNodeColorByClass.Find(Settings->GetClass()))
	{
		return *Override;
	}
	// Otherwise, check against the classes we know
	else if(Settings->GetType() == EPCGSettingsType::InputOutput)
	{
		return InputOutputNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Spatial)
	{
		return SetOperationNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Density)
	{
		return DensityOperationNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Blueprint)
	{
		return BlueprintNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Metadata)
	{
		return MetadataNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Filter)
	{
		return FilterNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Sampler)
	{
		return SamplerNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Spawner)
	{
		return SpawnerNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Subgraph)
	{
		return SubgraphNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Debug)
	{
		return DebugNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Param)
	{
		return ParamDataNodeColor;
	}
	else
	{
		// Finally, we couldn't find any match, so return the default value
		return DefaultNodeColor;
	}
}

FLinearColor UPCGEditorSettings::GetPinColor(const FEdGraphPinType& PinType) const
{
	if (PinType.PinCategory == FPCGEditorCommon::ConcreteDataType)
	{
		// Clauses below try to pick the narrowest type possible, falling back to Spatial
		if (PinType.PinSubCategory == FPCGEditorCommon::PointDataType)
		{
			return PointDataPinColor;
		}
		else if (PinType.PinSubCategory == FPCGEditorCommon::PolyLineDataType)
		{
			return PolyLineDataPinColor;
		}
		else if (PinType.PinSubCategory == FPCGEditorCommon::LandscapeDataType)
		{
			return LandscapeDataPinColor;
		}
		else if (PinType.PinSubCategory == FPCGEditorCommon::TextureDataType)
		{
			return TextureDataPinColor;
		}
		else if (PinType.PinSubCategory == FPCGEditorCommon::RenderTargetDataType)
		{
			return RenderTargetDataPinColor;
		}
		else if (PinType.PinSubCategory == FPCGEditorCommon::SurfaceDataType)
		{
			return SurfaceDataPinColor;
		}
		else if (PinType.PinSubCategory == FPCGEditorCommon::VolumeDataType)
		{
			return VolumeDataPinColor;
		}
		else if (PinType.PinSubCategory == FPCGEditorCommon::PrimitiveDataType)
		{
			return PrimitiveDataPinColor;
		}
		else
		{
			return ConcreteDataPinColor;
		}
	}
	else if (PinType.PinCategory == FPCGEditorCommon::CompositeDataType)
	{
		return CompositeDataPinColor;
	}
	else if (PinType.PinCategory == FPCGEditorCommon::SpatialDataType)
	{
		return SpatialDataPinColor;
	}
	else if (PinType.PinCategory == FPCGEditorCommon::ParamDataType)
	{
		return ParamDataPinColor;
	}
	else if (PinType.PinCategory == FPCGEditorCommon::OtherDataType)
	{
		return UnknownDataPinColor;
	}
	else
	{
		return DefaultPinColor;
	}
}
