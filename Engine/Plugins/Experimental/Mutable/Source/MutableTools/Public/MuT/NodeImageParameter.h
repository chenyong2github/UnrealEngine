// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImage.h"
#include "MuR/Image.h"


namespace mu
{

    class NodeImageParameter;
    typedef Ptr<NodeImageParameter> NodeImageParameterPtr;
    typedef Ptr<const NodeImageParameter> NodeImageParameterPtrConst;


    //! Node that defines a Image model parameter.
	//! \ingroup model
    class MUTABLETOOLS_API NodeImageParameter : public NodeImage
	{
	public:

		//-----------------------------------------------------------------------------------------
		// Life cycle
		//-----------------------------------------------------------------------------------------

        NodeImageParameter();

		void SerialiseWrapper(OutputArchive& arch) const override;
		static void Serialise( const NodeImageParameter* pNode, OutputArchive& arch );
        static NodeImageParameterPtr StaticUnserialise( InputArchive& arch );


		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

		NodePtr Clone( NodeMapPtr pMap = 0 ) const override;

		const NODE_TYPE* GetType() const override;
		static const NODE_TYPE* GetStaticType();

		int GetInputCount() const override;
		Node* GetInputNode( int i ) const override;
		void SetInputNode( int i, NodePtr pNode ) override;

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Get the name of the parameter. It will be exposed in the final compiled data.
		const char* GetName() const;

		//! Set the name of the parameter.
		void SetName( const char* );

		//! Get the uid of the parameter. It will be exposed in the final compiled data.
		const char* GetUid() const;

		//! Set the uid of the parameter.
		void SetUid( const char* );

		//! Get the default value of the parameter.
        //ImagePtrConst GetDefaultValue() const;

		//! Set the default value of the parameter.
        //void SetDefaultValue( ImagePtr );

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;
        Node::Private* GetBasePrivate() const override;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
        ~NodeImageParameter();

	private:

		Private* m_pD;

	};


}
