// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundInterface.h"

#include "IAudioParameterTransmitter.h"


namespace Metasound
{
	namespace Engine
	{
		void RegisterExternalInterfaces()
		{
			using namespace Audio;

			auto RegisterExternalInterface = [](FGeneratorInterfacePtr Interface)
			{
				auto ResolveMemberDataType = [](FName DataType, EAudioParameterType ParamType)
				{
					if (!DataType.IsNone())
					{
						const bool bIsRegisteredType = Frontend::IDataTypeRegistry::Get().IsRegistered(DataType);
						if (ensureAlwaysMsgf(bIsRegisteredType, TEXT("Attempting to register Interface member with unregistered DataType '%s'."), *DataType.ToString()))
						{
							return DataType;
						}
					}

					return Frontend::ConvertParameterToDataType(ParamType);
				};

				FMetasoundFrontendInterface FrontendInterface;
				FrontendInterface.Version = { Interface->Name, FMetasoundFrontendVersionNumber { Interface->Version.Major, Interface->Version.Minor } };

				Algo::Transform(Interface->Inputs, FrontendInterface.Inputs, [&](const FGeneratorInterface::FInput& Input)
				{
					FMetasoundFrontendClassInput ClassInput;
					ClassInput.Name = IGeneratorInterfaceRegistry::GetMemberFullName(Interface->Name, Input.InitValue.ParamName);
					ClassInput.DefaultLiteral = FMetasoundFrontendLiteral(Input.InitValue);
					ClassInput.TypeName = ResolveMemberDataType(Input.DataType, Input.InitValue.ParamType);
					ClassInput.Metadata.DisplayName = Input.DisplayName;
					ClassInput.Metadata.Description = Input.Description;
					ClassInput.VertexID = FGuid::NewGuid();

					return ClassInput;
				});

				Algo::Transform(Interface->Outputs, FrontendInterface.Outputs, [&](const FGeneratorInterface::FOutput& Output)
				{
					FMetasoundFrontendClassOutput ClassOutput;
					ClassOutput.Name = IGeneratorInterfaceRegistry::GetMemberFullName(Interface->Name, Output.ParamName);
					ClassOutput.TypeName = ResolveMemberDataType(Output.DataType, Output.ParamType);
					ClassOutput.Metadata.DisplayName = Output.DisplayName;
					ClassOutput.Metadata.Description = Output.Description;
					ClassOutput.VertexID = FGuid::NewGuid();

					return ClassOutput;
				});

				Algo::Transform(Interface->Environment, FrontendInterface.Environment, [&](const FGeneratorInterface::FEnvironmentVariable& Environment)
				{
					FMetasoundFrontendClassEnvironmentVariable EnvironmentVariable;
					EnvironmentVariable.Name = IGeneratorInterfaceRegistry::GetMemberFullName(Interface->Name, Environment.ParamName);
					EnvironmentVariable.TypeName = ResolveMemberDataType(Environment.DataType, Environment.ParamType);
					EnvironmentVariable.Metadata.DisplayName = Environment.DisplayName;
					EnvironmentVariable.Metadata.Description = Environment.Description;

					return EnvironmentVariable;
				});

				// For now, only support interfaces for UMetaSoundSource type
				if (ensureAlways(UMetaSoundSource::StaticClass()->IsChildOf(Interface->Type)))
				{
					// For now, all external interfaces use the ParameterTransmitter and cannot be applied by default to new
					// MetaSound objects. Could pipe either of these through in the future if required/desired.
					constexpr bool bIsDefault = false;
					RegisterInterface<UMetaSoundSource>(FrontendInterface, nullptr, bIsDefault, Audio::IParameterTransmitter::RouterName);
					UE_LOG(LogMetaSound, Verbose, TEXT("Interface '%s' registered as MetaSoundSource assets."), *Interface->Name.ToString());
				}
			};

			IGeneratorInterfaceRegistry::Get().IterateInterfaces(RegisterExternalInterface);
			IGeneratorInterfaceRegistry::Get().OnRegistration(MoveTemp(RegisterExternalInterface));
		}
	} // namespace Engine
} // namespace Metasound
