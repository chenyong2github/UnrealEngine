// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADData.h"

#include "A3DSDKErrorCodes.h"
#include "A3DSDKInitializeFunctions.h"

namespace TechSoft
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

	template< class ObjectType, class PtrType>
	class TUniqueTSObj
	{
	public:

		/**
		 * Constructor of an initialized ObjectType object
		 * @param InGetter: Function Pointer of the A3DXXXXXXGet function.
		 */
		explicit TUniqueTSObj(A3DStatus (*InGetter)(const PtrType*, ObjectType*))
		{
			Getter = InGetter;
			memset(&Data, 0, sizeof(Data)); 
			Data.m_usStructSize = sizeof(Data);
		}

		/**
		 * Constructor of an filled ObjectType object with the data of DataPtr
		 * @param DataPtr: the pointer of the data to copy
		 * @param InGetter: Function pointer of the A3DXXXXXXGet function.
		 */
		explicit TUniqueTSObj(const PtrType* DataPtr, A3DStatus (*InGetter)(const PtrType*, ObjectType*))
			:TUniqueTSObj(InGetter)
		{
			Get(DataPtr);
		}

		/**
		 * Constructor of an filled ObjectType object with the data of DataPtr
		 * This type of structure need a specific initialization method
		 * @param DataPtr: the pointer of the data to copy
		 * @param InGetter: Function pointer of the A3DXXXXXXGet function.
		 * @param Initializer: Initialization function pointer for ObjectType.
		 */
		explicit TUniqueTSObj(A3DStatus(*InGetter)(const PtrType*, ObjectType*), void(*Initializer)(ObjectType&))
		{
			Getter = InGetter;
			Initializer(Data);
		}

		/**
		 * Constructor of an initialized ObjectType object
		 * This type of structure need a specific initialization method
		 * @param DataPtr: the pointer of the data to copy
		 * @param InGetter: Function pointer of the A3DXXXXXXGet function.
		 * @param Initializer: Initialization function pointer for ObjectType.
		 */
		explicit TUniqueTSObj(const PtrType* DataPtr, A3DStatus(*InGetter)(const PtrType*, ObjectType*), void(*Initializer)(ObjectType&))
			:TUniqueTSObj(InGetter, Initializer)
		{
			Get(DataPtr);
		}

		~TUniqueTSObj()
		{
			Getter(NULL, &Data);
		}

		/**
		 * Fill the structure with the data of a new DataPtr
		 */
		A3DStatus Get(const PtrType* DataPtr)
		{
			if (IsValid())
			{
				Status = Getter(NULL, &Data);
			}
			else
			{
				Status = A3DStatus::A3D_SUCCESS;
			}

			if (!IsValid() || (DataPtr == NULL))
			{
				Status = A3DStatus::A3D_ERROR;
				return Status;
			}

			Status = Getter(DataPtr, &Data);
			return Status;
		}

		/**
		 * Empty the structure
		 */
		void Reset()
		{
			Get(NULL);
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
		TUniqueTSObj(const TUniqueTSObj&) = delete;
		TUniqueTSObj& operator=(const TUniqueTSObj&) = delete;

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
			Get(NULL);
			Status = A3DStatus::A3D_SUCCESS;
			return &Data;
		}


	private:
		ObjectType Data;
		A3DStatus Status = A3DStatus::A3D_ERROR;
		A3DStatus (*Getter)(const PtrType*, ObjectType*);
	};

}
