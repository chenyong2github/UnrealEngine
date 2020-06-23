// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using Autodesk.Navisworks.Api;
using Autodesk.Navisworks.Api.Plugins;
using Autodesk.Navisworks.Api.DocumentParts;
using Autodesk.Navisworks.Api.Interop;
using Autodesk.Navisworks.Api.Interop.ComApi;


namespace DatasmithNavisworks
{
	[Plugin("DatasmithNavisworksExporter", "EpicGames", 
		DisplayName = "Datasmith")]
	[AddInPlugin(AddInLocation.AddIn, // Put addin button on the 'Tool add-ins' tab 
		LoadForCanExecute = true,
		LargeIcon = "DatasmithIcon32.png")]
	[Strings("DatasmithNavisworksExporter.name")]
	public class DatasmithNavisworksExporter : AddInPlugin
	{
		private const string DIALOG_CAPTION = "Export 3D View to Unreal Datasmith"; // TODO: localize

		// Revit application information for Datasmith.
		private const string HOST_NAME = "Navisworks";
		private const string VENDOR_NAME = "Autodesk Inc.";
		private const string PRODUCT_NAME = "Navisworks Manage"; // TODO: identify?

		private StreamWriter LogStream;

		public DatasmithNavisworksExporter()
		{
		}

		public override CommandState CanExecute()
		{
			return new CommandState(true);
		}

		protected override void OnLoaded()
		{
		}

		public struct Vector3
		{
			public float X;
			public float Y;
			public float Z;
		}

		struct Box
		{
			public Vector3 Min;
			public Vector3 Max;

			public bool Intersects(Box Other)
			{
				Box A = this;
				Box B = Other;
				return A.MinLessThanMaxOf(B) && B.MinLessThanMaxOf(A);
			}

			private bool MinLessThanMaxOf(Box Other)
			{
				return (Min.X < Other.Max.X) 
				       && (Min.Y < Other.Max.Y)
				       && (Min.Z < Other.Max.Z);
			}
		}

		public class TransformMatrix
		{
			public float[] Matrix4x4;

			public float M(int X, int Y)
			{
				return Matrix4x4[Y * 4 + X];
			}

			public Vector3 TransformPosition(Vector3 V)
			{
				Vector3 R;

				R.X = V.X * M(0, 0) + V.Y * M(0, 1) + V.Z * M(0, 2) + 1 * M(0, 3);
				R.Y = V.X * M(1, 0) + V.Y * M(1, 1) + V.Z * M(1, 2) + 1 * M(1, 3);
				R.Z = V.X * M(2, 0) + V.Y * M(2, 1) + V.Z * M(2, 2) + 1 * M(2, 3);

				return R;
			}

			public Vector3 TransformNormal(Vector3 V)
			{
				Vector3 R;

				R.X = V.X * M(0, 0) + V.Y * M(0, 1) + V.Z * M(0, 2) + 0 * M(0, 3);
				R.Y = V.X * M(1, 0) + V.Y * M(1, 1) + V.Z * M(1, 2) + 0 * M(1, 3);
				R.Z = V.X * M(2, 0) + V.Y * M(2, 1) + V.Z * M(2, 2) + 0 * M(2, 3);


				return R;
			}

			public static readonly TransformMatrix Identity = new TransformMatrix
			{
				Matrix4x4 = new float[] {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,}
			};

		};

		class ItemFilterParameters
		{
			public bool bHasClipBox;
			public Box ClipBox;
		}

		// all meshes for Node's fragments
		class InstanceGeometry
		{
			public List<FDatasmithFacadeMesh> Meshes;

			public InstanceGeometry()
			{
				this.Meshes = new System.Collections.Generic.List<FDatasmithFacadeMesh>();
			}
		}

		class Instances
		{
			public InstanceGeometry Geometry;

			public Instances()
			{
				Geometry = new InstanceGeometry();
			}
		};

