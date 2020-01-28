// Copyright Epic Games, Inc. All Rights Reserved.

#include "InteractionMechanic.h"


UInteractionMechanic::UInteractionMechanic()
{
	// undo/redo doesn't work on uproperties unless UObject is transactional
	//SetFlags(RF_Transactional);
}


void UInteractionMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	ParentTool = ParentToolIn;
}

void UInteractionMechanic::Shutdown()
{
	ParentTool = nullptr;
}


void UInteractionMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
}


void UInteractionMechanic::Tick(float DeltaTime)
{

}


UInteractiveTool* UInteractionMechanic::GetParentTool() const
{
	return ParentTool.Get();
}



void UInteractionMechanic::AddToolPropertySource(UInteractiveToolPropertySet* PropertySet)
{
	if (ParentTool.IsValid())
	{
		ParentTool->AddToolPropertySource(PropertySet);
	}
}