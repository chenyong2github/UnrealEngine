// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#ifdef USE_TECHSOFT_SDK

#include "TechSoftInterface.h"

namespace CADLibrary
{

// Single-ownership smart TeshSoft object
// Use this when you need to manage TechSoft object's lifetime.
//
// TechSoft give access to void pointers
// According to the context, the class name of the void pointer is known but the class is unknown
// i.e. A3DSDKTypes.h defines all type like :
// 	   typedef void A3DEntity;		
// 	   typedef void A3DAsmModelFile; ...
// 
// From a pointer, TechSoft give access to a copy of the associated structure :
//
// const A3DXXXXX* pPointer;
// A3DXXXXXData sData; // the structure
// A3D_INITIALIZE_DATA(A3DXXXXXData, sData); // initialization of the structure
// A3DXXXXXXGet(pPointer, &sData); // Copy of the data of the pointer in the structure
// ...
// A3DXXXXXXGet(NULL, &sData); // Free the structure
//
// A3D_INITIALIZE_DATA, and all A3DXXXXXXGet methods are TechSoft macro
//

template<class ObjectType, class IndexerType>
class TUniqueTSObjBase
{
public:

	/**
	 * Constructor of an initialized ObjectType object
	 */
	explicit TUniqueTSObjBase()
	{
		InitializeData();
	}

	/**
	 * Constructor of an filled ObjectType object with the data of DataPtr
	 * @param DataPtr: the pointer of the data to copy
	 */
	explicit TUniqueTSObjBase(IndexerType DataPtr)
	{
		//TechSoftInterfaceImpl::InitializeData(Data);
		InitializeData();

		Status = GetData(DataPtr);
	}

	~TUniqueTSObjBase()
	{
		ResetData();
	}

	/**
	 * Fill the structure with the data of a new DataPtr
	 */
	A3DStatus FillFrom(IndexerType DataPtr)
	{
		if (IsValid())
		{
			Status = ResetData();
		}
		else
		{
			Status = A3DStatus::A3D_SUCCESS;
		}

		if (!IsValid() || (DataPtr == DefaultValue))
		{
			Status = A3DStatus::A3D_ERROR;
			return Status;
		}

		Status = GetData(DataPtr);
		return Status;
	}

	template<typename... InArgTypes>
	A3DStatus FillWith(A3DStatus (*Getter)(const A3DEntity*, ObjectType*, InArgTypes&&... ), const A3DEntity* DataPtr, InArgTypes&&... Args)
	{
		if (IsValid())
		{
			Status = ResetData();
		}
		else
		{
			Status = A3DStatus::A3D_SUCCESS;
		}

		if (!IsValid() || (DataPtr == DefaultValue))
		{
			Status = A3DStatus::A3D_ERROR;
			return Status;
		}

		Status = Getter(DataPtr, &Data, Forward<InArgTypes>(Args)...);
		return Status;
	}

	/**
	 * Empty the structure
	 */
	void Reset()
	{
		ResetData();
	}

	/**
	 * Return
	 *  - A3DStatus::A3D_SUCCESS if the data is filled
	 *  - A3DStatus::A3D_ERROR if the data is empty
	 */
	A3DStatus GetStatus()
	{
		return Status;
	}

	/**
	 * Return true if the data is filled
	 */
	const bool IsValid() const
	{
		return Status == A3DStatus::A3D_SUCCESS;
	}

	// Non-copyable
	TUniqueTSObjBase(const TUniqueTSObjBase&) = delete;
	TUniqueTSObjBase& operator=(const TUniqueTSObjBase&) = delete;

	// Conversion methods

	const ObjectType& operator*() const
	{
		return Data;
	}

	ObjectType& operator*()
	{
		return Data;
	}

	const ObjectType* operator->() const
	{
		check(IsValid());
		return &Data;
	}

	ObjectType* operator->()
	{
		check(IsValid());
		return &Data;
	}

	/**
	 * Return the structure and set the status to filled
	 * The method is used to manage structure filled by UE
	 */
	ObjectType* GetEmptyDataPtr()
	{
		FillFrom(DefaultValue);
		Status = A3DStatus::A3D_SUCCESS;
		return &Data;
	}

private:
	ObjectType Data;
	A3DStatus Status = A3DStatus::A3D_ERROR;

	/**
	 * DefaultValue is used to initialize "Data" with GetData method
	 * According to IndexerType, the value is either nullptr for const A3DEntity* either something like "A3D_DEFAULT_MATERIAL_INDEX" ((A3DUns16)-1) for uint32
	 * @see ResetData
	 */
	static IndexerType DefaultValue;

	void InitializeData()
#ifdef USE_TECHSOFT_SDK
		;
#else
	{
		return A3DStatus::A3D_ERROR;
	}
#endif

	A3DStatus GetData(IndexerType AsmModelFilePtr);
#ifdef USE_TECHSOFT_SDK
	;
#else
	{
		return A3DStatus::A3D_ERROR;
	}
#endif

	A3DStatus ResetData()
	{
		return GetData(DefaultValue);
	}
};

template<class ObjectType>
using TUniqueTSObj = TUniqueTSObjBase<ObjectType, const A3DEntity*>;

template<class ObjectType>
using TUniqueTSObjFromIndex = TUniqueTSObjBase<ObjectType, uint32>;

template<class ObjectType, class IndexerType>
IndexerType TUniqueTSObjBase<ObjectType, IndexerType>::DefaultValue = (IndexerType) nullptr;

}

#endif