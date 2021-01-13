// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendInvalidController.h"

namespace Metasound
{
	namespace Frontend
	{
		TSharedRef<INodeController> FInvalidOutputController::GetOwningNode()
		{
			return FInvalidNodeController::GetInvalid();
		}

		TSharedRef<const INodeController> FInvalidOutputController::GetOwningNode() const
		{
			return FInvalidNodeController::GetInvalid();
		}

		TSharedRef<INodeController> FInvalidInputController::GetOwningNode()
		{
			return FInvalidNodeController::GetInvalid();
		}

		TSharedRef<const INodeController> FInvalidInputController::GetOwningNode() const
		{
			return FInvalidNodeController::GetInvalid();
		}

		TSharedRef<IGraphController> FInvalidNodeController::AsGraph()
		{
			return FInvalidGraphController::GetInvalid();
		}

		TSharedRef<const IGraphController> FInvalidNodeController::AsGraph() const
		{
			return FInvalidGraphController::GetInvalid();
		}

		TSharedRef<IGraphController> FInvalidNodeController::GetOwningGraph()
		{
			return FInvalidGraphController::GetInvalid();
		}

		TSharedRef<const IGraphController> FInvalidNodeController::GetOwningGraph() const
		{
			return FInvalidGraphController::GetInvalid();
		}

		TSharedRef<IDocumentController> FInvalidGraphController::GetOwningDocument()
		{
			return FInvalidDocumentController::GetInvalid();
		}

		TSharedRef<const IDocumentController> FInvalidGraphController::GetOwningDocument() const
		{
			return FInvalidDocumentController::GetInvalid();
		}

		TSharedRef<IGraphController> FInvalidDocumentController::GetRootGraph()
		{
			return FInvalidGraphController::GetInvalid();
		}

		TSharedRef<const IGraphController> FInvalidDocumentController::GetRootGraph() const
		{
			return FInvalidGraphController::GetInvalid();
		}
	}
}