		public override int Execute(params string[] Parameters)
		{
			// TODO: 
			// Application.ActiveDocument.ActiveView.RequestDelayedRedraw(ViewRedrawRequests.OverlayRender);
			// ... Application.ActiveDocument.ActiveView.RequestDelayedRedraw(ViewRedrawRequests.All);

			try
			{
				bool IsAutomated = Autodesk.Navisworks.Api.Application.IsAutomated;
				Info(IsAutomated ? "Accessed using Automation" : "Exporting from GUI");

				Document ActiveDocument = Autodesk.Navisworks.Api.Application.ActiveDocument;

				string FilePath;
				if (IsAutomated)
				{
					if (Parameters.Length == 0)
					{
						DisplayWarning("Datasmith Export command requires target path parameter string");
						return -1;
					}

					FilePath = Parameters[0];
				}
				else
				{
					string InitialDirectory;
					if (File.Exists(ActiveDocument.FileName))
					{
						InitialDirectory = Path.GetDirectoryName(ActiveDocument.FileName);
					}
					else
					{
						InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
					}

					SaveFileDialog Dialog = new SaveFileDialog
					{
						Title = DIALOG_CAPTION,
						InitialDirectory =  InitialDirectory,
						FileName = Path.GetFileNameWithoutExtension(ActiveDocument.FileName) + ".udatasmith",
						DefaultExt = "udatasmith",
						Filter = "Unreal Datasmith|*.udatasmith",
						CheckFileExists = false,
						CheckPathExists = true,
						AddExtension = true,
						OverwritePrompt = true
					};

					if (Dialog.ShowDialog() != DialogResult.OK)
					{
						return -1; // this is not used by Navisworks by docs, but can be used for tests/automation
					}

					FilePath = Dialog.FileName;
				}

				if (string.IsNullOrWhiteSpace(FilePath))
				{
					DisplayWarning("The udatasmith file name is not valid. Aborting the export....");
					return -1;
				}

				string LogFilePath = FilePath + "." + DateTime.Now.ToString("yyyy-M-dd-HH-mm-ss") + ".log";
				LogStream = new StreamWriter(LogFilePath);
				try
				{
					EventInfo("Start export: " + FilePath);
					InwOpState10 State = Autodesk.Navisworks.Api.ComApi.ComApiBridge.State;

					ItemFilterParameters ItemFilterParams = new ItemFilterParameters();

					ExtractClipBox(ActiveDocument, ItemFilterParams);

					// TODO: right-handed Z-up - this is how gatehouse looks like but NW allows to setup orientation
					FDatasmithFacadeElement.SetCoordinateSystemType(FDatasmithFacadeElement.ECoordinateSystemType.RightHandedZup);

					double CentimetersPerUnit = GetCentimetersPerUnit(ActiveDocument.Units);
					Info($"CentimetersPerUnit: {CentimetersPerUnit}");
					FDatasmithFacadeElement.SetWorldUnitScale((float)CentimetersPerUnit);

					DocumentModels Models = ActiveDocument.Models;

					Progress ProgressBar = Autodesk.Navisworks.Api.Application.BeginProgress("Export", "exporting stuff");

					string ProductVersion = "1.0"; // TODO:
					FDatasmithFacadeScene DatasmithScene =
						new FDatasmithFacadeScene(HOST_NAME, VENDOR_NAME, PRODUCT_NAME, ProductVersion);
					DatasmithScene.PreExport();

					SceneContext SceneContext = new SceneContext
					{
						ItemFilterParams = ItemFilterParams,
						State = State,
						ModelItemCount = Models.RootItemDescendantsAndSelf.Count() // An estimate for total scene item count
					};

					// Collect Navisworks scene nodes 
					{
						EventInfo("Retrieving Model Items");
						ProgressBar.BeginSubOperation(0.25, "Retrieving Model Items");

						NodeContext Context = new NodeContext(SceneContext);
						CollectNodes(Context, new SceneItem(null, State.CurrentPartition), ProgressBar);

						// Output some early scene stats
						Info($"GeometryInstanceTotalCount={Context.SceneContext.GeometryInstanceTotalCount}");
						Info($"GroupInstanceTotalCount={Context.SceneContext.GroupInstanceTotalCount}");
						Info($"HiddenNodeCount={Context.SceneContext.HiddenNodeCount}");
						Info($"GeometryNodeCount={Context.SceneContext.GeometryNodeCount}");
						Info($"GroupNodeCount={Context.SceneContext.GroupNodeCount}");
						Info($"InsertNodeCount={Context.SceneContext.InsertNodeCount}");
						Info($"LayerNodeCount={Context.SceneContext.LayerNodeCount}");
						Info($"FragmentCount={Context.SceneContext.FragmentCount}");
						Info($"Collected Nodes count={SceneContext.SceneItemCount}");

						ProgressBar.EndSubOperation();
						EventInfo("Done - Retrieving Model Items");
						LogStream?.Flush(); // let all scene info be written to file before starting long geometry export operation
					}

					// Extract geometry data and create Datasmith actors
					{
						EventInfo("Extracting Geometry");
						ProgressBar.BeginSubOperation(0.5, "Extracting Geometry");

						int SceneItemCount = SceneContext.SceneItemList.Count;
						for (int ItemIndex = 0; ItemIndex < SceneItemCount; ItemIndex++)
						{
							SceneItem Item = SceneContext.SceneItemList[ItemIndex];

							if (ProgressBar.IsCanceled)
							{
								Autodesk.Navisworks.Api.Application.EndProgress();
								return 0;
							}

							if (Item.Node.IsGeometry) // Is this a a Leaf geometry node
							{
								if (!ConvertGeometryItem(SceneContext, DatasmithScene, Item, ItemIndex, ProgressBar))
								{
									return 0;
								}
							}
							else
							{
								FDatasmithFacadeActor Actor =
									new FDatasmithFacadeActor("", "");
								GetNameAndLabelFromItem(Item, Actor);
								SceneContext.DatasmithActors.Add(Actor);
								SceneContext.DatastmithActorForModelItem.Add(Item, Actor);
							}

							ProgressBar.Update((double) ItemIndex / SceneItemCount);
						}

						int TotalTriangleCount = SceneContext.DatasmithMeshes.Sum(Mesh => Mesh.GetTriangleCount());

						Info($"Created Datasmith Scene with {SceneContext.DatasmithActors.Count} Actors, {SceneContext.DatasmithMeshes.Count} Meshes, {TotalTriangleCount} TotalTriangleCount");

						ProgressBar.EndSubOperation();
						EventInfo("Done - Extracting Geometry");
						LogStream?.Flush();
					}


					{
						EventInfo("Building Scene Hierarchy");
						ProgressBar.BeginSubOperation(0.5, "Building Scene Hierarchy");
						int ModelItemCount = SceneContext.SceneItemList.Count();
						int ModelItemIndex = 0;
						foreach (SceneItem Item in SceneContext.SceneItemList)
						{
							// check, in case we skipped exporting this ModelItem for some reason
							if (SceneContext.DatastmithActorForModelItem.TryGetValue(Item, out FDatasmithFacadeActor Actor))
							{
								// If this item has parent and it has its Datasmith actor
								if (Item.Parent != null &&
								    SceneContext.DatastmithActorForModelItem.TryGetValue(Item.Parent,
									    out FDatasmithFacadeActor ParentActor))
								{
									ParentActor.AddChild(Actor);
								}
								else
								{
									DatasmithScene.AddElement(Actor);
								}
							}

							ModelItemIndex++;
							ProgressBar.Update((double) ModelItemIndex / ModelItemCount);
						}

						ProgressBar.EndSubOperation();
						EventInfo("Done - Building Scene Hierarchy");
					}

					{
						EventInfo("Optimizing Datasmith scene");
						ProgressBar.BeginSubOperation(0.8, "Optimizing Datasmith scene");
						// Optimize the Datasmith actor hierarchy by removing the intermediate single child actors.
						DatasmithScene.Optimize();
						ProgressBar.EndSubOperation();
						EventInfo("Done - Optimizing Datasmith scene");
					}

					{
						// Build and export the Datasmith scene instance and its scene element assets.
						EventInfo("Saving Datasmith scene");
						ProgressBar.BeginSubOperation(0.9, "Saving Datasmith scene");
						DatasmithScene.ExportScene(FilePath);
						ProgressBar.EndSubOperation();
						EventInfo("Done - Saving Datasmith scene");

					}
					Autodesk.Navisworks.Api.Application.EndProgress();
				}
				finally
				{
					LogStream.Close();
					LogStream = null;
				}
			}
			catch (Exception Ex)
			{
				DisplayWarning(Ex.ToString());
			}

			return 0;
		}

