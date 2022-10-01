// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImage.h"


namespace mu
{

	// Forward definitions
	class NodeImageInvert;
	typedef Ptr<NodeImageInvert> NodeImageInvertPtr;
	typedef Ptr<const NodeImageInvert> NodeImageInvertPtrConst;

	//! Node that inverts the colors of an image, channel by channel
	//! \ingroup model

	class MUTABLETOOLS_API NodeImageInvert : public NodeImage
	{
	public:

		//-----------------------------------------------------------------------------------------
		// Life cycle
		//-----------------------------------------------------------------------------------------

		NodeImageInvert();

		static void Serialise(const NodeImageInvert* pNode, OutputArchive& arch);
		void SerialiseWrapper(OutputArchive& arch) const override;
		static NodeImageInvertPtr StaticUnserialise(InputArchive& arch);


		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

		NodePtr Clone(NodeMapPtr pMap = 0) const override;

		const NODE_TYPE* GetType() const override;
		static const NODE_TYPE* GetStaticType();

		virtual int GetInputCount() const override;
		virtual Node* GetInputNode(int i)const override;
		void SetInputNode(int i, NodePtr pNode) override;

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Base image to invert.
		NodeImagePtr GetBase() const;
		void SetBase(NodeImagePtr);

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;
		Node::Private*GetBasePrivate()const override;

	protected:

		//! Forbidden. Manage with the Ptr<> template
		~NodeImageInvert();

	private:

		Private* m_pD;
	};
}
