// Copyright Epic Games, Inc. All Rights Reserved.


#include "MutableTools/Public/NodeString.h"

#include "MutableTools/Public/Node.h"
#include "MutableTools/Private/NodePrivate.h"

#include "MutableTools/Public/NodeStringConstant.h"
#include "MutableTools/Public/NodeStringParameter.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	static NODE_TYPE s_nodeStringType = NODE_TYPE( "NodeString", Node::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	void NodeString::Serialise( const NodeString* p, OutputArchive& arch )
	{
        uint32_t ver = 0;
		arch << ver;


#define SERIALISE_CHILDREN( C, ID ) \
        ( const C* pTyped##ID = dynamic_cast<const C*>(p) )			\
        {                                                           \
            arch << (uint32_t)ID;                                   \
            C::Serialise( pTyped##ID, arch );						\
        }                                                           \

        if SERIALISE_CHILDREN( NodeStringConstant                   , 0 )
        else if SERIALISE_CHILDREN( NodeStringParameter             , 1 )
        else check(false);

#undef SERIALISE_CHILDREN
    }

    //---------------------------------------------------------------------------------------------
	NodeStringPtr NodeString::StaticUnserialise( InputArchive& arch )
	{
        uint32_t ver;
		arch >> ver;
		check( ver == 0 );

        uint32_t id;
		arch >> id;

		switch (id)
		{
		case 0 :  return NodeStringConstant::StaticUnserialise( arch ); break;
		case 1 :  return NodeStringParameter::StaticUnserialise( arch ); break;
        default : check(false);
		}

		return 0;
	}


	//---------------------------------------------------------------------------------------------
	const NODE_TYPE* NodeString::GetType() const
	{
		return GetStaticType();
	}


	//---------------------------------------------------------------------------------------------
	const NODE_TYPE* NodeString::GetStaticType()
	{
		return &s_nodeStringType;
	}


}


