// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "MuT/ErrorLog.h"

#include "MuR/ModelPrivate.h"
#include "MuR/Operations.h"

#include <memory>

namespace mu
{
	MUTABLETOOLS_API extern const TCHAR* s_opNames[(int)OP_TYPE::COUNT];

	class ErrorLog::Private : public Base
	{
	public:

		Private()
		{
		}

        struct FErrorData
        {
            TArray< float > m_unassignedUVs;
        };

		struct FMessage
		{
			ErrorLogMessageType m_type = ELMT_NONE;
			FString m_text;
            TSharedPtr<FErrorData> m_data;
			const void* m_context = nullptr;
		};

		TArray<FMessage> m_messages;


		//-----------------------------------------------------------------------------------------

		//!
		void Add(const FString& Message, ErrorLogMessageType Type, const void* Context);

        //!
        void Add(const FString& InMessage, const ErrorLogMessageAttachedDataView& data, ErrorLogMessageType type, const void* context );
	};


    extern const TCHAR* GetOpName( OP_TYPE type );

}