		private bool ConvertGeometryItem(SceneContext SceneContext, FDatasmithFacadeScene DatasmithScene,
			SceneItem Item, int ItemIndex, Progress ProgressBar)
		{
			FDatasmithFacadeActor ItemDatasmithActor = new FDatasmithFacadeActor("", "");
			GetNameAndLabelFromItem(Item, ItemDatasmithActor);
			SceneContext.DatasmithActors.Add(ItemDatasmithActor); // hold on to reference

			SceneContext.DatastmithActorForModelItem.Add(Item, ItemDatasmithActor);

			bool bIsInstance = Item.InstanceOf >= 0;

			// Geometry node is split into 'Fragments', each containing mesh and transform
			InwNodeFragsColl Fragments = Item.Node.Fragments();

			// Leaf geometry node sometimes split into selarate 'fragments'
			// we merge this fragments into one mesh (fragments separation is not reflected in Navisoworks UI)
			bool bMergeFragments =  !(bIsInstance || Item.bIsPrototype);

			if (!bMergeFragments)
			{
				// Each Fragment has geometry(that can be shared between instances) and transform(per instance)
				List<FDatasmithFacadeMesh> FragmentMeshes = null;
				if (bIsInstance) // In case node is an instance use meshes from
				{
					Instances Instances = SceneContext.InstancesForPrototype[Item.InstanceOf];
					FragmentMeshes = Instances.Geometry.Meshes;
				}

				if (Item.bIsPrototype)
				{
					Instances Instances = new Instances
					{
						Geometry = {Meshes = new List<FDatasmithFacadeMesh>()}
					};
					SceneContext.InstancesForPrototype.Add(ItemIndex, Instances);
					FragmentMeshes = Instances.Geometry.Meshes;
				}

				if (Fragments.Count > 1)
				{
					Info("Multiple fragments on instanced node");
				}
				int MeshIndex = 0; // We are checking if fragments belong to the path
				foreach (InwOaFragment3 Fragment in Fragments)
				{
					// TODO: add suboperation? In case geometry item contains lots of fragments
					if (ProgressBar.IsCanceled)
					{
						Autodesk.Navisworks.Api.Application.EndProgress();
						return false;
					}

					if (FragmentPathBelongsToThisItem(Item, Fragment))
					{
						FDatasmithFacadeMesh DatasmithMesh;
						if (bIsInstance)
						{
							DatasmithMesh = FragmentMeshes[MeshIndex];
						}
						else
						{
							string DatasmithMeshName =
								"M" + MeshIndex.ToString() + ":" + ItemDatasmithActor.GetName();
							string DatasmithMeshLabel = ItemDatasmithActor.GetLabel() + "_M_" + MeshIndex.ToString();
							DatasmithMesh = new FDatasmithFacadeMesh(DatasmithMeshName,
								DatasmithMeshLabel);
							// Hash the Datasmith mesh name to shorten it and make it valid
							DatasmithMesh.HashName();

							SceneContext.DatasmithMeshes.Add(DatasmithMesh); // hold on to reference
							DatasmithScene.AddElement(DatasmithMesh);

							SetFragmentGeometryUntransformedToMesh(Fragment, DatasmithMesh);

							if (Item.bIsPrototype)
							{
								FragmentMeshes.Add(DatasmithMesh);
							}
						}

						FDatasmithFacadeActorMesh FragmentMeshActor =
							new FDatasmithFacadeActorMesh(
								"F" + MeshIndex.ToString() + ":" + ItemDatasmithActor.GetName(),
								ItemDatasmithActor.GetLabel() + "_Frag_" + MeshIndex.ToString());
						FragmentMeshActor.HashName();
						SceneContext.DatasmithActors.Add(FragmentMeshActor); // hold on to reference
						ItemDatasmithActor.AddChild(FragmentMeshActor);

						TransformMatrix Transform = ConvertMatrix(Fragment.GetLocalToWorldMatrix());
						FragmentMeshActor.SetWorldTransform(Transform.Matrix4x4);
						FragmentMeshActor.SetMesh(DatasmithMesh.GetName());

						MeshIndex++;
					}
					else
					{
						Info(
							$"Skipping Fragment for '{ItemDatasmithActor.GetLabel()}', Path doesn't not belong to Item");
					}
				}
			}
			else
			{
				string DatasmithMeshName =
					"M:" + ItemDatasmithActor.GetName();
				string DatasmithMeshLabel =
					ItemDatasmithActor.GetLabel() + "_M";
				FDatasmithFacadeMesh DatasmithMesh = new FDatasmithFacadeMesh(DatasmithMeshName,
					DatasmithMeshLabel);
				// Hash the Datasmith mesh name to shorten it and make it valid
				DatasmithMesh.HashName();

				SceneContext.DatasmithMeshes.Add(DatasmithMesh); // hold on to reference
				DatasmithScene.AddElement(DatasmithMesh);

				FDatasmithFacadeActorMesh FragmentMeshActor =
					new FDatasmithFacadeActorMesh(
						"F:" + ItemDatasmithActor.GetName(),
						ItemDatasmithActor.GetLabel() + "_F");
				FragmentMeshActor.HashName();
				SceneContext.DatasmithActors.Add(FragmentMeshActor); // hold on to reference
				ItemDatasmithActor.AddChild(FragmentMeshActor);

				FragmentMeshActor.SetMesh(DatasmithMesh.GetName());

				// Geometry node is split into 'Fragments', each containing mesh and transform
				if (Fragments.Count > 1)
				{
					Info("Multiple fragments on non-instanced node");
				}
				
				foreach (InwOaFragment3 Fragment in Fragments)
				{
					if (ProgressBar.IsCanceled)
					{
						Autodesk.Navisworks.Api.Application.EndProgress();
						return false;
					}

					if (FragmentPathBelongsToThisItem(Item, Fragment))
					{
						if (Fragments.Count > 1)
						{
							// Add each Fragment to datasmith mesh
							AddFragmentGeometryToMesh(Fragment, DatasmithMesh);
						}
						else 
						{
							// Keep Fragment;s transform when it's a lone fragment for geometry(for convenience)
							SetFragmentGeometryUntransformedToMesh(Fragment, DatasmithMesh);

							TransformMatrix Transform = ConvertMatrix(Fragment.GetLocalToWorldMatrix());
							FragmentMeshActor.SetWorldTransform(Transform.Matrix4x4);
							FragmentMeshActor.SetMesh(DatasmithMesh.GetName());
						}
					}
					else
					{
						Info(
							$"Skipping Fragment for '{ItemDatasmithActor.GetLabel()}', Path doesn't not belong to Item");
					}
				}
			}
			GC.KeepAlive(Fragments);

			return true;
		}

