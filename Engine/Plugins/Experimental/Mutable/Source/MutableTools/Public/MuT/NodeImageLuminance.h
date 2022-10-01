// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImage.h"

#include "MuR/Image.h"


namespace mu
{

	// Forward definitions
	class NodeImageLuminance;
	typedef Ptr<NodeImageLuminance> NodeImageLuminancePtr;
	typedef Ptr<const NodeImageLuminance> NodeImageLuminancePtrConst;


	//! Calculate the luminance of an image into a new single-channel image..
	//! \ingroup model
	class MUTABLETOOLS_API NodeImageLuminance : public NodeImage
	{
	public:

		//-----------------------------------------------------------------------------------------
		// Life cycle
		//-----------------------------------------------------------------------------------------

		NodeImageLuminance();

		void SerialiseWrapper(OutputArchive& arch) const override;
		static void Serialise( const NodeImageLuminance* pNode, OutputArchive& arch );
		static NodeImageLuminancePtr StaticUnserialise( InputArchive& arch );


		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        NodePtr Clone( NodeMapPtr pMap = 0 ) const override;

        const NODE_TYPE* GetType() const override;
		static const NODE_TYPE* GetStaticType();

        virtual int GetInputCount() const override;
        virtual Node* GetInputNode( int i ) const override;
        void SetInputNode( int i, NodePtr pNode ) override;

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Get the node generating the source image calculate the luminance from.
		NodeImagePtr GetSource() const;
		void SetSource( NodeImagePtr );

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;
        Node::Private* GetBasePrivate() const override;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeImageLuminance();

	private:

		Private* m_pD;

	};


}
