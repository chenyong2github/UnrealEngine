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
		private Dictionary<string, HashSet<int>> PartMaterials = new Dictionary<string, HashSet<int>>();
		private Dictionary<string, HashSet<int>> BodyMaterials = new Dictionary<string, HashSet<int>>();
		private Dictionary<string, HashSet<int>> FaceMaterials = new Dictionary<string, HashSet<int>>();
		private Dictionary<string, HashSet<int>> FeatureMaterials = new Dictionary<string, HashSet<int>>();

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

		private void RegisterMaterial(Dictionary<string, HashSet<int>> MaterialsDict, RenderMaterial RenderMat, string ObjectID)
		{
			if (!MaterialsDict.ContainsKey(ObjectID))
			{
				MaterialsDict[ObjectID] = new HashSet<int>();
			}

			int MaterialID = SwMaterial.GetMaterialID(RenderMat);
			if (!MaterialsDict[ObjectID].Contains(MaterialID))
			{
				MaterialsDict[ObjectID].Add(MaterialID);
			}
		}

		private static bool EqualMaterials(string ElemID, Dictionary<string, HashSet<int>> Cache1, Dictionary<string, HashSet<int>> Cache2)
		{
			if (Cache1.ContainsKey(ElemID) != Cache2.ContainsKey(ElemID))
			{
				return false;
			}

			if (!Cache1.ContainsKey(ElemID))
			{
				return true;
			}

			HashSet<int> Mats1 = Cache1[ElemID];
			HashSet<int> Mats2 = Cache2[ElemID];

			if (null == Mats1 || null == Mats2)
			{
				return false;
			}

			return Mats1.SetEquals(Mats2);
		}

		private bool EqualPartMaterials(SwPartDoc Other)
		{
			string PartID = (Doc as IModelDoc2).GetPathName();
			return EqualMaterials(PartID, PartMaterials, Other.PartMaterials);
		}

		private bool EqualBodyMaterials(Body2 Body, SwPartDoc Other)
		{
			string BodyID = SwScene.GetBodyPath(Body, (Doc as IModelDoc2));
			return EqualMaterials(BodyID, BodyMaterials, Other.BodyMaterials);
		}

		private bool EqualFaceMaterials(IFace2 Face, SwPartDoc Other)
		{
			string FaceID = SwScene.GetFaceID(Face).ToString();
			return EqualMaterials(FaceID, FaceMaterials, Other.FaceMaterials);
		}

		private bool EqualFeatureMaterials(IFeature Feature, SwPartDoc Other)
		{
			string FeatureID = SwScene.GetFeaturePath(Feature, (Doc as IModelDoc2));
			return EqualMaterials(FeatureID, FeatureMaterials, Other.FeatureMaterials);
		}

		private void LoadMaterials()
		{
			IModelDocExtension ext = (Doc as IModelDoc2).Extension;

			int numMaterials = ext.GetRenderMaterialsCount2((int)swDisplayStateOpts_e.swThisDisplayState, null);

			if (numMaterials > 0)
			{
				object[] materials = ext.GetRenderMaterials2((int)swDisplayStateOpts_e.swThisDisplayState, null);
				foreach (var omm in materials)
				{
					var mm = omm as RenderMaterial;
					int numUsers = mm.GetEntitiesCount();
					if (numUsers > 0)
					{
						object[] users = mm.GetEntities();
						foreach (var user in users)
						{
							if (user is IPartDoc part)
							{
								RegisterMaterial(PartMaterials, mm, (part as IModelDoc2).GetPathName());
								continue;
							}

							if (user is IBody2 body)
							{
								RegisterMaterial(BodyMaterials, mm, SwScene.GetBodyPath(body, (Doc as IModelDoc2)));
								continue;
							}

							if (user is IFace2 face)
							{
								uint id = SwScene.GetFaceID(face);
								RegisterMaterial(FaceMaterials, mm, id.ToString());
								continue;
							}

							if (user is IFeature feat)
							{
								IBody2 Body = feat.IGetBody2();

								if (Body != null)
								{
									RegisterMaterial(BodyMaterials, mm, SwScene.GetBodyPath(Body, (Doc as IModelDoc2)));
								}
								else
								{
									RegisterMaterial(PartMaterials, mm, (Doc as IModelDoc2).GetPathName());
								}
								continue;
							}
						}
					}
				}
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

		public void Load(bool bInIsDirectLinkUpdate)
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
				doc2.LoadMaterials();
				areSame = bInIsDirectLinkUpdate && IsSame(doc2);
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
					LoadMaterials();
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

				if (!bInIsDirectLinkUpdate)
				{
					SwSingleton.CurrentScene.SendModelDocMetadataToProcessor(Doc as ModelDoc2, cmd.Name, MetadataCommand.MetadataType.MeshActor);
				}
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
			if (Bodies.Count != doc2.Bodies.Count)
			{
				return false;
			}

			if (!EqualPartMaterials(doc2))
			{
				return false;
			}

			for (int i = 0; i < Bodies.Count; i++)
			{
				if (Bodies[i].Faces.Count != doc2.Bodies[i].Faces.Count)
				{
					return false;
				}

				if (!EqualBodyMaterials(Bodies[i].Body, doc2))
				{
					return false;
				}

				for (int j = 0; j < Bodies[i].Faces.Count; j++)
				{
					try
					{
						Face2 face1 = Bodies[i].Faces[j].Face;
						Face2 face2 = doc2.Bodies[i].Faces[j].Face;
						uint id1 = SwScene.GetFaceID(face1);
						uint id2 = SwScene.GetFaceID(face2);
						if (id1 != id2)
						{
							return false;
						}
						if (!Utility.IsSame(face1.GetArea(), face2.GetArea()))
						{
							return false;
						}
						if (!EqualFaceMaterials(face1, doc2))
						{
							return false;
						}
					}
					catch (Exception)
					{
						return false; // Original body object has become invalid, therefore consider it different
					}
				}
			}
			return true;
		}
	}
}
