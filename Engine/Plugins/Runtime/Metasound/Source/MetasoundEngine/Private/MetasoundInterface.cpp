// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundInterface.h"

#include "Algo/Transform.h"
#include "AudioParameterControllerInterface.h"
#include "IAudioParameterInterfaceRegistry.h"
#include "IAudioParameterTransmitter.h"
#include "Interfaces/MetasoundInputFormatInterfaces.h"
#include "Interfaces/MetasoundOutputFormatInterfaces.h"
#include "Interfaces/MetasoundFrontendSourceInterface.h"
#include "Metasound.h"
#include "MetasoundEngineArchetypes.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundLog.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundSource.h"
#include "MetasoundUObjectRegistry.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "UObject/Class.h"
#include "UObject/NoExportTypes.h"


namespace Metasound
{
	namespace Engine
	{
		struct FInterfaceRegistryUClassOptions
		{
			FName ClassName;
			bool bIsDefault = false;
			bool bEditorCanAddOrRemove = false;
		};

		struct FInterfaceRegistryOptions
		{
			FName InputSystemName;
			TArray<FInterfaceRegistryUClassOptions> UClassOptions;
		};

		// Entry for registered interface.
		class FInterfaceRegistryEntry : public Frontend::IInterfaceRegistryEntry
		{
			const FInterfaceRegistryUClassOptions* FindOptionsByClassName(FName ClassName) const
			{
				auto NameIsEqual = [&ClassName](const FInterfaceRegistryUClassOptions& InOptions)
				{
					return InOptions.ClassName == ClassName;
				};

				return Options.UClassOptions.FindByPredicate(NameIsEqual);
			}

			static bool SortBindingsByConnectionPriorityPredicate(const FMetasoundFrontendInterfaceBinding& A, const FMetasoundFrontendInterfaceBinding& B)
			{
				return A.ConnectionPriority < B.ConnectionPriority;
			}

		public:
			FInterfaceRegistryEntry(
				const FMetasoundFrontendInterface& InInterface,
				TUniquePtr<Frontend::IDocumentTransform>&& InUpdateTransform,
				FInterfaceRegistryOptions&& InOptions
			)
				: Interface(InInterface)
				, UpdateTransform(MoveTemp(InUpdateTransform))
				, Options(MoveTemp(InOptions))
			{
			}

			virtual void AddOutputBindings(TArray<FMetasoundFrontendInterfaceBinding>&& InBindings) override
			{
				OutputBindings.Append(MoveTemp(InBindings));
				OutputBindings.Sort(&SortBindingsByConnectionPriorityPredicate);
			}

			virtual bool EditorCanAddOrRemove(FName InUClassName) const override
			{
				if (const FInterfaceRegistryUClassOptions* UClassOptions = FindOptionsByClassName(InUClassName))
				{
					return UClassOptions->bEditorCanAddOrRemove;
				}

				return false;
			}

			virtual bool UClassIsSupported(FName InUClassName) const override
			{
				if (const FInterfaceRegistryUClassOptions* UClassOptions = FindOptionsByClassName(InUClassName))
				{
					return true;
				}

				// TODO: Support child asset class types.
				return false;
			}

			virtual bool IsDefault(FName InUClassName) const override
			{
				if (const FInterfaceRegistryUClassOptions* UClassOptions = FindOptionsByClassName(InUClassName))
				{
					return UClassOptions->bIsDefault;
				}

				return false;
			}

			virtual FName GetRouterName() const override
			{
				return Options.InputSystemName;
			}

			virtual const FMetasoundFrontendInterface& GetInterface() const override
			{
				return Interface;
			}

			virtual const TArray<FMetasoundFrontendInterfaceBinding>& GetOutputBindings() const override
			{
				return OutputBindings;
			}

			virtual bool RemoveOutputBinding(const FMetasoundFrontendVersion& InInterfaceVersion) override
			{
				return OutputBindings.RemoveAll([&InInterfaceVersion](const FMetasoundFrontendInterfaceBinding& Binding)
				{
					return Binding.Version == InInterfaceVersion;
				}) > 0;
			}

