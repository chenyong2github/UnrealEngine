// Copyright Epic Games, Inc. All Rights Reserved.
#include "Analysis/MetasoundFrontendAnalyzerRegistry.h"

#include "Analysis/MetasoundFrontendAnalyzerFactory.h"
#include "Analysis/MetasoundFrontendVertexAnalyzerEnvelopeFollower.h"
#include "Analysis/MetasoundFrontendVertexAnalyzerTriggerDensity.h"
#include "Analysis/MetasoundFrontendVertexAnalyzerValue.h"
#include "MetasoundPrimitives.h"


namespace Metasound
{
	namespace Frontend
	{
		class FVertexAnalyzerRegistry : public IVertexAnalyzerRegistry
		{
			TMap<FName, TUniquePtr<IVertexAnalyzerFactory>> AnalyzerFactoryRegistry;

			template<typename TAnalyzerFactoryClass>
			void RegisterAnalyzerFactory()
			{
				TUniquePtr<IVertexAnalyzerFactory> Factory(new TAnalyzerFactoryClass());
				AnalyzerFactoryRegistry.Emplace(TAnalyzerFactoryClass::GetAnalyzerName(), MoveTemp(Factory));
			}

		public:
			FVertexAnalyzerRegistry() = default;
			virtual ~FVertexAnalyzerRegistry() = default;

			virtual const TUniquePtr<IVertexAnalyzerFactory>& FindAnalyzerFactory(FName InAnalyzerName) const override
			{
				const TUniquePtr<IVertexAnalyzerFactory>* Factory = AnalyzerFactoryRegistry.Find(InAnalyzerName);
				if (ensureMsgf(Factory, TEXT("Failed to find registered MetaSound Analyzer Factory with name '%s'"), *InAnalyzerName.ToString()))
				{
					return *Factory;
				}

				static const TUniquePtr<IVertexAnalyzerFactory> InvalidFactory;
				return InvalidFactory;
			}

			virtual void RegisterAnalyzerFactories() override
			{
				RegisterAnalyzerFactory<FVertexAnalyzerEnvelopeFollower::FFactory>();
				RegisterAnalyzerFactory<FVertexAnalyzerTriggerDensity::FFactory>();

				// Primitives
				RegisterAnalyzerFactory<FVertexAnalyzerBool::FFactory>();
				RegisterAnalyzerFactory<FVertexAnalyzerFloat::FFactory>();
				RegisterAnalyzerFactory<FVertexAnalyzerInt::FFactory>();
				RegisterAnalyzerFactory<FVertexAnalyzerString::FFactory>();
			}
		};

		IVertexAnalyzerRegistry& IVertexAnalyzerRegistry::Get()
		{
			static FVertexAnalyzerRegistry Registry;
			return Registry;
		}
	} // namespace Frontend
} // namespace Metasound
