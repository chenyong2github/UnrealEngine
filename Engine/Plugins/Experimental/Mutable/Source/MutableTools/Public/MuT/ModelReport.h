// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/RefCounted.h"
#include "MuR/Ptr.h"
#include "MuR/Types.h"


namespace mu
{
	// Forward declarations
	class ModelReport;
	typedef Ptr<ModelReport> ModelReportPtr;
	typedef Ptr<const ModelReport> ModelReportPtrConst;

	//! Class tha stores the additional information of a compiled model.
	class MUTABLETOOLS_API ModelReport : public RefCounted
	{
	public:

		//!
		ModelReport();

		//! Return the name of an image of a particular state.
		//! If the state didn't target render-time generation, a null pointer is returned.
		int GetStateCount() const;

		//! Return the name of an image of a particular state.
		//! If the state didn't target render-time generation, a null pointer is returned.
		int GetLODCount( int state ) const;

		//! Return the name of an image of a particular state.
		//! If the state didn't target render-time generation, a null pointer is returned.
		int GetComponentCount( int state, int lod ) const;

		//! Return the name of an image of a particular state.
		//! If the state didn't target render-time generation, a null pointer is returned.
		const char* GetComponentName( int state, int lod, int comp ) const;

		//! Return the name of an image of a particular state.
		//! If the state didn't target render-time generation, a null pointer is returned.
		int GetComponentImageCount( int state, int lod, int comp ) const;

		//! Return the name of an image of a particular state.
		//! If the state didn't target render-time generation, a null pointer is returned.
		const char* GetImageName( int state, int lod, int comp, int image ) const;

		//! Return the gpu source code that generates the value for an image in a particular state.
		//! If the state didn't target render-time generation, a null pointer is returned.
		const char* GetImageFragmentCode( int state, int lod, int comp, int image ) const;

		//!
		int GetFragmentSourceImageCount( int state, int lod, int comp, int image ) const;

		//!
		const char* GetFragmentSourceImageName( int state, int lod, int comp, int image, int source ) const;

		//!
		int GetFragmentSourceVectorCount( int state, int lod, int comp, int image ) const;

		//!
		const char* GetFragmentSourceVectorName( int state, int lod, int comp, int image, int source ) const;

		//!
		int GetFragmentSourceScalarCount( int state, int lod, int comp, int image ) const;

		//!
		const char* GetFragmentSourceScalarName( int state, int lod, int comp, int image, int source ) const;

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~ModelReport();

	private:

		Private* m_pD;

	};

}