			virtual void ResetOutputBindings()
			{
				OutputBindings.Reset();
			}

			virtual bool UpdateRootGraphInterface(Frontend::FDocumentHandle InDocument) const override
			{
				if (UpdateTransform.IsValid())
				{
					return UpdateTransform->Transform(InDocument);
				}
				return false;
			}

		private:
			FMetasoundFrontendInterface Interface;
			TArray<FMetasoundFrontendInterfaceBinding> OutputBindings;
			TUniquePtr<Frontend::IDocumentTransform> UpdateTransform;
			FInterfaceRegistryOptions Options;
		};

		FMetasoundFrontendInterface ConvertParameterToFrontendInterface(const Audio::FParameterInterface& InInterface)
		{
			using namespace Frontend;

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

				return ConvertParameterToDataType(ParamType);
			};

			FMetasoundFrontendInterface FrontendInterface;
			FrontendInterface.Version = { InInterface.GetName(), FMetasoundFrontendVersionNumber { InInterface.GetVersion().Major, InInterface.GetVersion().Minor } };

			// Transfer all input data from AudioExtension interface struct to FrontendInterface
			{
				Algo::Transform(InInterface.GetInputs(), FrontendInterface.Inputs, [&](const Audio::FParameterInterface::FInput& Input)
				{
					FMetasoundFrontendClassInput ClassInput;
					ClassInput.Name = Input.InitValue.ParamName;
					ClassInput.DefaultLiteral = FMetasoundFrontendLiteral(Input.InitValue);
					ClassInput.TypeName = ResolveMemberDataType(Input.DataType, Input.InitValue.ParamType);

#if WITH_EDITOR
					// Interfaces should never serialize text to avoid desync between
					// copied versions serialized in assets and those defined in code.
					ClassInput.Metadata.SetSerializeText(false);
					ClassInput.Metadata.SetDisplayName(Input.DisplayName);
					ClassInput.Metadata.SetDescription(Input.Description);
					ClassInput.Metadata.SortOrderIndex = Input.SortOrderIndex;
					
					FrontendInterface.AddSortOrderToInputStyle(Input.SortOrderIndex);

					// Setup required inputs by telling the style that the input is required
					// This will later be validated against.
					if (!Input.RequiredText.IsEmpty())
					{
						FrontendInterface.AddRequiredInputToStyle(Input.InitValue.ParamName, Input.RequiredText);
					}
#endif // WITH_EDITOR

					ClassInput.VertexID = FGuid::NewGuid();

					return ClassInput;
				});
			}

			// Transfer all output data from AudioExtension interface struct to FrontendInterface
			{
				Algo::Transform(InInterface.GetOutputs(), FrontendInterface.Outputs, [&](const Audio::FParameterInterface::FOutput& Output)
				{
					FMetasoundFrontendClassOutput ClassOutput;
					ClassOutput.Name = Output.ParamName;
					ClassOutput.TypeName = ResolveMemberDataType(Output.DataType, Output.ParamType);

#if WITH_EDITOR
					// Interfaces should never serialize text to avoid desync between
					// copied versions serialized in assets and those defined in code.
					ClassOutput.Metadata.SetSerializeText(false);
					ClassOutput.Metadata.SetDisplayName(Output.DisplayName);
					ClassOutput.Metadata.SetDescription(Output.Description);
					ClassOutput.Metadata.SortOrderIndex = Output.SortOrderIndex;
					
					FrontendInterface.AddSortOrderToOutputStyle(Output.SortOrderIndex);

					// Setup required outputs by telling the style that the output is required
					// This will later be validated against.
					if (!Output.RequiredText.IsEmpty())
					{
						FrontendInterface.AddRequiredOutputToStyle(Output.ParamName, Output.RequiredText);
					}
#endif // WITH_EDITOR

					ClassOutput.VertexID = FGuid::NewGuid();

					return ClassOutput;
				});
			}

