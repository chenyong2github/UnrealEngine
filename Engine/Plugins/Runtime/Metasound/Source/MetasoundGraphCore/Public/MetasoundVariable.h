// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataFactory.h"

namespace Metasound
{

	/** A MetaSound Variable contains a readable data reference.
	 *
	 * @tparam DataType - Underlying data type of data reference.
	 */
	template<typename DataType>
	struct TVariable
	{
		using FReadReference = TDataReadReference<DataType>;

		TVariable() = delete;
		TVariable(TVariable&&) = default;
		TVariable(const TVariable&) = default;

		/** Create a variable with a given data reference. */
		TVariable(FReadReference InDataReference)
		: DataReference(InDataReference)
		{
		}

		FReadReference GetDataReference() const
		{
			return DataReference;
		}

	private:

		FReadReference DataReference;
	};
	
	/** A MetaSound DelayedVariable contains a data reference's prior and current value.
	 *
	 * @tparam DataType - Underlying data type of data reference.
	 */
	template<typename DataType>
	struct TDelayedVariable
	{
		using FWriteReference = TDataWriteReference<DataType>;
		using FReadReference = TDataReadReference<DataType>;

		TDelayedVariable() = delete;
		TDelayedVariable(TDelayedVariable&&) = default;
		TDelayedVariable(const TDelayedVariable&) = default;


		/** Create a delayed variable.
		 *
		 * @param InDelayedReference - A writable reference which will hold the delayed
		 *                           version of the data reference.
		 */
		TDelayedVariable(FWriteReference InDelayedReference)
		: DelayedDataReference(InDelayedReference)
		, DataReference(InDelayedReference)
		{
		}

		void CopyReferencedData()
		{
			*DelayedDataReference = *DataReference;
		}

		void SetDataReference(FReadReference InDataReference)
		{
			DataReference = InDataReference;
		}

		/** Get the current data reference. */
		FReadReference GetDataReference() const
		{
			return DataReference;
		}

		/** Get the delayed data reference */
		FReadReference GetDelayedDataReference() const
		{
			return DelayedDataReference;
		}

	private:

		FWriteReference DelayedDataReference;
		FReadReference DataReference;
	};
}

