// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundVertexData.h"

namespace Metasound
{
	namespace MetasoundVertexDataPrivate
	{
		template<typename VertexType>
		void EmplaceBindings(TArray<TBinding<VertexType>>& InArray, const TVertexInterfaceGroup<VertexType>& InVertexInterface)
		{
			for (const VertexType& DataVertex : InVertexInterface)
			{
				InArray.Emplace(DataVertex);
			}
		}

		template<typename BindingType>
		BindingType* Find(TArray<BindingType>& InBindings, const FVertexName& InVertexName)
		{
			auto IsVertexWithName = [&InVertexName](const BindingType& InBinding)
			{
				return InBinding.GetVertex().VertexName == InVertexName;
			};
			return InBindings.FindByPredicate(IsVertexWithName);
		}

		template<typename BindingType>
		const BindingType* Find(const TArray<BindingType>& InBindings, const FVertexName& InVertexName)
		{
			auto IsVertexWithName = [&InVertexName](const BindingType& InBinding)
			{
				return InBinding.GetVertex().VertexName == InVertexName;
			};
			return InBindings.FindByPredicate(IsVertexWithName);
		}

		template<typename BindingType>
		void BindVertex(TArray<BindingType>& InBindings, const FVertexName& InVertexName, FAnyDataReference&& InDataReference)
		{
			if (BindingType* Binding = Find(InBindings, InVertexName))
			{
				if (Binding->GetVertex().DataTypeName == InDataReference.GetDataTypeName())
				{
					Binding->Bind(MoveTemp(InDataReference));
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Failed bind vertex with name '%s'. Supplied data type (%s) does not match vertex data type (%s)"), *InVertexName.ToString(), *InDataReference.GetDataTypeName().ToString(), *Binding->GetVertex().DataTypeName.ToString());
				}
			}
			else
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed find vertex with name '%s'. Failed to bind vertex data"), *InVertexName.ToString());
			}
		}

		template<typename BindingType>
		void Bind(TArray<BindingType>& InBindings, const FDataReferenceCollection& InCollection)
		{
			for (BindingType& Binding : InBindings)
			{
				const FDataVertex& Vertex = Binding.GetVertex();

				if (const FAnyDataReference* DataRef = InCollection.FindDataReference(Vertex.VertexName))
				{
					Binding.Bind(*DataRef);
				}
			}
		}

		template<typename BindingType>
		bool IsVertexBound(const TArray<BindingType>& InBindings, const FVertexName& InVertexName)
		{
			if (const BindingType* Binding = Find(InBindings, InVertexName))
			{
				return Binding->IsBound();
			}
			return false;
		}

		template<typename BindingType>
		bool AreAllVerticesBound(const TArray<BindingType>& InBindings)
		{
			return Algo::AllOf(InBindings, [](const BindingType& Binding) { return Binding.IsBound(); });
		}

