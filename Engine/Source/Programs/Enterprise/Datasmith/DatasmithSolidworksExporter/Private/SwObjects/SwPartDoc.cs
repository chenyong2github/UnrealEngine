// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using SolidworksDatasmith.Geometry;
using System.Runtime.InteropServices;
using SolidworksDatasmith.Engine;

namespace SolidworksDatasmith.SwObjects
{
	[ComVisible(false)]
	public class SwPartDoc
	{
		public string Name { get; set; } = "";
		public string PathName { get; set; } = "";
		public SwMaterial Material { get; set; } = null;
		List<SwBody> Bodies { get; set; } = new List<SwBody>();
		public PartDoc Doc { get; set; } = null;

		public SwPartDoc(PartDoc doc)
		{
			Doc = doc;
			SwSingleton.Events.PartAddItemEvent.Register(onAddItem);

			PathName = (doc as ModelDoc2).GetPathName();
			if (!string.IsNullOrEmpty(PathName))
			{
				Name = Path.GetFileNameWithoutExtension(PathName);
			}
			else
			{
				// unsaved imported parts have no path or name
				PathName = (doc as ModelDoc2).GetExternalReferenceName();
				Name = Path.GetFileNameWithoutExtension(PathName);
			}
		}

		private void LoadBodies()
		{
			try
			{
				var enum3 = Doc.EnumBodies3((int)swBodyType_e.swSolidBody, false);
				Body2 body = null;
				do
				{
					int fetched = 0;
					enum3.Next(1, out body, ref fetched);
					if (body != null && body.Visible && !body.IsTemporaryBody())
					{
						Bodies.Add(new SwBody(this, body));
					}
				} while (body != null);

				enum3 = Doc.EnumBodies3((int)swBodyType_e.swSheetBody, false);
				body = null;
				do
				{
					int fetched = 0;
					enum3.Next(1, out body, ref fetched);
					if (body != null && body.Visible && !body.IsTemporaryBody())
					{
						Bodies.Add(new SwBody(this, body));
					}
				} while (body != null);
			}
			catch (Exception e)
			{
				var s = e.Message;
			}
		}

		public void Load(bool fast = false)
		{
			SwSingleton.FireProgressEvent("Extracting Part Data");

			SwPartDoc doc2 = null;

			bool areSame = false;

			if (Bodies.Count != 0)
			{
				doc2 = new SwPartDoc(Doc);
				doc2.Name = Name;
				doc2.PathName = PathName;
				doc2.LoadBodies();
				areSame = IsSame(doc2);
			}

			if (!areSame)
			{
				if (doc2 != null)
				{
					Bodies = doc2.Bodies;
					doc2.Bodies = null;
					doc2 = null;
				}
				else
				{
					LoadBodies();
				}

				try
				{
					SwSingleton.CurrentScene.CollectMaterials(Doc as ModelDoc2);
				}
				catch(Exception){}

				PartCommand cmd = new PartCommand();
				cmd.PathName = PathName;
				cmd.Name = Name;
				cmd.StripGeom = ExtractSourceGeometry();
				SwSingleton.CurrentScene.Processor.AddCommand(cmd);

				if (!fast)
					SwSingleton.CurrentScene.SendModelDocMetadataToProcessor(Doc as ModelDoc2, cmd.Name, MetadataCommand.MetadataType.MeshActor);
			}
		}

		private void onAddItem(object sender, SwEventArgs args)
		{
			PartDoc doc = args.GetParameter<PartDoc>("Doc");
			if (doc.Equals(Doc))
			{
				int EntityType = args.GetParameter<int>("EntityType");
				string itemName = args.GetParameter<string>("itemName");
			}
		}
		
		public StripGeometry ExtractSourceGeometry()
		{
			StripGeometry stripGeom = new StripGeometry();
			foreach (var b in Bodies)
			{
				StripGeometryBody stripBody = new StripGeometryBody();
				stripBody.Bounds = b.Bounds;
				stripGeom.Bodies.Add(stripBody);
				foreach (var f in b.Faces)
				{
					StripGeometryFace stripFace = new StripGeometryFace();
					stripFace.Strip = f.ExtractGeometry();
					stripFace.Material = f.GetMaterial();
					stripBody.Faces.Add(stripFace);
				}
			}

			return stripGeom;
		}

		public bool IsSame(SwPartDoc doc2)
		{
			if (Bodies.Count != doc2.Bodies.Count) return false;
			for (int i = 0; i < Bodies.Count; i++)
			{
				if (Bodies[i].Faces.Count != doc2.Bodies[i].Faces.Count) return false;
				for (int j = 0; j < Bodies[i].Faces.Count; j++)
				{
					Face2 face1 = Bodies[i].Faces[j].Face;
					Face2 face2 = doc2.Bodies[i].Faces[j].Face;
					uint id1 = SwSingleton.CurrentScene.GetFaceID(face1);
					uint id2 = SwSingleton.CurrentScene.GetFaceID(face2);
					if (id1 != id2) return false;
					if (!Utility.IsSame(face1.GetArea(), face2.GetArea())) return false;
				}
			}
			return true;
		}
	}
}
