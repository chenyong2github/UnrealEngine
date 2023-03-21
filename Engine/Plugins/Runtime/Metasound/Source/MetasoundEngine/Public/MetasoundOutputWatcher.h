// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundOutput.h"
#include "Analysis/MetasoundFrontendAnalyzerView.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Templates/Function.h"

namespace Metasound::Private
{
	/**
	 * Provides a mechanism to check an output and notify subscribers when its value(s) change
	 */
	class METASOUNDENGINE_API FMetasoundOutputWatcher
	{
	public:
		FMetasoundOutputWatcher(Frontend::FAnalyzerAddress&& Address, const FOperatorSettings& OperatorSettings);

		const FName Name;
		
		/**
		 * Update the watched outputs
		 */
		void Update(TFunctionRef<void(FName, const FMetaSoundOutput&)> OnOutputChanged);

		using FOutputInitFn = TFunction<void(FMetaSoundOutput&)>;
		using FOutputUpdateFn = TFunction<bool(FMetasoundOutputWatcher&, FMetaSoundOutput&)>;

		/**
		 * Stores the typed operations for a specific supported type
		 */
		class IOutputTypeOperations
		{
		public:
			virtual ~IOutputTypeOperations() = default;
			
			virtual void Init(FMetaSoundOutput& Output) = 0;
			virtual bool Update(FMetasoundOutputWatcher& Watcher, FMetaSoundOutput& Output) = 0;
		};

		template<typename DataType>
		class TOutputTypeOperations final : public IOutputTypeOperations
		{
		public:
			explicit TOutputTypeOperations(DataType InDefaultValue)
				: DefaultValue(InDefaultValue)
			{}
			
			const DataType DefaultValue;
			
			virtual void Init(FMetaSoundOutput& Output) override
			{
				Output.Init<DataType>(DefaultValue);
			}
			
			virtual bool Update(FMetasoundOutputWatcher& Watcher, FMetaSoundOutput& Output) override
			{
				return Watcher.UpdateOutput<DataType>(Output);
			}

		private:
		};

		/**
		 * Register operations for a given type.
		 * NOTE: This is exposed to enable external registration of types.
		 * You shouldn't need to use this directly.
		 *
		 * @param TypeName - The name of the type, usually returned from GetMetasoundDataTypeName()
		 * @param OutputTypeOperations - The operations to use
		 */
		static void RegisterOutputTypeOperations(FName TypeName, TUniquePtr<IOutputTypeOperations>&& OutputTypeOperations);
	
	private:
		template<typename DataType>
		bool UpdateOutput(FMetaSoundOutput& Output)
		{
			// check the type
			if (!Output.IsType<DataType>())
			{
				return false;
			}

			// try to get the latest value
			DataType Value;

			if (View.TryGetOutputData(Output.Name, Value))
			{
				Output.Set(Value);
				return true;
			}
			
			return false;
		}
		
		static TMap<FName, TUniquePtr<IOutputTypeOperations>> OutputTypeOperationMap;
		Frontend::FMetasoundAnalyzerView View;
		TArray<FMetaSoundOutput> Outputs;
	};
}


/** Helper to register typed operations on outputs */
#define METASOUND_PRIVATE_REGISTER_GENERATOR_OUTPUT_WATCHER_TYPE_OPERATIONS(TYPE, DEFAULT_VALUE) \
	{ \
		using FTypeOps = Metasound::Private::FMetasoundOutputWatcher::TOutputTypeOperations<TYPE>; \
		TUniquePtr<FTypeOps> Ops = MakeUnique<FTypeOps>(DEFAULT_VALUE); \
		Metasound::Private::FMetasoundOutputWatcher::RegisterOutputTypeOperations( \
			Metasound::GetMetasoundDataTypeName<TYPE>(), \
			MoveTemp(Ops) \
			); \
	} \