		private static unsafe void SetFragmentGeometryUntransformedToMesh(InwOaFragment3 Fragment,
			FDatasmithFacadeMesh DatasmithMesh)
		{
			using(DatasmithNavisworksUtil.Geometry ReadGeometry = DatasmithNavisworksUtil.TriangleReader.ReadGeometry(Fragment))
			{
				int VertexCount = ReadGeometry.VertexCount;
				for (int VertexIndex = 0; VertexIndex < VertexCount; VertexIndex++)
				{
					DatasmithMesh.AddVertex((float) ReadGeometry.Coords[VertexIndex * 3 + 0],
						(float) ReadGeometry.Coords[VertexIndex * 3 + 1],
						(float) ReadGeometry.Coords[VertexIndex * 3 + 2]);
					DatasmithMesh.AddNormal((float) ReadGeometry.Normals[VertexIndex * 3 + 0],
						(float) ReadGeometry.Normals[VertexIndex * 3 + 1],
						(float) ReadGeometry.Normals[VertexIndex * 3 + 2]);
					DatasmithMesh.AddUV(0, (float) ReadGeometry.UVs[VertexIndex * 2 + 0],
						(float) ReadGeometry.UVs[VertexIndex * 2 + 1]);
				}

				int TriangleCount = ReadGeometry.TriangleCount;
				for (int TriangleIndex = 0; TriangleIndex < TriangleCount; TriangleIndex++)
				{
					DatasmithMesh.AddTriangle(
						(int)ReadGeometry.Indices[TriangleIndex * 3], 
						(int)ReadGeometry.Indices[TriangleIndex * 3 + 1],
						(int)ReadGeometry.Indices[TriangleIndex * 3 + 2]);
				}
			}
		}