			Algo::Transform(InInterface.GetEnvironment(), FrontendInterface.Environment, [&](const Audio::FParameterInterface::FEnvironmentVariable& Environment)
			{
				FMetasoundFrontendClassEnvironmentVariable EnvironmentVariable;
				EnvironmentVariable.Name = Environment.ParamName;

				// Disabled as it isn't used to infer type when getting/setting at a lower level.
				// TODO: Either remove type info for environment variables all together or enforce type.
				// EnvironmentVariable.TypeName = ResolveMemberDataType(Environment.DataType, Environment.ParamType);

				return EnvironmentVariable;
			});

			return FrontendInterface;
		}

		void RegisterUClasses()
		{
			IMetasoundUObjectRegistry::Get().RegisterUClass(MakeUnique<TMetasoundUObjectRegistryEntry<UMetaSoundPatch>>());
			IMetasoundUObjectRegistry::Get().RegisterUClass(MakeUnique<TMetasoundUObjectRegistryEntry<UMetaSoundSource>>());
		}

		void RegisterInterface(Audio::FParameterInterfacePtr Interface, TUniquePtr<Frontend::IDocumentTransform>&& InUpdateTransform, FInterfaceRegistryOptions&& InOptions)
		{
			using namespace Frontend;

			const FMetasoundFrontendInterface FrontendInterface = ConvertParameterToFrontendInterface(*Interface);
			IInterfaceRegistry::Get().RegisterInterface(MakeUnique<FInterfaceRegistryEntry>(FrontendInterface, MoveTemp(InUpdateTransform), MoveTemp(InOptions)));
		}

		void RegisterInterface(const FMetasoundFrontendInterface& InInterface, TUniquePtr<Frontend::IDocumentTransform>&& InUpdateTransform, FInterfaceRegistryOptions&& InOptions)
		{
			using namespace Frontend;
			IInterfaceRegistry::Get().RegisterInterface(MakeUnique<FInterfaceRegistryEntry>(InInterface, MoveTemp(InUpdateTransform), MoveTemp(InOptions)));
		}

		template <typename UClassType>
		void RegisterInterfaceForSingleClass(Audio::FParameterInterfacePtr Interface, TUniquePtr<Frontend::IDocumentTransform>&& InUpdateTransform, bool bInIsDefault = false, bool bInEditorCanAddOrRemove = false, FName InRouterName = IDataReference::RouterName)
		{
			using namespace Frontend;

			const FMetasoundFrontendInterface FrontendInterface = ConvertParameterToFrontendInterface(*Interface);
			RegisterInterface(FrontendInterface, MoveTemp(InUpdateTransform), FInterfaceRegistryOptions
			{
				InRouterName,
				{
					{
						UClassType::StaticClass()->GetFName(),
						bInIsDefault,
						bInEditorCanAddOrRemove
					}
				}
			});
		}

		template <typename UClassType>
		void RegisterInterfaceForSingleClass(const FMetasoundFrontendInterface& InInterface, TUniquePtr<Frontend::IDocumentTransform>&& InUpdateTransform, bool bInIsDefault = false, bool bInEditorCanAddOrRemove = false, FName InRouterName = IDataReference::RouterName)
		{
			using namespace Frontend;

			RegisterInterface(InInterface, MoveTemp(InUpdateTransform), FInterfaceRegistryOptions
			{
				InRouterName,
				{
					{
						UClassType::StaticClass()->GetFName(),
						bInIsDefault,
						bInEditorCanAddOrRemove
					}
				}
			});
		}

