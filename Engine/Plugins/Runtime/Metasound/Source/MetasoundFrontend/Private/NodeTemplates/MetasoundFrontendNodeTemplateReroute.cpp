// Copyright Epic Games, Inc. All Rights Reserved.

#include "NodeTemplates/MetasoundFrontendNodeTemplateReroute.h"

#include "Algo/AllOf.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendRegistries.h"
#include "Algo/AnyOf.h"


namespace Metasound
{
	namespace Frontend
	{
		namespace ReroutePrivate
		{
			FConstOutputHandle FindReroutedOutput(FConstOutputHandle InOutputHandle)
			{
				using namespace Frontend;

				if (InOutputHandle->IsValid())
				{
					FConstNodeHandle NodeHandle = InOutputHandle->GetOwningNode();
					if (NodeHandle->IsValid())
					{
						if (NodeHandle->GetClassMetadata().GetClassName() == FRerouteNodeTemplate::ClassName)
						{
							TArray<FConstInputHandle> Inputs = NodeHandle->GetConstInputs();
							if (!Inputs.IsEmpty())
							{
								FConstInputHandle RerouteInputHandle = Inputs.Last();
								if (RerouteInputHandle->IsValid())
								{
									FConstOutputHandle ConnectedOutputHandle = RerouteInputHandle->GetConnectedOutput();
									return FindReroutedOutput(ConnectedOutputHandle);
								}
							}
						}
					}
				}

				return InOutputHandle;
			}

			void FindReroutedInputs(FConstInputHandle InHandleToCheck, TArray<FConstInputHandle>& InOutInputHandles)
			{
				using namespace Frontend;

				if (InHandleToCheck->IsValid())
				{
					FConstNodeHandle NodeHandle = InHandleToCheck->GetOwningNode();
					if (NodeHandle->IsValid())
					{
						if (NodeHandle->GetClassMetadata().GetClassName() == FRerouteNodeTemplate::ClassName)
						{
							TArray<FConstOutputHandle> Outputs = NodeHandle->GetConstOutputs();
							for (FConstOutputHandle& OutputHandle : Outputs)
							{
								TArray<FConstInputHandle> LinkedInputs = OutputHandle->GetConstConnectedInputs();
								for (FConstInputHandle LinkedInput : LinkedInputs)
								{
									FindReroutedInputs(LinkedInput, InOutInputHandles);
								}
							}

							return;
						}
					}

					InOutInputHandles.Add(InHandleToCheck);
				}
			}
		}

		const FMetasoundFrontendClassName FRerouteNodeTemplate::ClassName { "UE", "Reroute", "" };

		const FMetasoundFrontendVersion FRerouteNodeTemplate::Version { ClassName.GetFullName(), { 1, 0 } };

		bool FRerouteNodeTemplate::BuildTemplate(FMetasoundFrontendDocument& InOutDocument, FMetasoundFrontendGraph& InOutGraph, FMetasoundFrontendNode& InOutNode) const
		{
			if (!ensure(InOutNode.Interface.Inputs.Num() == 1))
			{
				return false;
			}
			const FMetasoundFrontendVertex& InputVertex = InOutNode.Interface.Inputs.Last();

			if (!ensure(InOutNode.Interface.Outputs.Num() == 1))
			{
				return false;
			}
			const FMetasoundFrontendVertex& OutputVertex = InOutNode.Interface.Outputs.Last();

			int32 SourceIndex = InOutGraph.Edges.IndexOfByPredicate([&](const FMetasoundFrontendEdge& Edge)
			{
				return Edge.ToNodeID == InOutNode.GetID() && Edge.ToVertexID == InputVertex.VertexID;
			});

			if (SourceIndex != INDEX_NONE)
			{
				for (FMetasoundFrontendEdge& Edge : InOutGraph.Edges)
				{
					if (Edge.FromNodeID == InOutNode.GetID() && Edge.FromVertexID == OutputVertex.VertexID)
					{
						Edge.FromNodeID = InOutGraph.Edges[SourceIndex].FromNodeID;
						Edge.FromVertexID = InOutGraph.Edges[SourceIndex].FromVertexID;
					}
				}
			}

			return true;
		}