		template<typename BindingType>
		EDataReferenceAccessType GetVertexDataAccessType(const TArray<BindingType>& InBindings, const FVertexName& InVertexName)
		{
			if (const BindingType* Binding = Find(InBindings, InVertexName))
			{
				return Binding->GetAccessType();
			}
			return EDataReferenceAccessType::None;
		}
	}

	FInputVertexInterfaceData::FInputVertexInterfaceData(const FInputVertexInterface& InVertexInterface)
	{
		MetasoundVertexDataPrivate::EmplaceBindings(Bindings, InVertexInterface);
	}

	
	void FInputVertexInterfaceData::BindVertex(const FVertexName& InVertexName, FAnyDataReference&& InDataReference)
	{
		MetasoundVertexDataPrivate::BindVertex(Bindings, InVertexName, MoveTemp(InDataReference));
	}

	void FInputVertexInterfaceData::Bind(const FDataReferenceCollection& InCollection)
	{
		MetasoundVertexDataPrivate::Bind(Bindings, InCollection);
	}

	bool FInputVertexInterfaceData::IsVertexBound(const FVertexName& InVertexName) const
	{
		return MetasoundVertexDataPrivate::IsVertexBound(Bindings, InVertexName);
	}

	EDataReferenceAccessType FInputVertexInterfaceData::GetVertexDataAccessType(const FVertexName& InVertexName) const
	{
		return MetasoundVertexDataPrivate::GetVertexDataAccessType(Bindings, InVertexName);
	}

	bool FInputVertexInterfaceData::AreAllVerticesBound() const
	{
		return MetasoundVertexDataPrivate::AreAllVerticesBound(Bindings);
	}

	FInputVertexInterfaceData::FBinding* FInputVertexInterfaceData::Find(const FVertexName& InVertexName)
	{
		return MetasoundVertexDataPrivate::Find(Bindings, InVertexName);
	}

	const FInputVertexInterfaceData::FBinding* FInputVertexInterfaceData::Find(const FVertexName& InVertexName) const
	{
		return MetasoundVertexDataPrivate::Find(Bindings, InVertexName);
	}

	FInputVertexInterfaceData::FBinding* FInputVertexInterfaceData::FindChecked(const FVertexName& InVertexName)
	{
		FBinding* Binding = Find(InVertexName);
		check(nullptr != Binding);
		return Binding;
	}

	const FInputVertexInterfaceData::FBinding* FInputVertexInterfaceData::FindChecked(const FVertexName& InVertexName) const
	{
		const FBinding* Binding = Find(InVertexName);
		check(nullptr != Binding);
		return Binding;
	}


	FOutputVertexInterfaceData::FOutputVertexInterfaceData(const FOutputVertexInterface& InVertexInterface)
	{
		MetasoundVertexDataPrivate::EmplaceBindings(Bindings, InVertexInterface);
	}

	
	void FOutputVertexInterfaceData::BindVertex(const FVertexName& InVertexName, FAnyDataReference&& InDataReference)
	{
		MetasoundVertexDataPrivate::BindVertex(Bindings, InVertexName, MoveTemp(InDataReference));
	}

	void FOutputVertexInterfaceData::Bind(const FDataReferenceCollection& InCollection)
	{
		MetasoundVertexDataPrivate::Bind(Bindings, InCollection);
	}

	bool FOutputVertexInterfaceData::IsVertexBound(const FVertexName& InVertexName) const
	{
		return MetasoundVertexDataPrivate::IsVertexBound(Bindings, InVertexName);
	}

	EDataReferenceAccessType FOutputVertexInterfaceData::GetVertexDataAccessType(const FVertexName& InVertexName) const
	{
		return MetasoundVertexDataPrivate::GetVertexDataAccessType(Bindings, InVertexName);
	}

	bool FOutputVertexInterfaceData::AreAllVerticesBound() const
	{
		return MetasoundVertexDataPrivate::AreAllVerticesBound(Bindings);
	}

	FOutputVertexInterfaceData::FBinding* FOutputVertexInterfaceData::Find(const FVertexName& InVertexName)
	{
		return MetasoundVertexDataPrivate::Find(Bindings, InVertexName);
	}

	const FOutputVertexInterfaceData::FBinding* FOutputVertexInterfaceData::Find(const FVertexName& InVertexName) const
	{
		return MetasoundVertexDataPrivate::Find(Bindings, InVertexName);
	}

	FOutputVertexInterfaceData::FBinding* FOutputVertexInterfaceData::FindChecked(const FVertexName& InVertexName)
	{
		FBinding* Binding = Find(InVertexName);
		check(nullptr != Binding);
		return Binding;
	}

	const FOutputVertexInterfaceData::FBinding* FOutputVertexInterfaceData::FindChecked(const FVertexName& InVertexName) const
	{
		const FBinding* Binding = Find(InVertexName);
		check(nullptr != Binding);
		return Binding;
	}
}
