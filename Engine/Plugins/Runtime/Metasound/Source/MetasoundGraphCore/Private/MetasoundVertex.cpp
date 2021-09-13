// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundVertex.h"

#include "CoreMinimal.h"

namespace Metasound
{
	FInputDataVertex::FInputDataVertex()
	:	VertexModel(MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty()))
	{}

	FInputDataVertex::FInputDataVertex(const FInputDataVertex& InOther)
	:	VertexModel()
	{
		// Copy constructor has to call underlying model's clone.
		if (InOther.VertexModel.IsValid())
		{
			VertexModel = InOther.VertexModel->Clone();
		}

		if (!VertexModel.IsValid())
		{
			// Default to empty vertex if there was a failure. 
			VertexModel = MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty());
		}
	}

	FInputDataVertex& FInputDataVertex::operator=(const FInputDataVertex& InOther)
	{
		// Assignment operator has to call underlying model's clone.
		if (InOther.VertexModel.IsValid())
		{
			VertexModel = InOther.VertexModel->Clone();
		}

		if (!VertexModel.IsValid())
		{
			// Default to empty vertex if there was a failure. 
			VertexModel = MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty());
		}

		return *this;
	}

	const FVertexName& FInputDataVertex::GetVertexName() const
	{
		return VertexModel->VertexName;
	}

	const FName& FInputDataVertex::GetDataTypeName() const
	{
		return VertexModel->DataTypeName;
	}

	FLiteral FInputDataVertex::GetDefaultLiteral() const
	{
		return VertexModel->CreateDefaultLiteral();
	}

	const FDataVertexMetadata& FInputDataVertex::GetMetadata() const
	{
		return VertexModel->VertexMetadata;
	}

	bool FInputDataVertex::IsReferenceOfSameType(const IDataReference& InReference) const
	{
		return VertexModel->IsReferenceOfSameType(InReference);
	}

	const FName& FInputDataVertex::GetVertexTypeName() const 
	{
		return VertexModel->GetVertexTypeName();
	}

	bool operator==(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS)
	{
		return (InLHS.VertexModel->IsEqual(*InRHS.VertexModel) && (InRHS.VertexModel->IsEqual(*InLHS.VertexModel)));
	}

	bool operator!=(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS)
	{
		return !(InLHS == InRHS);
	}

	bool operator<(const FInputDataVertex& InLHS, const FInputDataVertex& InRHS)
	{
		if (InLHS == InRHS)
		{
			return false;
		}
		
		if (InLHS.GetVertexName() == InRHS.GetVertexName())
		{
			return InLHS.GetDataTypeName().FastLess(InRHS.GetDataTypeName());
		}
		else
		{
			return InLHS.GetVertexName().FastLess(InRHS.GetVertexName());
		}
	}

	FOutputDataVertex::FOutputDataVertex()
	:	VertexModel(MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty()))
	{}

	FOutputDataVertex::FOutputDataVertex(const FOutputDataVertex& InOther)
	:	VertexModel()
	{
		// Copy constructor has to call underlying model's clone.
		if (InOther.VertexModel.IsValid())
		{
			VertexModel = InOther.VertexModel->Clone();
		}

		if (!VertexModel.IsValid())
		{
			// Default to empty vertex if there was a failure. 
			VertexModel = MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty());
		}
	}

	FOutputDataVertex& FOutputDataVertex::operator=(const FOutputDataVertex& InOther)
	{
		// assignment operator has to call underlying model's clone.
		if (InOther.VertexModel.IsValid())
		{
			VertexModel = InOther.VertexModel->Clone();
		}

		if (!VertexModel.IsValid())
		{
			// Default to empty vertex if there was a failure. 
			VertexModel = MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty());
		}

		return *this;
	}

	FVertexName FOutputDataVertex::GetVertexName() const
	{
		return VertexModel->VertexName;
	}

	const FName& FOutputDataVertex::GetDataTypeName() const
	{
		return VertexModel->DataTypeName;
	}

	const FDataVertexMetadata& FOutputDataVertex::GetMetadata() const
	{
		return VertexModel->VertexMetadata;
	}

	bool FOutputDataVertex::IsReferenceOfSameType(const IDataReference& InReference) const
	{
		return VertexModel->IsReferenceOfSameType(InReference);
	}

	const FName& FOutputDataVertex::GetVertexTypeName() const
	{
		return VertexModel->GetVertexTypeName();
	}


	bool operator==(const FOutputDataVertex& InLHS, const FOutputDataVertex& InRHS)
	{
		return (InLHS.VertexModel->IsEqual(*InRHS.VertexModel) && (InRHS.VertexModel->IsEqual(*InLHS.VertexModel)));
	}

	bool operator!=(const FOutputDataVertex& InLHS, const FOutputDataVertex& InRHS)
	{
		return !(InLHS == InRHS);
	}

	bool operator<(const FOutputDataVertex& InLHS, const FOutputDataVertex& InRHS)
	{
		if (InLHS == InRHS)
		{
			return false;
		}
		
		if (InLHS.GetVertexName() == InRHS.GetVertexName())
		{
			return InLHS.GetDataTypeName().FastLess(InRHS.GetDataTypeName());
		}
		else
		{
			return InLHS.GetVertexName().FastLess(InRHS.GetVertexName());
		}
	}

	FEnvironmentVertex::FEnvironmentVertex()
	:	VertexModel(MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty()))
	{
	}

	/** Copy constructor */
	FEnvironmentVertex::FEnvironmentVertex(const FEnvironmentVertex& InOther)
	{
		if (InOther.VertexModel.IsValid())
		{
			// call underlying model's clone to copy model.
			VertexModel = InOther.VertexModel->Clone();
		}
		else
		{
			// Make an empty model if no valid model exists.
			VertexModel = MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty());
		}
	}

	FEnvironmentVertex& FEnvironmentVertex::operator=(const FEnvironmentVertex& InOther)
	{
		if (InOther.VertexModel.IsValid())
		{
			// call underlying model's clone to copy model.
			VertexModel = InOther.VertexModel->Clone();
		}
		else
		{
			// Make an empty model if no valid model exists.
			VertexModel = MakeUnique<FEmptyVertexModel>(TEXT(""), FText::GetEmpty());
		}

		return *this;
	}

	const FVertexName& FEnvironmentVertex::GetVertexName() const
	{
		return VertexModel->VertexName;
	}

	const FText& FEnvironmentVertex::GetDescription() const
	{
		return VertexModel->Description;
	}

	bool FEnvironmentVertex::IsVariableOfSameType(const IMetasoundEnvironmentVariable& InVariable) const
	{
		return VertexModel->IsVariableOfSameType(InVariable);
	}

	bool operator==(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS)
	{
		return (InLHS.VertexModel->IsEqual(*InRHS.VertexModel) && InRHS.VertexModel->IsEqual(*InLHS.VertexModel));
	}

	bool operator!=(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS)
	{
		return !(InLHS == InRHS);
	}

	bool operator<(const FEnvironmentVertex& InLHS, const FEnvironmentVertex& InRHS)
	{
		if (InLHS == InRHS)
		{
			return false;
		}

		return InLHS.GetVertexName().FastLess(InRHS.GetVertexName());
	}

	FVertexInterface::FVertexInterface(const FInputVertexInterface& InInputs, const FOutputVertexInterface& InOutputs)
	:	InputInterface(InInputs)
	,	OutputInterface(InOutputs)
	{
	}

	FVertexInterface::FVertexInterface(const FInputVertexInterface& InInputs, const FOutputVertexInterface& InOutputs, const FEnvironmentVertexInterface& InEnvironmentVariables)
	:	InputInterface(InInputs)
	,	OutputInterface(InOutputs)
	,	EnvironmentInterface(InEnvironmentVariables)
	{
	}

	const FInputVertexInterface& FVertexInterface::GetInputInterface() const
	{
		return InputInterface;
	}

	FInputVertexInterface& FVertexInterface::GetInputInterface()
	{
		return InputInterface;
	}

	const FInputDataVertex& FVertexInterface::GetInputVertex(const FVertexName& InKey) const
	{
		return InputInterface[InKey];
	}

	bool FVertexInterface::ContainsInputVertex(const FVertexName& InKey) const
	{
		return InputInterface.Contains(InKey);
	}

	const FOutputVertexInterface& FVertexInterface::GetOutputInterface() const
	{
		return OutputInterface;
	}

	FOutputVertexInterface& FVertexInterface::GetOutputInterface()
	{
		return OutputInterface;
	}

	const FOutputDataVertex& FVertexInterface::GetOutputVertex(const FVertexName& InName) const
	{
		return OutputInterface[InName];
	}

	bool FVertexInterface::ContainsOutputVertex(const FVertexName& InName) const
	{
		return OutputInterface.Contains(InName);
	}

	const FEnvironmentVertexInterface& FVertexInterface::GetEnvironmentInterface() const
	{
		return EnvironmentInterface;
	}

	FEnvironmentVertexInterface& FVertexInterface::GetEnvironmentInterface()
	{
		return EnvironmentInterface;
	}

	const FEnvironmentVertex& FVertexInterface::GetEnvironmentVertex(const FVertexName& InKey) const
	{
		return EnvironmentInterface[InKey];
	}

	bool FVertexInterface::ContainsEnvironmentVertex(const FVertexName& InKey) const
	{
		return EnvironmentInterface.Contains(InKey);
	}

	bool operator==(const FVertexInterface& InLHS, const FVertexInterface& InRHS)
	{
		const bool bIsEqual = (InLHS.InputInterface == InRHS.InputInterface) && 
			(InLHS.OutputInterface == InRHS.OutputInterface) && 
			(InLHS.EnvironmentInterface == InRHS.EnvironmentInterface);

		return bIsEqual;
	}

	bool operator!=(const FVertexInterface& InLHS, const FVertexInterface& InRHS)
	{
		return !(InLHS == InRHS);
	}
}