		void RegisterAudioFormatInterfaces()
		{
			using namespace Frontend;

			const FName SourceClassName = UMetaSoundSource::StaticClass()->GetFName();
			const FName PatchClassName = UMetaSoundPatch::StaticClass()->GetFName();

			// Input Formats
			{
				RegisterInterface(InputFormatMonoInterface::CreateInterface(), nullptr, FInterfaceRegistryOptions
				{
					IDataReference::RouterName,
					{
						{ PatchClassName, false /* bIsDefault */, true /* bEditorCanAddOrRemove */ },
						{ SourceClassName },
					}
				});
				IInterfaceRegistry::Get().AddInterfaceOutputBindings(InputFormatMonoInterface::GetVersion(), InputFormatMonoInterface::CreateOutputBindings());


				RegisterInterface(InputFormatStereoInterface::CreateInterface(), nullptr, FInterfaceRegistryOptions
				{
					IDataReference::RouterName,
					{
						{ PatchClassName, false /* bIsDefault */, true /* bEditorCanAddOrRemove */ },
						{ SourceClassName },
					}
				});
				IInterfaceRegistry::Get().AddInterfaceOutputBindings(InputFormatStereoInterface::GetVersion(), InputFormatStereoInterface::CreateOutputBindings());
			}

			// Output Formats
			{
				RegisterInterface(OutputFormatMonoInterface::CreateInterface(), nullptr, FInterfaceRegistryOptions
				{
					IDataReference::RouterName,
					{
						{ PatchClassName, false /* bIsDefault */, true /* bEditorCanAddOrRemove */ },
						{ SourceClassName, true /* bIsDefault */, false /* bEditorCanAddOrRemove */ },
					}
				});

				RegisterInterface(OutputFormatStereoInterface::CreateInterface(), nullptr, FInterfaceRegistryOptions
				{
					IDataReference::RouterName,
					{
						{ PatchClassName, false /* bIsDefault */, true /* bEditorCanAddOrRemove */ },
						{ SourceClassName },
					}
				});

				RegisterInterface(OutputFormatQuadInterface::CreateInterface(), nullptr, FInterfaceRegistryOptions
				{
					IDataReference::RouterName,
					{
						{ PatchClassName, false /* bIsDefault */, true /* bEditorCanAddOrRemove */ },
						{ SourceClassName },
					}
				});

				RegisterInterface(OutputFormatFiveDotOneInterface::CreateInterface(), nullptr, FInterfaceRegistryOptions
				{
					IDataReference::RouterName,
					{
						{ PatchClassName, false /* bIsDefault */, true /* bEditorCanAddOrRemove */ },
						{ SourceClassName },
					}
				});

				RegisterInterface(OutputFormatSevenDotOneInterface::CreateInterface(), nullptr, FInterfaceRegistryOptions
				{
					IDataReference::RouterName,
					{
						{ PatchClassName, false /* bIsDefault */, true /* bEditorCanAddOrRemove */ },
						{ SourceClassName },
					}
				});
			}
		}

		void RegisterDeprecatedInterfaces()
		{
			using namespace Frontend;

			RegisterInterfaceForSingleClass<UMetaSoundSource>(SourceInterfaceV1_0::CreateInterface(), nullptr);

			// Set default interface with unset version to use base UMetaSoundPatch class implementation (legacy requirement for 5.0 alpha).
			RegisterInterfaceForSingleClass<UMetaSoundPatch>(FMetasoundFrontendInterface(), nullptr);

			RegisterInterfaceForSingleClass<UMetaSoundSource>(MetasoundOutputFormatStereoV1_0::GetInterface(), nullptr);
			RegisterInterfaceForSingleClass<UMetaSoundSource>(MetasoundOutputFormatStereoV1_1::GetInterface(), MakeUnique<MetasoundOutputFormatStereoV1_1::FUpdateInterface>());
			RegisterInterfaceForSingleClass<UMetaSoundSource>(MetasoundOutputFormatStereoV1_2::GetInterface(), MakeUnique<MetasoundOutputFormatStereoV1_2::FUpdateInterface>());

			RegisterInterfaceForSingleClass<UMetaSoundSource>(MetasoundOutputFormatMonoV1_0::GetInterface(), nullptr);
			RegisterInterfaceForSingleClass<UMetaSoundSource>(MetasoundOutputFormatMonoV1_1::GetInterface(), MakeUnique<MetasoundOutputFormatMonoV1_1::FUpdateInterface>());
			RegisterInterfaceForSingleClass<UMetaSoundSource>(MetasoundOutputFormatMonoV1_2::GetInterface(), MakeUnique<MetasoundOutputFormatMonoV1_2::FUpdateInterface>());
		}

