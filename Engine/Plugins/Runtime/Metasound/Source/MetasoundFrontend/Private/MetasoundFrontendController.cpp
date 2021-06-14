// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendController.h"

#include "MetasoundFrontendInvalidController.h"
#include "MetasoundFrontendStandardController.h"

namespace Metasound
{
	namespace Frontend
	{
		FOutputHandle IOutputController::GetInvalidHandle()
		{
			return FInvalidOutputController::GetInvalid();
		}

		FInputHandle IInputController::GetInvalidHandle()
		{
			return FInvalidInputController::GetInvalid();
		}

		FNodeHandle INodeController::GetInvalidHandle()
		{
			return FInvalidNodeController::GetInvalid();
		}

		FGraphHandle IGraphController::GetInvalidHandle()
		{
			return FInvalidGraphController::GetInvalid();
		}

		FDocumentHandle IDocumentController::GetInvalidHandle()
		{
			return FInvalidDocumentController::GetInvalid();
		}
			
		FDocumentHandle IDocumentController::CreateDocumentHandle(FDocumentAccessPtr InDocument)
		{
			// Create using standard document controller.
			return FDocumentController::CreateDocumentHandle(InDocument);
		}

		FDocumentHandle IDocumentController::CreateDocumentHandle(FMetasoundFrontendDocument& InDocument)
		{
			return CreateDocumentHandle(MakeAccessPtr<FDocumentAccessPtr>(InDocument.AccessPoint, InDocument));
		}

		FConstDocumentHandle IDocumentController::CreateDocumentHandle(FConstDocumentAccessPtr InDocument)
		{
			// Create using standard document controller. 
			return FDocumentController::CreateDocumentHandle(ConstCastAccessPtr<FDocumentAccessPtr>(InDocument));
		}

		FConstDocumentHandle IDocumentController::CreateDocumentHandle(const FMetasoundFrontendDocument& InDocument)
		{
			return CreateDocumentHandle(MakeAccessPtr<FConstDocumentAccessPtr>(InDocument.AccessPoint, InDocument));
		}
	}
}
