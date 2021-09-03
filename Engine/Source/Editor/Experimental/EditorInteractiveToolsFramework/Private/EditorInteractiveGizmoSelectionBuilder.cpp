// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorInteractiveGizmoSelectionBuilder.h"
#include "BaseGizmos/TransformProxy.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "Engine/Selection.h"
#include "Engine/World.h"

UTransformProxy* FEditorGizmoSelectionBuilderHelper::CreateTransformProxyForSelection(const FToolBuilderState& SceneState)
{
	// @todo - once UTransformProxy supports typed elements, update this to use the normalized typed
	// element selection set.
	if (UTypedElementSelectionSet* SelectionSet = SceneState.TypedElementSelectionSet.Get())
	{
		if (SelectionSet->GetNumSelectedElements() > 0)
		{
			bool bHasSelectedElements = false;
			UTransformProxy* TransformProxy = NewObject<UTransformProxy>();

			SelectionSet->ForEachSelectedElement<ITypedElementWorldInterface>([TransformProxy, SelectionSet, &bHasSelectedElements](const TTypedElement<ITypedElementWorldInterface>& InWorldElement)
				{
					if (InWorldElement.CanMoveElement(ETypedElementWorldType::Editor))
					{
						if (TTypedElement<ITypedElementObjectInterface> ObjectTypedElement = SelectionSet->GetElementList()->GetElement<ITypedElementObjectInterface>(InWorldElement))
						{
							if (AActor* Actor = ObjectTypedElement.GetObjectAs<AActor>())
							{
								USceneComponent* SceneComponent = Actor->GetRootComponent();
								TransformProxy->AddComponent(SceneComponent);
								bHasSelectedElements = true;
							}
						}
					}
					return true;
				});

			if (bHasSelectedElements)
			{
				return TransformProxy;
			}
		}
	}

	return nullptr;
}
	