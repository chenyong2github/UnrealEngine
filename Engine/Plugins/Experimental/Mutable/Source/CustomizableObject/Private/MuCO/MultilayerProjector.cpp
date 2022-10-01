// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/MultilayerProjector.h"

#include "MuCO/CustomizableObjectInstance.h"


void FMultilayerProjectorLayer::Read(const FMultilayerProjector& MultilayerProjector, const int32 Index)
{
	checkCode(MultilayerProjector.CheckInstanceParameters());
	check(Index >= 0 && Index < MultilayerProjector.NumLayers()); // Layer out of range.
	
	const UCustomizableObjectInstance& Instance = *MultilayerProjector.Instance;
	const FString ParamName = MultilayerProjector.ParamName.ToString();
	
	{
		const int32 ProjectorParamIndex = Instance.FindProjectorParameterNameIndex(ParamName);
		const FCustomizableObjectProjector& Projector = Instance.GetProjectorParameters()[ProjectorParamIndex].RangeValues[Index];
		Position = static_cast<FVector3d>(Projector.Position);
		Direction = static_cast<FVector3d>(Projector.Direction);
		Up = static_cast<FVector3d>(Projector.Up);
		Scale = static_cast<FVector3d>(Projector.Scale);
		Angle = Projector.Angle;
	}
	
	{
		const int32 ImageParamIndex = Instance.FindIntParameterNameIndex(ParamName + FMultilayerProjector::IMAGE_PARAMETER_POSTFIX);
		Image = Instance.GetIntParameters()[ImageParamIndex].ParameterRangeValueNames[Index];
	}

	{
		const int32 OpacityParamIndex = Instance.FindFloatParameterNameIndex(ParamName + FMultilayerProjector::OPACITY_PARAMETER_POSTFIX);
		Opacity = Instance.GetFloatParameters()[OpacityParamIndex].ParameterRangeValues[Index];
	}
}


void FMultilayerProjectorLayer::Write(const FMultilayerProjector& MultilayerProjector, const int32 Index) const
{
	checkCode(MultilayerProjector.CheckInstanceParameters());
	check(Index >= 0 && Index < MultilayerProjector.NumLayers()); // Layer out of range.

	UCustomizableObjectInstance& Instance = *MultilayerProjector.Instance;
	const FString ParamName = MultilayerProjector.ParamName.ToString();

	{
		const int32 ProjectorParamIndex = Instance.FindProjectorParameterNameIndex(ParamName);
		FCustomizableObjectProjector& Projector = Instance.GetProjectorParameters()[ProjectorParamIndex].RangeValues[Index];
		Projector.Position = static_cast<FVector3f>(Position);
		Projector.Direction = static_cast<FVector3f>(Direction);
		Projector.Up = static_cast<FVector3f>(Up);
		Projector.Scale = static_cast<FVector3f>(Scale);
		Projector.Angle = Angle;
	}
	
	{
		const int32 ImageParamIndex = Instance.FindIntParameterNameIndex(ParamName + FMultilayerProjector::IMAGE_PARAMETER_POSTFIX);
		Instance.GetIntParameters()[ImageParamIndex].ParameterRangeValueNames[Index] = Image;
	}

	{
		const int32 OpacityParamIndex = Instance.FindFloatParameterNameIndex(ParamName + FMultilayerProjector::OPACITY_PARAMETER_POSTFIX);
		Instance.GetFloatParameters()[OpacityParamIndex].ParameterRangeValues[Index] = Opacity;
	}
}


FMultilayerProjectorVirtualLayer::FMultilayerProjectorVirtualLayer(const FMultilayerProjectorLayer& Layer, const bool bEnabled, const int32 Order):
	FMultilayerProjectorLayer(Layer),
	bEnabled(bEnabled),
	Order(Order)
{
}


const FString FMultilayerProjector::NUM_LAYERS_PARAMETER_POSTFIX = FString("_NumLayers");