		void RegisterExternalInterfaces()
		{
			// Register External Interfaces (Interfaces defined externally & can be managed directly by end-user).
			auto RegisterExternalInterface = [](Audio::FParameterInterfacePtr Interface)
			{
				FInterfaceRegistryOptions Options { Audio::IParameterTransmitter::RouterName };

				// Currently, no externally defined interfaces can be added as default for protection against undesired
				// interfaces automatically being added when creating a new MetaSound asset through the editor. Also,
				// all parameter interfaces are enabled in the editor for addition/removal.
				constexpr bool bIsDefault = false;
				constexpr bool bEditorCanAddOrRemove = true;

				bool bInterfaceSupportsPatch = true;
				bool bInterfaceSupportsSource = true;

				const TArray<const UClass*> SupportedUClasses = Interface->FindSupportedUClasses();
				if (!SupportedUClasses.IsEmpty())
				{
					bInterfaceSupportsPatch = false;
					bInterfaceSupportsSource = false;
					for (const UClass* SupportedUClass : SupportedUClasses)
					{
						const UClass* PatchClass = UMetaSoundPatch::StaticClass();
						check(PatchClass);
						bInterfaceSupportsPatch |= PatchClass->IsChildOf(SupportedUClass);

						const UClass* SourceClass = UMetaSoundSource::StaticClass();
						check(SourceClass);
						bInterfaceSupportsSource |= SourceClass->IsChildOf(SupportedUClass);
					}
				}

				const bool bSupported = bInterfaceSupportsSource || bInterfaceSupportsPatch;
				if (ensureAlwaysMsgf(bSupported, TEXT("Parameter interface '%s' type not supported by MetaSounds"), *Interface->GetName().ToString()))
				{
					const FMetasoundFrontendInterface FrontendInterface = ConvertParameterToFrontendInterface(*Interface);
					if (bInterfaceSupportsPatch)
					{
						const FName ClassName = UMetaSoundPatch::StaticClass()->GetFName();
						Options.UClassOptions.Add(FInterfaceRegistryUClassOptions { ClassName, bIsDefault, bEditorCanAddOrRemove });
					}

					if (bInterfaceSupportsSource)
					{
						const FName ClassName = UMetaSoundSource::StaticClass()->GetFName();
						Options.UClassOptions.Add(FInterfaceRegistryUClassOptions { ClassName, bIsDefault, bEditorCanAddOrRemove });
					}

					RegisterInterface(FrontendInterface, nullptr, MoveTemp(Options));
				}
			};

			Audio::IAudioParameterInterfaceRegistry::Get().IterateInterfaces(RegisterExternalInterface);
			Audio::IAudioParameterInterfaceRegistry::Get().OnRegistration(MoveTemp(RegisterExternalInterface));
		}

		void RegisterInterfaces()
		{
			using namespace Frontend;

			// Default Patch Interfaces (legacy)
			{
				RegisterInterfaceForSingleClass<UMetaSoundPatch>(MetasoundV1_0::GetInterface(), nullptr, true, false);
			}

			// Default Source Interfaces
			{
				RegisterInterfaceForSingleClass<UMetaSoundSource>(SourceInterface::CreateInterface(), MakeUnique<SourceInterface::FUpdateInterface>(), true, false);
				RegisterInterfaceForSingleClass<UMetaSoundSource>(SourceOneShotInterface::CreateInterface(), nullptr, true, true);
			}

			RegisterAudioFormatInterfaces();
			RegisterDeprecatedInterfaces();
			RegisterExternalInterfaces();
		}
	} // namespace Engine
} // namespace Metasound