		private static unsafe void AddFragmentGeometryToMesh(InwOaFragment3 Fragment, FDatasmithFacadeMesh DatasmithMesh)
		{
			TransformMatrix Transform = ConvertMatrix(Fragment.GetLocalToWorldMatrix());
			using(DatasmithNavisworksUtil.Geometry ReadGeometry = DatasmithNavisworksUtil.TriangleReader.ReadGeometry(Fragment))
			{
				// Remember base vertex index - we are merging fragment meshes into DatasmithMesh
				int BaseIndex = DatasmithMesh.GetVertexCount(); 

				int VertexCount = ReadGeometry.VertexCount;
				for (int VertexIndex = 0; VertexIndex < VertexCount; VertexIndex++)
				{
					Vector3 Coord;
					Coord.X = (float) ReadGeometry.Coords[VertexIndex * 3 + 0];
					Coord.Y = (float) ReadGeometry.Coords[VertexIndex * 3 + 1];
					Coord.Z = (float) ReadGeometry.Coords[VertexIndex * 3 + 2];
					Vector3 CoordTransformed = Transform.TransformPosition(Coord);

					DatasmithMesh.AddVertex(CoordTransformed.X, CoordTransformed.Y, CoordTransformed.Z);

					Vector3 Normal;
					Normal.X = (float) ReadGeometry.Normals[VertexIndex * 3 + 0];
					Normal.Y = (float) ReadGeometry.Normals[VertexIndex * 3 + 1];
					Normal.Z = (float) ReadGeometry.Normals[VertexIndex * 3 + 2];
					Vector3 NormalTransformed = Transform.TransformNormal(Normal);

					DatasmithMesh.AddNormal(NormalTransformed.X, NormalTransformed.Y, NormalTransformed.Z);

					DatasmithMesh.AddUV(0, (float) ReadGeometry.UVs[VertexIndex * 2 + 0],
						(float) ReadGeometry.UVs[VertexIndex * 2 + 1]);
				}

				int TriangleCount = ReadGeometry.TriangleCount;
				for (int TriangleIndex = 0; TriangleIndex < TriangleCount; TriangleIndex++)
				{
					DatasmithMesh.AddTriangle(
						BaseIndex + (int)ReadGeometry.Indices[TriangleIndex * 3], 
						BaseIndex + (int)ReadGeometry.Indices[TriangleIndex * 3 + 1],
						BaseIndex + (int)ReadGeometry.Indices[TriangleIndex * 3 + 2]);
				}
			}
		}