const FString FMultilayerProjector::OPACITY_PARAMETER_POSTFIX = FString("_Opacity");

const FString FMultilayerProjector::IMAGE_PARAMETER_POSTFIX = FString("_SelectedImages");

const FString FMultilayerProjector::POSE_PARAMETER_POSTFIX = FString("_SelectedPoses");


int32 FMultilayerProjector::NumLayers() const
{
	check(Instance); // Multilayer Projector created without using the Instance factory.

	const FString NumLayersParamName = ParamName.ToString() + NUM_LAYERS_PARAMETER_POSTFIX;
	
	const int32 FloatParameterIndex = Instance->FindFloatParameterNameIndex(NumLayersParamName);
	check(FloatParameterIndex != -1); // Parameter not found.

	return Instance->GetFloatParameters()[FloatParameterIndex].ParameterValue;
}


void FMultilayerProjector::CreateLayer(const int32 Index) const
{
	check(Instance); // Multilayer Projector created without using the Instance factory.
	checkCode(CheckInstanceParameters());
	check(Index >= 0 && Index <= NumLayers()); // Layer is non-contiguous or out of range.

	const UCustomizableObject* Object = Instance->GetCustomizableObject(); 
	
	// Num Layers.
	{
        const FString NumLayersParamName = ParamName.ToString() + NUM_LAYERS_PARAMETER_POSTFIX;
		const int32 FloatParameterIndex = Instance->FindFloatParameterNameIndex(NumLayersParamName);

        Instance->GetFloatParameters()[FloatParameterIndex].ParameterValue += 1;
    }
	// Projector Range.
	{
		const int32 ProjectorParameterIndex = Instance->FindProjectorParameterNameIndex(ParamName.ToString());
		
		FCustomizableObjectProjectorParameterValue& ProjectorParameter = Instance->GetProjectorParameters()[ProjectorParameterIndex];
		const FCustomizableObjectProjector Projector = Instance->GetProjectorDefaultValue(Object->FindParameter(ParamName.ToString()));
		ProjectorParameter.RangeValues.Insert(Projector, Index);
	}
	
	// Selected Image Range.
	{
		const int32 IntParameterIndex = Instance->FindIntParameterNameIndex(ParamName.ToString() + FMultilayerProjector::IMAGE_PARAMETER_POSTFIX);

		FCustomizableObjectIntParameterValue& IntParameter = Instance->GetIntParameters()[IntParameterIndex];
		const int32 ParamIndexInObject = Object->FindParameter(IntParameter.ParameterName);

		const FString DefaultValue = Object->GetIntParameterAvailableOption(ParamIndexInObject, 0); // TODO: Define the default option in the editor instead of taking the first available, like it's currently defined for GetProjectorDefaultValue()
		IntParameter.ParameterRangeValueNames.Insert(DefaultValue, Index);
	}
	
	// Opacity Range.
	{
		const int32 FloatParameterIndex = Instance->FindFloatParameterNameIndex(ParamName.ToString() + FMultilayerProjector::OPACITY_PARAMETER_POSTFIX);

		FCustomizableObjectFloatParameterValue& FloatParameter = Instance->GetFloatParameters()[FloatParameterIndex];
		FloatParameter.ParameterRangeValues.Insert(0.5, Index); // TODO: Define the default float in the editor instead of [0.5f], like it's currently defined for GetProjectorDefaultValue()
	}
}


