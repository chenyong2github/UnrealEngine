// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundOutputWatcher.h"
#include "MetasoundTrace.h"

namespace Metasound::Private
{
	TMap<FName, TUniquePtr<FMetasoundOutputWatcher::IOutputTypeOperations>> FMetasoundOutputWatcher::OutputTypeOperationMap{};
	
	FMetasoundOutputWatcher::FMetasoundOutputWatcher(
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

			FMetaSoundOutput Output;
			Output.Name = OutputDescription.Name;
			OutputTypeOperationMap[OutputDescription.TypeName]->Init(Output);
			Outputs.Emplace(MoveTemp(Output));
		}
	}

	void FMetasoundOutputWatcher::Update(const TFunctionRef<void(FName, const FMetaSoundOutput&)> OnOutputChanged)
	{
		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetasoundOutputWatcher::Update);
		
		for (FMetaSoundOutput& Output : Outputs)
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

	void FMetasoundOutputWatcher::RegisterOutputTypeOperations(FName TypeName,
		TUniquePtr<IOutputTypeOperations>&& OutputTypeOperations)
	{
		check(!OutputTypeOperationMap.Contains(TypeName));
		check(OutputTypeOperations.IsValid());
		OutputTypeOperationMap.Emplace(TypeName, MoveTemp(OutputTypeOperations));
	}
}
