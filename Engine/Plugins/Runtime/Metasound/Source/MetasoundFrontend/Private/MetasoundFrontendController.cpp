// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendController.h"

#include "MetasoundFrontendInvalidController.h"
#include "MetasoundFrontendDocumentController.h"

namespace Metasound
{
	namespace Frontend
	{
		FOutputHandle IOutputController::GetInvalidHandle()
		{
			static FOutputHandle Invalid = MakeShared<FInvalidOutputController>();
			return Invalid;
		}

		FInputHandle IInputController::GetInvalidHandle()
		{
			static FInputHandle Invalid = MakeShared<FInvalidInputController>();
			return Invalid;
		}

		FVariableHandle IVariableController::GetInvalidHandle()
		{
			static FVariableHandle Invalid = MakeShared<FInvalidVariableController>();
			return Invalid;
		}

		FNodeHandle INodeController::GetInvalidHandle()
		{
			static FNodeHandle Invalid = MakeShared<FInvalidNodeController>();
			return Invalid;
		}

		FGraphHandle IGraphController::GetInvalidHandle()
		{
			static FGraphHandle Invalid = MakeShared<FInvalidGraphController>();
			return Invalid;
		}

		FDocumentHandle IDocumentController::GetInvalidHandle()
		{
			static FDocumentHandle Invalid = MakeShared<FInvalidDocumentController>();
			return Invalid;
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

		FDocumentAccess IDocumentAccessor::GetSharedAccess(IDocumentAccessor& InDocumentAccessor)
		{
			return InDocumentAccessor.ShareAccess();
		}

		FConstDocumentAccess IDocumentAccessor::GetSharedAccess(const IDocumentAccessor& InDocumentAccessor)
		{
			return InDocumentAccessor.ShareAccess();
		}
	}
}
