// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.SwObjects
{
	[ComVisible(false)]
	public class SwFeature
	{
		public SwBody Parent { get; set; } = null;
		public SwMaterial Material { get; set; } = null;
		List<SwFace> Faces { get; set; } = new List<SwFace>();
		public Feature Feature { get; set; } = null;
		public SwPartDoc Doc { get { return Parent.Doc; } }

		public SwFeature(SwBody parent, Feature feature)
		{
			Feature = feature;
			Parent = parent;

			var name = feature.Name;

			var faces = (Object[])feature.GetFaces();
			if (faces != null)
			{
				foreach (var obj in faces)
				{
					Face2 face = (Face2)obj;
					Faces.Add(new SwFace(Parent, face));
				}
			}
		}
	}
}