		// TODO: Find out if it's a Navisworks issue or something else
		// For some reason sometimes fragments under a leaf Geometry node also contain other fragments, from other nodes!
		// This functions tests Path(through scene tree) for Fragment testing if it corresponds to SceneItem
		private static bool FragmentPathBelongsToThisItem(SceneItem Item, InwOaFragment3 Fragment)
		{
			Array PathArray = ((Array) (object) Fragment.path.ArrayData);

			List<int> PathList = new List<int>();
			SceneItem ItemSearch = Item;
			while (ItemSearch != null)
			{
				if (ItemSearch.Parent != null) // don't add root
				{
					PathList.Insert(0, ItemSearch.Index + 1);
				}

				ItemSearch = ItemSearch.Parent;
			}

			List<int> FragmentPathList = PathArray.OfType<int>().ToList();

			bool bIsFragmentGeometryUnderThisNode =
				PathList.SequenceEqual(FragmentPathList);
			return bIsFragmentGeometryUnderThisNode;
		}

		private void ExtractClipBox(Document ActiveDocument, ItemFilterParameters ItemFilterParams)
		{
			ItemFilterParams.bHasClipBox = false;

			LcOaClipPlaneSet ClipPlaneSet = ActiveDocument.ActiveView.Viewer.GetClipPlaneSet();
			LcOaClipPlaneSetMode ClipPlaneSetMode = ClipPlaneSet.GetMode();

			if (ClipPlaneSetMode == LcOaClipPlaneSetMode.eMODE_BOX)
			{
				BoundingBox3D Box3D = ClipPlaneSet.GetBox();

				ItemFilterParams.bHasClipBox = true;
				Box ClipBox = new Box
				{
					Min = {X = (float) Box3D.Min.X, Y = (float) Box3D.Min.Y, Z = (float) Box3D.Min.Z},
					Max = {X = (float) Box3D.Max.X, Y = (float) Box3D.Max.Y, Z = (float) Box3D.Max.Z}
				};

				ItemFilterParams.ClipBox = ClipBox;
			}

			Info(ItemFilterParams.bHasClipBox ? "ClipBox: " + ItemFilterParams.ClipBox : "No clip box");
		}

		private static double GetCentimetersPerUnit(Units Units)
		{
			const double CENTIMETERS_PER_YARD = 91.44;
			const double CENTIMETERS_PER_INCH = 2.54;
			switch (Units)
			{
				case Units.Meters:
					return 100;
				case Units.Centimeters:
					return 1;
				case Units.Millimeters:
					return 0.1;
				case Units.Feet:
					return 30.48;
				case Units.Inches:
					return CENTIMETERS_PER_INCH;
				case Units.Yards:
					return CENTIMETERS_PER_YARD;
				case Units.Kilometers:
					return 100000;
				case Units.Miles:
					return CENTIMETERS_PER_YARD * 1760;
				case Units.Micrometers:
					return 0.0001;
				case Units.Mils:
					return CENTIMETERS_PER_INCH * 0.001;
				case Units.Microinches:
					return CENTIMETERS_PER_INCH * 0.000001;
				default:
					return 1;
			}
		}

		private static void GetNameAndLabelFromItem(SceneItem Item, FDatasmithFacadeElement Element)
		{
			string Label = Item.Node.UserName;
			const int LabelMaxLength = 50;

			// Reduce too long label(or asset won't save if path is >255)
			if (Label.Length > LabelMaxLength)
			{
				// Leave right(end) part of the label - looks like in contains most useful information(and most varying per object)
				Label = Label.Substring(Label.Length - LabelMaxLength, LabelMaxLength);
			}

			// Path uniquely identifies scene Item, so build Name from it
			List<int> PathArray = new List<int>();
			while (Item!=null)
			{
				PathArray.Add(Item.Index);
				Item = Item.Parent;
			}

			string Name = string.Join("_", (from object O in PathArray select O.ToString()));

			Element.SetName(Name);
			Element.SetLabel(Label);
			Element.HashName();

		}

		private static TransformMatrix ConvertMatrix(InwLTransform3f LocalToWorldMatrix)
		{
			Array Matrix = (Array) (object) (LocalToWorldMatrix.Matrix);

			float[] MatrixFloat = new float[16];

			foreach (int Index in Enumerable.Range(0, 16))
			{
				MatrixFloat[Index] = (float) (double) Matrix.GetValue(Index + Matrix.GetLowerBound(0));
			}

			return new TransformMatrix() {Matrix4x4 = MatrixFloat};

		}

