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

	const FString& FInputDataVertex::GetVertexName() const
	{
		return VertexModel->VertexName;
	}

	const FName& FInputDataVertex::GetDataTypeName() const
	{
		return VertexModel->DataTypeName;
	}

	const FText& FInputDataVertex::GetDescription() const
	{
		return VertexModel->Description;
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
			return InLHS.GetVertexName() < InRHS.GetVertexName();
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

	const FString& FOutputDataVertex::GetVertexName() const
	{
		return VertexModel->VertexName;
	}

	const FName& FOutputDataVertex::GetDataTypeName() const
	{
		return VertexModel->DataTypeName;
	}

	const FText& FOutputDataVertex::GetDescription() const
	{
		return VertexModel->Description;
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
			return InLHS.GetVertexName() < InRHS.GetVertexName();
		}
	}

			/** Construct with an input and output interface. */
	FVertexInterface::FVertexInterface(const FInputVertexInterface& InInputs, const FOutputVertexInterface& InOutputs)
	:	InputInterface(InInputs)
	,	OutputInterface(InOutputs)
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

	const FInputDataVertex& FVertexInterface::GetInputVertex(const FDataVertexKey& InKey) const
	{
		return InputInterface[InKey];
	}

	bool FVertexInterface::ContainsInputVertex(const FDataVertexKey& InKey) const
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

	const FOutputDataVertex& FVertexInterface::GetOutputVertex(const FDataVertexKey& InKey) const
	{
		return OutputInterface[InKey];
	}

	bool FVertexInterface::ContainsOutputVertex(const FDataVertexKey& InKey) const
	{
		return OutputInterface.Contains(InKey);
	}

	bool operator==(const FVertexInterface& InLHS, const FVertexInterface& InRHS)
	{
		return (InLHS.InputInterface == InRHS.InputInterface) && (InLHS.OutputInterface == InRHS.OutputInterface);
	}

	bool operator!=(const FVertexInterface& InLHS, const FVertexInterface& InRHS)
	{
		return !(InLHS == InRHS);
	}
}
