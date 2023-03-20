// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGeneratorOutputWatcher.h"

namespace Metasound::Private
{
	TMap<FName, TUniquePtr<FMetasoundGeneratorOutputWatcher::IOutputTypeOperations>> FMetasoundGeneratorOutputWatcher::OutputTypeOperationMap{};
	
	FMetasoundGeneratorOutputWatcher::FMetasoundGeneratorOutputWatcher(
		Frontend::FAnalyzerAddress&& Address,
		const FOperatorSettings& OperatorSettings)
		: Name(Address.OutputName)
		, View(MoveTemp(Address))
	{
		View.BindToAllOutputs(OperatorSettings);
		
		TArray<Frontend::FMetasoundAnalyzerView::FBoundOutputDescription> OutputDescriptions = View.GetBoundOutputDescriptions();
		for (Frontend::FMetasoundAnalyzerView::FBoundOutputDescription& OutputDescription : OutputDescriptions)
		{
			if (!ensure(OutputTypeOperationMap.Contains(OutputDescription.TypeName)))
			{
				continue;
			}

			FMetasoundGeneratorOutput Output;
			Output.Name = OutputDescription.Name;
			OutputTypeOperationMap[OutputDescription.TypeName]->Init(Output);
			Outputs.Emplace(MoveTemp(Output));
		}
	}

	void FMetasoundGeneratorOutputWatcher::Update(const TFunctionRef<void(FName, const FMetasoundGeneratorOutput&)> OnOutputChanged)
	{
		for (FMetasoundGeneratorOutput& Output : Outputs)
		{
			const FName TypeName = Output.GetTypeName();
			check(!TypeName.IsNone());
			
			if (ensure(OutputTypeOperationMap.Contains(TypeName)))
			{
				if (OutputTypeOperationMap[TypeName]->Update(*this, Output))
				{
					OnOutputChanged(Name, Output);
				}
			}
		}
	}

	void FMetasoundGeneratorOutputWatcher::RegisterOutputTypeOperations(FName TypeName,
		TUniquePtr<IOutputTypeOperations>&& OutputTypeOperations)
	{
		check(!OutputTypeOperationMap.Contains(TypeName));
		check(OutputTypeOperations.IsValid());
		OutputTypeOperationMap.Emplace(TypeName, MoveTemp(OutputTypeOperations));
	}
}
