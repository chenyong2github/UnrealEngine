// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/EditorElementSubsystem.h"

#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "Engine/World.h"

bool UEditorElementSubsystem::SetElementTransform(FTypedElementHandle InElementHandle, const FTransform& InWorldTransform)
{
	if (TTypedElement<ITypedElementWorldInterface> WorldInterfaceElement = UTypedElementRegistry::GetInstance()->GetElement<ITypedElementWorldInterface>(InElementHandle))
	{
		if (UWorld* ElementWorld = WorldInterfaceElement.GetOwnerWorld())
		{
			ETypedElementWorldType WorldType = ElementWorld->IsGameWorld() ? ETypedElementWorldType::Game : ETypedElementWorldType::Editor;
			if (WorldInterfaceElement.CanMoveElement(WorldType))
			{
				WorldInterfaceElement.NotifyMovementStarted();
				WorldInterfaceElement.SetWorldTransform(InWorldTransform);
				WorldInterfaceElement.NotifyMovementEnded();

				return true;
			}
		}
	}

	return false;
}