		public class SimplePrimitiveWithTransform : InwSimplePrimitivesCB
		{
			private FDatasmithFacadeMesh Mesh;

			public SimplePrimitiveWithTransform(FDatasmithFacadeMesh Mesh)
			{
				this.Mesh = Mesh;
			}

			public void Triangle(InwSimpleVertex V1, InwSimpleVertex V2, InwSimpleVertex V3)
			{
				int BaseVertexIndex = Mesh.GetVertexCount();

				AddVertex(V1);
				AddVertex(V2);
				AddVertex(V3);

				Mesh.AddTriangle(BaseVertexIndex + 0, BaseVertexIndex + 1, BaseVertexIndex + 2);
			}

			public void Line(InwSimpleVertex V1, InwSimpleVertex V2)
			{
				// TODO:
			}

			public void Point(InwSimpleVertex V1)
			{
				// TODO:
			}

			public void SnapPoint(InwSimpleVertex V1)
			{
				// TODO:
			}

			private void AddVertex(InwSimpleVertex Vertex)
			{
				Vector3 Position = ConvertCoord(Vertex);
				Mesh.AddVertex(Position.X, Position.Y, Position.Z);

				Vector3 Normal = ConvertNormal(Vertex);
				Mesh.AddNormal(Normal.X, Normal.Y, Normal.Z);

				{
					Array A = Vertex.tex_coord as Array;
					// NOTE: Navisworks COM api array indexing is 1-based
					float U = (float) A.GetValue(1);
					float V = (float) A.GetValue(2);
					Mesh.AddUV(0, U, V);
				}
			}

			private Vector3 ConvertCoord(InwSimpleVertex SimpleVertex)
			{
				// vertex attributes are variant arrays
				Array A = SimpleVertex.coord as Array;

				Vector3 Result;
				// NOTE: Navisworks COM api array indexing is 1-based
				Result.X = (float) A.GetValue(1);
				Result.Y = (float) A.GetValue(2);
				Result.Z = (float) A.GetValue(3);
				return Result;
			}

			private Vector3 ConvertNormal(InwSimpleVertex SimpleVertex)
			{
				Array A = SimpleVertex.normal as Array;

				Vector3 Result;
				// NOTE: Navisworks COM api array indexing is 1-based
				Result.X = (float) A.GetValue(1);
				Result.Y = (float) A.GetValue(2);
				Result.Z = (float) A.GetValue(3);
				return Result;
			}

		};

		private void Info(string Message, int Level = 0)
		{
			string Indent = new string(' ', Level*2);
			LogStream?.WriteLine($"{Indent}{Message}");
		}

		private void EventInfo(string Message)
		{
			LogStream?.WriteLine($"{Process.GetCurrentProcess().TotalProcessorTime.TotalSeconds:0.00}:{Message}");
			LogStream?.Flush(); // let all scene info be written for critical message(event)
			Console.WriteLine($"{Process.GetCurrentProcess().TotalProcessorTime.TotalSeconds:0.00}:{Message}");
			Debug.WriteLine($"{Process.GetCurrentProcess().TotalProcessorTime.TotalSeconds:0.00}:{Message}");
		}

