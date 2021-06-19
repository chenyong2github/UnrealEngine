// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using SolidworksDatasmith.Geometry;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.SwObjects
{
	[ComVisible(false)]
	public class SwFace
	{
		public SwBody Parent { get; set; } = null;
		public SwMaterial Material { get; set; } = null;
		public Face2 Face { get; set; } = null;
		public SwPartDoc Doc { get { return Parent.Doc; } }

		public SwFace(SwBody parent, Face2 face)
		{
			Parent = parent;
			Face = face;
			if (SwSingleton.CurrentScene.GetFaceID(face) <= 0)
				SwSingleton.CurrentScene.SetFaceID(face, SwSingleton.CurrentScene.NewFaceID);
		}

		public TriangleStrip ExtractGeometry()
		{
			TriangleStrip triangles = null;
			dynamic tris = Face.GetTessTriStrips(true);
			dynamic norms = Face.GetTessTriStripNorms();
			if (tris != null && norms != null)
			{
				triangles = new TriangleStrip(tris as float[], norms as float[]);
			}
			return (triangles.numTris > 0) ? triangles : null;
		}

		public SwMaterial GetMaterial()
		{
			SwMaterial mat = null;
			var scene = SwSingleton.CurrentScene;

			uint id = scene.GetFaceID(Face);
			if (id != 0)
				mat = scene.MaterialMapper.GetUserMaterial(Materials.MaterialMapper.EntityType.MU_FACE, id);

			Feature feature = Face.GetFeature();
			Body2 body = Face.GetBody();

			if (mat == null && feature != null)
				mat = scene.MaterialMapper.GetUserMaterial(Materials.MaterialMapper.EntityType.MU_FEATURE, scene.GetFeaturePath(feature, Doc.Doc as IModelDoc2));

			if (mat == null && body != null)
				mat = scene.MaterialMapper.GetUserMaterial(Materials.MaterialMapper.EntityType.MU_BODY, scene.GetBodyPath(body, Doc.Doc as IModelDoc2));

			if (mat == null && Doc.Doc != null)
			{
				mat = scene.MaterialMapper.GetUserMaterial(Materials.MaterialMapper.EntityType.MU_PART, (Doc.Doc as IModelDoc2).GetPathName());
			}

			return mat;
		}
	}
}