void FMultilayerProjector::RemoveLayerAt(const int32 Index) const
{
	check(Instance); // Multilayer Projector created without using the Instance factory.
	checkCode(CheckInstanceParameters());
	check(Index >= 0 && Index < NumLayers()); // Layer out of range.
	
	// Num Layers.
	{
		const FString NumLayersParamName = ParamName.ToString() + NUM_LAYERS_PARAMETER_POSTFIX;
        const int32 FloatParameterIndex = Instance->FindFloatParameterNameIndex(NumLayersParamName);
		
    	Instance->GetFloatParameters()[FloatParameterIndex].ParameterValue -= 1;
    }
    
	// Projector Range.
	{
		const int32 ProjectorParameterIndex = Instance->FindProjectorParameterNameIndex(ParamName.ToString());
		
		FCustomizableObjectProjectorParameterValue& ProjectorParameter = Instance->GetProjectorParameters()[ProjectorParameterIndex];
		ProjectorParameter.RangeValues.RemoveAt(Index);
	}
	
	// Selected Image Range.
	{
		const int32 IntParameterIndex = Instance->FindIntParameterNameIndex(ParamName.ToString() + IMAGE_PARAMETER_POSTFIX);
		
		FCustomizableObjectIntParameterValue& IntParameter = Instance->GetIntParameters()[IntParameterIndex];
		IntParameter.ParameterRangeValueNames.RemoveAt(Index);
	}
	
	// Opacity Range.
	{
		const int32 FloatParameterIndex = Instance->FindFloatParameterNameIndex(ParamName.ToString() + OPACITY_PARAMETER_POSTFIX);
		
		FCustomizableObjectFloatParameterValue& FloatParameter = Instance->GetFloatParameters()[FloatParameterIndex];
		FloatParameter.ParameterRangeValues.RemoveAt(Index);
	}
}


FMultilayerProjectorLayer FMultilayerProjector::GetLayer(const int32 Index) const
{
	FMultilayerProjectorLayer MultilayerProjectorLayer;
	MultilayerProjectorLayer.Read(*this, Index);
	return MultilayerProjectorLayer;
}


void FMultilayerProjector::UpdateLayer(const int32 Index, const FMultilayerProjectorLayer& Layer) const
{
	Layer.Write(*this, Index);
}


TArray<FName> FMultilayerProjector::GetVirtualLayers() const
{
	TArray<FName> VirtualLayers;
	VirtualLayersMapping.GetKeys(VirtualLayers);
	return VirtualLayers;
}


void FMultilayerProjector::CreateVirtualLayer(const FName& Id)
{
	if (!VirtualLayersMapping.Contains(Id))
	{
		const int32 Index = NumLayers();
		
		CreateLayer(Index);
		VirtualLayersMapping.Add(Id, Index);
		VirtualLayersOrder.Add(Id, NEW_VIRTUAL_LAYER_ORDER);
	}
}


FMultilayerProjectorVirtualLayer FMultilayerProjector::FindOrCreateVirtualLayer(const FName& Id)
{
	FMultilayerProjectorLayer Layer;
	bool bEnabled;
	int32 Order;

	if (const int32* Index = VirtualLayersMapping.Find(Id))
	{
		if (*Index == VIRTUAL_LAYER_DISABLED)
		{
			Layer = DisableVirtualLayers[Id];
			bEnabled = false;
		}
		else
		{
			Layer = GetLayer(*Index);
			bEnabled = true;
		}

		Order = VirtualLayersOrder[Id];
	}
	else
	{
		const int32 NewIndex = NumLayers();
		constexpr int32 NewOrder = NEW_VIRTUAL_LAYER_ORDER;
		
		CreateLayer(NewIndex);
		VirtualLayersMapping.Add(Id, NewIndex);
		VirtualLayersOrder.Add(Id, NewOrder);

		Layer = GetLayer(NewIndex);
		bEnabled = true;
		Order = NewOrder;
	}

	return FMultilayerProjectorVirtualLayer(Layer, bEnabled, Order);
}