		private static void DisplayWarning(string Message)
		{
			if (Autodesk.Navisworks.Api.Application.IsAutomated)
			{
				Console.WriteLine(Message);
			}
			else
			{
				MessageBox.Show(Message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
			}
		}

		class SceneItem
		{
			public SceneItem(SceneItem Parent, InwOaNode Node, int Index = 0)
			{
				this.Parent = Parent;
				this.Node = Node;
				this.Index = Index;
				this.InstanceOf = -1;
				this.bIsPrototype = false;
				InwLBox3f Box3F = Node.GetBoundingBox(true, true);

				this.BoundingBox.Min.X = (float)Box3F.min_pos.data1;
				this.BoundingBox.Min.Y = (float)Box3F.min_pos.data2;
				this.BoundingBox.Min.Z = (float)Box3F.min_pos.data3;
				this.BoundingBox.Max.X = (float)Box3F.max_pos.data1;
				this.BoundingBox.Max.Y = (float)Box3F.max_pos.data2;
				this.BoundingBox.Max.Z = (float)Box3F.max_pos.data3;
			}

			public SceneItem Parent;
			public InwOaNode Node;
			public int Index;
			public Box BoundingBox;
			public int InstanceOf;
			public bool bIsPrototype;
		}

		class SceneContext
		{
			public ItemFilterParameters ItemFilterParams;
			public InwOpState10 State;
			public int ModelItemCount;
			
			public List<SceneItem> SceneItemList = new List<SceneItem>();

			public Dictionary<SceneItem, FDatasmithFacadeActor> DatastmithActorForModelItem = new Dictionary<SceneItem, FDatasmithFacadeActor>();
			public List<FDatasmithFacadeMesh> DatasmithMeshes = new List<FDatasmithFacadeMesh>();
			public List<FDatasmithFacadeActor> DatasmithActors = new List<FDatasmithFacadeActor>();

			public int GroupInstanceTotalCount = 0;
			public int GeometryInstanceTotalCount = 0;
			public int HiddenNodeCount = 0;
			public int GeometryNodeCount = 0;
			public int GroupNodeCount = 0;
			public int InsertNodeCount = 0;
			public int LayerNodeCount = 0;

			public int FragmentCount = 0;

			public SceneContext()
			{
			}

			public int SceneItemCount => SceneItemList.Count;
			public Dictionary<int, Instances> InstancesForPrototype = new Dictionary<int, Instances>();
		}

		class NodeContext
		{
			public SceneContext SceneContext;

			public int Level;
			public NodeContext Parent;
			public Dictionary<object, int> UsedNodes;

			public NodeContext(SceneContext SceneContext)
			{
				Level = 0;
				Parent = null;
				UsedNodes = new Dictionary<object, int>();
				this.SceneContext = SceneContext;
			}

			public NodeContext(NodeContext Parent)
			{
				this.Level = Parent.Level + 1;
				this.Parent = Parent;
				this.UsedNodes = Parent.UsedNodes;
				this.SceneContext = Parent.SceneContext;
			}
		};

		private void CollectNodes(NodeContext Context, SceneItem Item, Progress ProgressBar)
		{
			Info($"Node {Item.Index}: {Item.Node.UserName}", Context.Level);

			if (Item.Node.IsOverrideHide)
			{
				// skipping entire hierarchy for hidden nodes
				Context.SceneContext.HiddenNodeCount++;
				return;
			}

			if (Context.SceneContext.ItemFilterParams.bHasClipBox && !Item.BoundingBox.Intersects(Context.SceneContext.ItemFilterParams.ClipBox))
			{
				return;
			}

			int ItemIndex = Context.SceneContext.SceneItemList.Count;
			Context.SceneContext.SceneItemList.Add(Item);

			InwOaNode Node = Item.Node;
			Context.SceneContext.State.X64PtrVar(Node, out object Pointer); // Get actual 64-bit pointer to COM object
			// Test if this node was already used within the hierarchy(instanced)
			bool bDoesNodeHaveAnotherInstance = Context.UsedNodes.TryGetValue(Pointer, out int InstancePrototypeIndex);
			if (bDoesNodeHaveAnotherInstance)
			{
				Info($"!instance", Context.Level);
				Item.InstanceOf = InstancePrototypeIndex;
				Context.SceneContext.SceneItemList[InstancePrototypeIndex].bIsPrototype = true;

				if (Item.Node.IsGeometry)
				{
					InwNodeFragsColl Fragments = Item.Node.Fragments();
					Context.SceneContext.FragmentCount += Fragments.Count;
					Context.SceneContext.GeometryInstanceTotalCount++;
				}
				if (Item.Node.IsGroup)
				{
					Context.SceneContext.GroupInstanceTotalCount++;
				}
			}
			else
			{
				Context.UsedNodes.Add(Pointer, ItemIndex);
			}

			if (Node.IsGeometry)
			{
				Context.SceneContext.GeometryNodeCount++;
				InwNodeFragsColl Fragments = Node.Fragments();
				Info($"# IsGeometry (Fragments: {Fragments.Count})", Context.Level);
			}

			if (Node.IsInsert)
			{
				Context.SceneContext.InsertNodeCount++;
				Info($"# IsInsert ", Context.Level);
			}

			if (Node.IsLayer)
			{
				Context.SceneContext.LayerNodeCount++;
				Info($"# IsLayer ", Context.Level);
			}

			if (Node.IsGroup)
			{
				Context.SceneContext.GroupNodeCount ++;
				Info($"# IsGroup ", Context.Level);
				if (Node is InwOaGroup Group)
				{
					Info($"# Children ({Group.Children().Count})", Context.Level);
					int ChildIndex = 0;
					foreach (InwOaNode Child in Group.Children())
					{
						NodeContext ChildContext = new NodeContext(Context);
						CollectNodes(ChildContext, new SceneItem(Item, Child, ChildIndex), ProgressBar);
						ChildIndex++;
					}
				}
			}

			ProgressBar.Update((double) Context.SceneContext.SceneItemCount / Context.SceneContext.ModelItemCount);
		}
	}
}
