// Copyright Epic Games, Inc. All Rights Reserved.

#include "TriangleReaderNative.h"

#include <Unknwn.h>

#include <vector>

#if !defined(Navisworks_API)
#error "Navisworks_API is undefined - won't find Navisworks assemblies and typelibs"
#endif

#define STRINGIFY_(x) #x
#define lcodieD(x) STRINGIFY_(x)
#import lcodieD(Navisworks_API/lcodieD.dll) rename_namespace("NavisworksIntegratedAPI") // renaming to omit API version(e.g. NavisworksIntegratedAPI17) 


using namespace DatasmithNavisworksUtilImpl;


// Implementation of callback object that goes to Naviswork's GenerateSimplePrimitives
// Although it's an IDispatch interface we don't need its mechanics implemented when
// calling GenerateSimplePrimitives directly from C++
class SimplePrimitivesCallback : public NavisworksIntegratedAPI::InwSimplePrimitivesCB {
public:

	TriangleReaderNative* Geometry;

	void ConvertCoord(NavisworksIntegratedAPI::InwSimpleVertex* SimpleVertex, std::vector<double>& Result)
	{
		ExtractVectorFromVariant(SimpleVertex->coord, Result, 3);
	}

	void ConvertNormal(NavisworksIntegratedAPI::InwSimpleVertex* SimpleVertex, std::vector<double>& Result)
	{
		ExtractVectorFromVariant(SimpleVertex->normal, Result, 3);
	}

	void ConvertUV(NavisworksIntegratedAPI::InwSimpleVertex* SimpleVertex, std::vector<double>& Result)
	{
		ExtractVectorFromVariant(SimpleVertex->tex_coord, Result, 2);
	}

	void ExtractVectorFromVariant(const _variant_t& variant, std::vector<double>& Result, int Count)
	{
		SAFEARRAY* ComArray = variant.parray;
		HRESULT Hr;
		if (SUCCEEDED(Hr = SafeArrayLock(ComArray)))
		{
			FLOAT* Array = static_cast<FLOAT*>(ComArray->pvData);
			for(int I=0; I != Count; ++I)
			{
				Result.push_back(Array[I]);
			}
			Hr = SafeArrayUnlock(ComArray);
		}
	}

	void AddVertex(NavisworksIntegratedAPI::InwSimpleVertex* v)
	{
		Geometry->VertexCount += 1;
		
		ConvertCoord(v, Geometry->Coords);
		ConvertNormal(v, Geometry->Normals);
		ConvertUV(v, Geometry->UVs);
	}

	HRESULT raw_Triangle(NavisworksIntegratedAPI::InwSimpleVertex* V1, NavisworksIntegratedAPI::InwSimpleVertex* V2, NavisworksIntegratedAPI::InwSimpleVertex* V3) override
	{
		const int BaseIndex = Geometry->VertexCount;

		AddVertex(V1);
		AddVertex(V2);
		AddVertex(V3);

		for (int I = 0; I < 3; ++I)
		{
			Geometry->Indices.push_back(BaseIndex + I);
		}
		Geometry->TriangleCount++;

		return S_OK;
	}

	HRESULT raw_Line(NavisworksIntegratedAPI::InwSimpleVertex* v1, NavisworksIntegratedAPI::InwSimpleVertex* v2) override
	{
		return S_OK;
	}

	HRESULT raw_Point(NavisworksIntegratedAPI::InwSimpleVertex* v1) override
	{
		return S_OK;
	}

	HRESULT raw_SnapPoint(NavisworksIntegratedAPI::InwSimpleVertex* v1) override
	{
		return S_OK;
	}

	// IDispatch implementation - just simple stubs - these methods are not called anyway
	HRESULT QueryInterface(const IID& riid, void** ppvObject) override
	{
		return S_OK;
	}
	
	ULONG AddRef() override
	{
		return 1;
	}
	
	ULONG Release() override
	{
		return 1;
	}
	
	HRESULT GetTypeInfoCount(UINT* pctinfo) override
	{
		return S_OK;
	}
	
	HRESULT GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo) override
	{
		return S_OK;
	}
	
	HRESULT GetIDsOfNames(const IID& riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid, DISPID* rgDispId) override
	{
		return S_OK;
	}
	
	HRESULT Invoke(DISPID dispIdMember, const IID& riid, LCID lcid, WORD wFlags, DISPPARAMS* pDispParams,
		VARIANT* pVarResult, EXCEPINFO* pExcepInfo, UINT* puArgErr) override
	{
		return S_OK;
	}
	// end IDispatch implementation
};

TriangleReaderNative::TriangleReaderNative()
{
}

TriangleReaderNative::~TriangleReaderNative()
{
}


void TriangleReaderNative::Read(void* FragmentIUnknownPtr)
{
	NavisworksIntegratedAPI::InwOaFragment3Ptr Fragment(static_cast<IUnknown*>(FragmentIUnknownPtr));
	SimplePrimitivesCallback Callback;
	Callback.Geometry = this;

	// Callback will be called for each triangle in the fragment mesh
	Fragment->GenerateSimplePrimitives(NavisworksIntegratedAPI::nwEVertexProperty(NavisworksIntegratedAPI::nwEVertexProperty::eNORMAL | NavisworksIntegratedAPI::nwEVertexProperty::eTEX_COORD), &Callback);
}