void FMultilayerProjector::RemoveVirtualLayer(const FName& Id)
{
	const int32* Index = VirtualLayersMapping.Find(Id);
	check(Index); // Virtual Layer not created.
	
	if (*Index == VIRTUAL_LAYER_DISABLED)
	{
		DisableVirtualLayers.Remove(Id);
	}
	else
	{
		RemoveLayerAt(*Index);
		
		for (TMap<FName, int32>::TIterator It =  VirtualLayersMapping.CreateIterator(); It; ++It)
		{
			if (It.Key() == Id)
			{
				It.RemoveCurrent();
			}
			else if (It.Value() > *Index) // Update following Layers.
			{
				--It.Value();
			}
		}
	}

	VirtualLayersOrder.Remove(Id);
}


FMultilayerProjectorVirtualLayer FMultilayerProjector::GetVirtualLayer(const FName& Id) const
{
	const int32* Index = VirtualLayersMapping.Find(Id);
	check(Index); // Virtual Layer not created.

	const FMultilayerProjectorLayer Layer = GetLayer(*Index);
	const bool bEnabled = *Index != VIRTUAL_LAYER_DISABLED;
	const int32 Order = VirtualLayersOrder[Id];
	
	return FMultilayerProjectorVirtualLayer(Layer, bEnabled, Order);
}


void FMultilayerProjector::UpdateVirtualLayer(const FName& Id, const FMultilayerProjectorVirtualLayer& Layer)
{
	const int32* Index = VirtualLayersMapping.Find(Id);
	check(Index); // Virtual Layer not created.

	const bool bEnabled = *Index != VIRTUAL_LAYER_DISABLED;
	
	if (!bEnabled)
	{
		DisableVirtualLayers[Id] = static_cast<FMultilayerProjectorLayer>(Layer); // Update disabled layer.
		VirtualLayersOrder[Id] = Layer.Order;
	}
	else
	{
		int32* Order = VirtualLayersOrder.Find(Id);
		if (*Order != Layer.Order) // Order changed, check if it needs to be moved.
		{
			const int32 OldIndex = *Index;
			int32 NewIndex = CalculateVirtualLayerIndex(Id, Layer.Order);
			if (OldIndex != NewIndex) // Move required. Could be optimized by moving only the in-between values.
			{
				RemoveLayerAt(OldIndex);
				UpdateMappingVirtualLayerDisabled(Id, OldIndex);

				if (OldIndex < NewIndex)
				{
					NewIndex -= 1;
				}
				
				CreateLayer(NewIndex);
				UpdateMappingVirtualLayerEnabled(Id, NewIndex);
			}
			
			*Order = Layer.Order;
		}

		UpdateLayer(*Index, static_cast<FMultilayerProjectorLayer>(Layer)); // Update enabled layer.
	}
	
	// Enable or disable virtual layer.
	if (Layer.bEnabled && !bEnabled)
	{
		const int32 NewIndex = CalculateVirtualLayerIndex(Id, VirtualLayersOrder[Id]);

		CreateLayer(NewIndex);
		UpdateMappingVirtualLayerEnabled(Id, NewIndex);

		UpdateLayer(NewIndex, static_cast<FMultilayerProjectorLayer>(Layer));
		
		DisableVirtualLayers.Remove(Id);
	}
	else if (!Layer.bEnabled && bEnabled)
	{
		RemoveLayerAt(*Index);
		UpdateMappingVirtualLayerDisabled(Id, *Index);
		
		DisableVirtualLayers.Add(Id, Layer);
	}
}


FMultilayerProjector::FMultilayerProjector(UCustomizableObjectInstance& Instance, const FName& ParamName) :
	Instance(&Instance),
	ParamName(ParamName)
{
}


int32 FMultilayerProjector::CalculateVirtualLayerIndex(const FName& Id, const int32 InsertOrder) const
{
	int32 LayerBeforeIndex = -1;
	int32 LayerBeforeOrder = -1;
	
	for (const TTuple<FName, int>& MappingTuple : VirtualLayersMapping) // Find closest smallest layer.
	{
		if (MappingTuple.Value != VIRTUAL_LAYER_DISABLED && MappingTuple.Key != Id)
		{
			const int32 LayerOrder = VirtualLayersOrder[MappingTuple.Key];
			if (LayerOrder <= InsertOrder)
			{
				if ((LayerOrder > LayerBeforeOrder) ||
					(LayerOrder == LayerBeforeOrder && MappingTuple.Value > LayerBeforeIndex))
				{
					LayerBeforeIndex = MappingTuple.Value;
					LayerBeforeOrder = LayerOrder;
				}
			}
		}
	}
	
	return LayerBeforeIndex + 1;
}


