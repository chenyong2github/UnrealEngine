// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusComputeGraph.h"

#include "IOptimusComputeKernelProvider.h"
#include "IOptimusValueProvider.h"
#include "OptimusDeformer.h"
#include "OptimusCoreModule.h"
#include "OptimusNode.h"

#include "Internationalization/Regex.h"
#include "Misc/UObjectToken.h"


#define LOCTEXT_NAMESPACE "OptimusComputeGraph"


void UOptimusComputeGraph::GetKernelBindings(int32 InKernelIndex, TMap<int32, TArray<uint8>>& OutBindings) const
{
	for (const FOptimus_ShaderParameterBinding& Binding: KernelParameterBindings)
	{
		if (Binding.KernelIndex == InKernelIndex)
		{
			// This may happen if the node has been GC'd.
			if (const IOptimusValueProvider *ValueProvider = Cast<const IOptimusValueProvider>(Binding.ValueNode))
			{
				TArray<uint8> ValueData = ValueProvider->GetShaderValue();
				if (!ValueData.IsEmpty())
				{
					OutBindings.Emplace(Binding.ParameterIndex, MoveTemp(ValueData));
				}
			}
		}
	}
}


void UOptimusComputeGraph::OnKernelCompilationComplete(int32 InKernelIndex, const TArray<FString>& InCompileErrors)
{
	// Find the Optimus objects from the raw kernel index.
	if (KernelToNode.IsValidIndex(InKernelIndex))
	{
		UOptimusDeformer* Owner = Cast<UOptimusDeformer>(GetOuter());
		
		// Make sure the node hasn't been GC'd.
		if (UOptimusNode* Node = const_cast<UOptimusNode*>(KernelToNode[InKernelIndex].Get()))
		{
			IOptimusComputeKernelProvider* KernelProvider = Cast<IOptimusComputeKernelProvider>(Node);
			if (ensure(KernelProvider != nullptr))
			{
				TArray<FOptimusCompilerDiagnostic>  Diagnostics;

				// This is a compute kernel as expected so broadcast the compile errors.
				for (FString const& CompileError : InCompileErrors)
				{
					FOptimusCompilerDiagnostic Diagnostic = ProcessCompilationMessage(Owner, Node, CompileError);
					if (Diagnostic.Level != EOptimusDiagnosticLevel::None)
					{
						Diagnostics.Add(Diagnostic);
					}
				}

				KernelProvider->SetCompilationDiagnostics(Diagnostics);
			}
		}
	}
}


FOptimusCompilerDiagnostic UOptimusComputeGraph::ProcessCompilationMessage(
	UOptimusDeformer* InOwner,
	const UOptimusNode* InKernelNode,
	const FString& InMessage
	)
{
	// "/Engine/Generated/ComputeFramework/Kernel_LinearBlendSkinning.usf(19,39-63):  error X3013: 'DI000_ReadNumVertices': no matching 1 parameter function"	
	// "OptimusNode_ComputeKernel_2(1,42):  error X3004: undeclared identifier 'a'"

	// TODO: Parsing diagnostics rightfully belongs at the shader compiler level, especially if
	// the shader compiler is rewriting.
	static const FRegexPattern MessagePattern(TEXT(R"(^\s*(.*?)\((\d+),(\d+)(-(\d+))?\):\s*(error|warning)\s+[A-Z0-9]+:\s*(.*)$)"));

	FRegexMatcher Matcher(MessagePattern, InMessage);
	if (!Matcher.FindNext())
	{
		UE_LOG(LogOptimusCore, Warning, TEXT("Cannot parse message from shader compiler: [%s]"), *InMessage);
		return {};
	}

	// FString NodeName = Matcher.GetCaptureGroup(1);
	const int32 LineNumber = FCString::Atoi(*Matcher.GetCaptureGroup(2));
	const int32 ColumnStart = FCString::Atoi(*Matcher.GetCaptureGroup(3));
	const FString ColumnEndStr = Matcher.GetCaptureGroup(5);
	const int32 ColumnEnd = ColumnEndStr.IsEmpty() ? ColumnStart : FCString::Atoi(*ColumnEndStr);
	const FString SeverityStr = Matcher.GetCaptureGroup(6);
	const FString MessageStr = Matcher.GetCaptureGroup(7);

	EMessageSeverity::Type Severity = EMessageSeverity::Error; 
	EOptimusDiagnosticLevel Level = EOptimusDiagnosticLevel::Error;
	if (SeverityStr == TEXT("warning"))
	{
		Level = EOptimusDiagnosticLevel::Warning;
		Severity = EMessageSeverity::Warning;
	}

	if (InOwner)
	{
		// Set a dummy lambda for token activation because the default behavior for FUObjectToken is
		// to pop up the asset browser :-/
		static auto DummyActivation = [](const TSharedRef<class IMessageToken>&) {};
		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(
			Severity, 
			FText::Format(LOCTEXT("LineMessage", "{0} (line {1})"), FText::FromString(MessageStr), FText::AsNumber(LineNumber)));
		Message->AddToken(FUObjectToken::Create(InKernelNode)->OnMessageTokenActivated(FOnMessageTokenActivated::CreateLambda(DummyActivation)));
		InOwner->GetCompileMessageDelegate().Broadcast(Message);
	}

	return FOptimusCompilerDiagnostic(Level, MessageStr, LineNumber, ColumnStart, ColumnEnd);
}


#undef LOCTEXT_NAMESPACE