		const FMetasoundFrontendClass& FRerouteNodeTemplate::GetFrontendClass() const
		{
			auto CreateFrontendClass = []()
			{
				FMetasoundFrontendClass Class;
				Class.Metadata.SetClassName(ClassName);

#if WITH_EDITOR
				Class.Metadata.SetSerializeText(false);
				Class.Metadata.SetAuthor(Metasound::PluginAuthor);
				Class.Metadata.SetDescription(Metasound::PluginNodeMissingPrompt);

				FMetasoundFrontendClassStyleDisplay& StyleDisplay = Class.Style.Display;
				StyleDisplay.ImageName = "MetasoundEditor.Graph.Node.Class.Reroute";
				StyleDisplay.bShowInputNames = false;
				StyleDisplay.bShowOutputNames = false;
				StyleDisplay.bShowLiterals = false;
				StyleDisplay.bShowName = false;
#endif // WITH_EDITOR

				Class.Metadata.SetType(EMetasoundFrontendClassType::Template);
				Class.Metadata.SetVersion(Version.Number);


				return Class;
			};

			static const FMetasoundFrontendClass FrontendClass = CreateFrontendClass();
			return FrontendClass;
		}

		FMetasoundFrontendNodeInterface FRerouteNodeTemplate::CreateNodeInterfaceFromDataType(FName InDataType)
		{
			auto CreateNewVertex = [&] { return FMetasoundFrontendVertex { "Value", InDataType, FGuid::NewGuid() }; };

			FMetasoundFrontendNodeInterface NewInterface;
			NewInterface.Inputs.Add(CreateNewVertex());
			NewInterface.Outputs.Add(CreateNewVertex());

			return NewInterface;
		}

		const FNodeRegistryKey& FRerouteNodeTemplate::GetRegistryKey()
		{
			static const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(
				EMetasoundFrontendClassType::Template,
				ClassName.ToString(), 
				Version.Number.Major, 
				Version.Number.Minor);

			return RegistryKey;
		}

		const FMetasoundFrontendVersion& FRerouteNodeTemplate::GetVersion() const
		{
			return Version;
		}

#if WITH_EDITOR
		bool FRerouteNodeTemplate::HasRequiredConnections(FConstNodeHandle InNodeHandle) const
		{
			TArray<FConstOutputHandle> Outputs = InNodeHandle->GetConstOutputs();
			TArray<FConstInputHandle> Inputs = InNodeHandle->GetConstInputs();
			const bool bConnectedToNonRerouteOutputs = Algo::AnyOf(Outputs, [](const FConstOutputHandle& OutputHandle) { return ReroutePrivate::FindReroutedOutput(OutputHandle)->IsValid(); });
			const bool bConnectedToNonRerouteInputs = Algo::AnyOf(Inputs, [](const FConstInputHandle& InputHandle)
			{
				TArray<FConstInputHandle> Inputs;
				ReroutePrivate::FindReroutedInputs(InputHandle, Inputs);
				return !Inputs.IsEmpty();
			});

			return bConnectedToNonRerouteOutputs || bConnectedToNonRerouteOutputs == bConnectedToNonRerouteInputs;
		}
#endif // WITH_EDITOR

		bool FRerouteNodeTemplate::IsValidNodeInterface(const FMetasoundFrontendNodeInterface& InNodeInterface) const
		{
			if (InNodeInterface.Inputs.Num() != 1)
			{
				return false;
			}
			
			if (InNodeInterface.Outputs.Num() != 1)
			{
				return false;
			}

			const FName DataType = InNodeInterface.Inputs.Last().TypeName;
			if (DataType != InNodeInterface.Outputs.Last().TypeName)
			{
				return false;
			}

			return IDataTypeRegistry::Get().IsRegistered(DataType);
		}
	} // namespace Frontend
} // namespace Metasound