void FMultilayerProjector::UpdateMappingVirtualLayerEnabled(const FName& Id, const int32 Index)
{
	for (TTuple<FName, int>& Tuple : VirtualLayersMapping)
	{
		if (Tuple.Key == Id)
		{
			Tuple.Value = Index;
		}
		else if (Tuple.Value >= Index) // Update following Layers.
		{
			++Tuple.Value;
		}
	}
}


void FMultilayerProjector::UpdateMappingVirtualLayerDisabled(const FName& Id, const int32 Index)
{
	for (TTuple<FName, int>& Tuple : VirtualLayersMapping)
	{
		if (Tuple.Key == Id)
    	{
			Tuple.Value = VIRTUAL_LAYER_DISABLED;
    	}
		else if (Tuple.Value > Index) // Update following Layers.
		{
			--Tuple.Value;
		}
	}
}


void FMultilayerProjector::CheckInstanceParameters() const
{
	check(Instance); // Multilayer Projector created without using the Instance factory.

	const FString ParamNameString = ParamName.ToString();

	// Num layers.
	{
		const FString NumLayersParamName = ParamNameString + NUM_LAYERS_PARAMETER_POSTFIX;
		const int32 FloatParameterIndex = Instance->FindFloatParameterNameIndex(NumLayersParamName);
		check(FloatParameterIndex >= 0); // Instance Parameter does not exist.
	}
    
	// Projector.
	{
		const int32 ProjectorParameterIndex = Instance->FindProjectorParameterNameIndex(ParamNameString);
		check(ProjectorParameterIndex >= 0) // Instance Parameter does not exist.
	}
	
	// Selected Image.
	{
		const int32 IntParameterIndex = Instance->FindIntParameterNameIndex(ParamNameString + IMAGE_PARAMETER_POSTFIX);
		check(IntParameterIndex >= 0) // Instance Parameter does not exist.
	}
	
	// Opacity.
	{
		const int32 FloatParameterIndex = Instance->FindFloatParameterNameIndex(ParamNameString + OPACITY_PARAMETER_POSTFIX);
		check(FloatParameterIndex >= 0) // Instance Parameter does not exist.
	}
}


bool FMultilayerProjector::AreInstanceParametersValid(const UCustomizableObjectInstance& Instance, const FName& ParamName)
{
	const FString ParamNameString = ParamName.ToString();

	// Num layers.
	{
		const FString NumLayersParamName = ParamNameString + NUM_LAYERS_PARAMETER_POSTFIX;
		const int32 FloatParameterIndex = Instance.FindFloatParameterNameIndex(NumLayersParamName);
		if (FloatParameterIndex < 0)
		{
			return false;
		}
	}
    
	// Projector.
	{
		const int32 ProjectorParameterIndex = Instance.FindProjectorParameterNameIndex(ParamNameString);
		if (ProjectorParameterIndex < 0)
		{
			return false;
		}
	}
	
	// Selected Image.
	{
		const int32 IntParameterIndex = Instance.FindIntParameterNameIndex(ParamNameString + IMAGE_PARAMETER_POSTFIX);
		if (IntParameterIndex < 0)
		{
			return false;
		}
	}
	
	// Opacity.
	{
		const int32 FloatParameterIndex = Instance.FindFloatParameterNameIndex(ParamNameString + OPACITY_PARAMETER_POSTFIX);
		if (FloatParameterIndex < 0)
		{
			return false;
		}
	}

	return true;
}

