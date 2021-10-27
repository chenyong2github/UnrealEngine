// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Autodesk.Revit.DB;
using Autodesk.Revit.DB.Architecture;
using Autodesk.Revit.DB.Mechanical;
using Autodesk.Revit.DB.Plumbing;
using Autodesk.Revit.DB.Structure;
using Autodesk.Revit.DB.Visual;

namespace DatasmithRevitExporter
{
	public class FDocumentData
	{
		// This class reflects the child -> super component relationship in Revit into the exported hierarchy (children under super components actors).
		class FSuperComponentOptimizer
		{
			private Dictionary<FBaseElementData, Element> ElementDataToElementMap = new Dictionary<FBaseElementData, Element>();
			private Dictionary<ElementId, FBaseElementData> ElementIdToElementDataMap = new Dictionary<ElementId, FBaseElementData>();

			public void UpdateCache(FBaseElementData ParentElement, FBaseElementData ChildElement)
			{
				if (!ElementDataToElementMap.ContainsKey(ParentElement))
				{
					Element Elem = null;

					if (ParentElement.GetType() == typeof(FElementData))
					{
						Elem = ((FElementData)ParentElement).CurrentElement;
					}
					else if (ChildElement.GetType() == typeof(FElementData))
					{
						Elem = ((FElementData)ChildElement).CurrentElement;
					}

					if (Elem != null)
					{
						ElementDataToElementMap[ParentElement] = Elem;
						ElementIdToElementDataMap[Elem.Id] = ParentElement;
					}
				}
			}

			public void Optimize()
			{
				foreach (var KV in ElementDataToElementMap)
				{
					FBaseElementData ElemData = KV.Key;
					Element Elem = KV.Value;

					if ((Elem as FamilyInstance) != null)
					{
						Element Parent = (Elem as FamilyInstance).SuperComponent;

						if (Parent != null)
						{
							FBaseElementData SuperParent = null;
							bool bGot = ElementIdToElementDataMap.TryGetValue(Parent.Id, out SuperParent);

							if (bGot && SuperParent != ElemData.Parent)
							{
								if (ElemData.Parent != null)
								{
									ElemData.Parent.ElementActor.RemoveChild(ElemData.ElementActor);
									ElemData.Parent.ChildElements.Remove(ElemData);
								}

								SuperParent.ChildElements.Add(ElemData);
								SuperParent.ElementActor.AddChild(ElemData.ElementActor);
							}
						}
					}
				}
			}
		};

		public struct FPolymeshFace
		{
			public int V1;
			public int V2;
			public int V3;
			public int MaterialIndex;

			public FPolymeshFace(int InVertex1, int InVertex2, int InVertex3, int InMaterialIndex = 0)
			{
				V1 = InVertex1;
				V2 = InVertex2;
				V3 = InVertex3;
				MaterialIndex = InMaterialIndex;
			}
		}

		public class FDatasmithPolymesh
		{
			public List<XYZ> Vertices = new List<XYZ>();
			public List<XYZ> Normals = new List<XYZ>();
			public List<FPolymeshFace> Faces = new List<FPolymeshFace>();
			public List<UV> UVs = new List<UV>();
		}

		public class FBaseElementData
		{
			public ElementType					BaseElementType;
			public FDatasmithPolymesh			DatasmithPolymesh = null;
			public FDatasmithFacadeMeshElement	DatasmithMeshElement = null;
			public FDatasmithFacadeActor		ElementActor = null;
			public FDatasmithFacadeMetaData		ElementMetaData = null;
			public FDocumentData				DocumentData = null;
			public bool							bOptimizeHierarchy = true;
			public bool							bIsModified = true;
			public bool							bAllowMeshInstancing = true;

			public Dictionary<string, int>		MeshMaterialsMap = new Dictionary<string, int>();

			public Transform WorldTransform;

			public List<FBaseElementData>	ChildElements = new List<FBaseElementData>();

			public FBaseElementData			Parent = null;

			public FBaseElementData(
				ElementType InElementType, FDocumentData InDocumentData
			)
			{
				BaseElementType = InElementType;
				DocumentData = InDocumentData;
			}

			public FBaseElementData(FDatasmithFacadeActor InElementActor, FDatasmithFacadeMetaData InElementMetaData, FDocumentData InDocumentData)
			{
				ElementActor = InElementActor;
				ElementMetaData = InElementMetaData;
				DocumentData = InDocumentData;
			}

			public bool IsSimpleActor()
			{
				FDatasmithFacadeActorMesh MeshActor = ElementActor as FDatasmithFacadeActorMesh;

				if (MeshActor != null && MeshActor.GetMeshName().Length == 0)
				{
					ElementActor = new FDatasmithFacadeActor(MeshActor.GetName());
					CopyActorData(MeshActor, ElementActor);
					return true;
				}

				return !(ElementActor is FDatasmithFacadeActorMesh || ElementActor is FDatasmithFacadeActorLight || ElementActor is FDatasmithFacadeActorCamera);
			}

			void CopyActorData(FDatasmithFacadeActor InFromActor, FDatasmithFacadeActor InToActor)
			{
				InToActor.SetLabel(InFromActor.GetLabel());

				float X, Y, Z, W;
				InFromActor.GetTranslation(out X, out Y, out Z);
				InToActor.SetTranslation(X, Y, Z);
				InFromActor.GetScale(out X, out Y, out Z);
				InToActor.SetScale(X, Y, Z);
				InFromActor.GetRotation(out X, out Y, out Z, out W);
				InToActor.SetRotation(X, Y, Z, W);

				InToActor.SetLayer(InFromActor.GetLayer());

				for (int TagIndex = 0; TagIndex < InFromActor.GetTagsCount(); ++TagIndex)
				{
					InToActor.AddTag(InFromActor.GetTag(TagIndex));
				}

				InToActor.SetIsComponent(InFromActor.IsComponent());
				InToActor.SetVisibility(InFromActor.GetVisibility());

				for (int ChildIndex = 0; ChildIndex < InFromActor.GetChildrenCount(); ++ChildIndex)
				{
					InToActor.AddChild(InFromActor.GetChild(ChildIndex));
				}

				ElementMetaData?.SetAssociatedElement(InToActor);
			}

			public void AddToScene(FDatasmithFacadeScene InScene, FBaseElementData InParent, bool bInSkipChildren, bool bInForceAdd = false)
			{
				Element ThisElement = (this as FElementData)?.CurrentElement;

				if (!bInSkipChildren)
				{
					foreach (FBaseElementData CurrentChild in ChildElements)
					{
						// Stairs get special treatment: elements of stairs (strings, landings etc.) can be duplicated,
						// meaning that the same element id can exist multiple times under the same parent.
						bool bIsStairsElement = (ThisElement != null) && (ThisElement.GetType() == typeof(Stairs));

						bool bIsInstance = (ThisElement == null);
						bool bForceAdd = (bIsInstance && bIsModified) || (bIsStairsElement && bIsModified);

						CurrentChild.AddToScene(InScene, this, false, bForceAdd);
					}
				}

				bool bIsCached = 
					ThisElement != null && 
					ThisElement.IsValidObject && 
					(DocumentData.DirectLink?.IsElementCached(ThisElement) ?? false);

				// Check if actor type has changed for this element (f.e. static mesh actor -> regular actor),
				// and re-created if needed.
				if (bIsCached && bIsModified && ElementActor != null)
				{
					FDatasmithFacadeActor CachedActor = DocumentData.DirectLink.GetCachedActor(ElementActor.GetName());

					if (CachedActor != null && CachedActor.GetType() != ElementActor.GetType())
					{
						InScene.RemoveActor(CachedActor);
						DocumentData.DirectLink.CacheActorType(ElementActor);
						bIsCached = false;
					}
				}

				if ((!bIsCached && bIsModified) || bInForceAdd)
				{
					if (InParent == null)
					{
						InScene.AddActor(ElementActor);
					}
					else
					{
						InParent.ElementActor.AddChild(ElementActor);
					}

					if (!DocumentData.bSkipMetadataExport && ElementMetaData != null)
					{
						InScene.AddMetaData(ElementMetaData);
					}

					if (ThisElement != null)
					{
						DocumentData.DirectLink?.CacheElement(DocumentData.CurrentDocument, ThisElement, this);
					}
				}

				bIsModified = false;
			}

			public bool Optimize()
			{
				bool bIsSimpleActor = IsSimpleActor();

				List<FBaseElementData> ChildrenToRemove = new List<FBaseElementData>();

				foreach (FBaseElementData CurrentChild in ChildElements)
				{
					if (CurrentChild.Optimize())
					{
						ChildrenToRemove.Add(CurrentChild);
					}
				}

				foreach (FBaseElementData Child in ChildrenToRemove)
				{
					ChildElements.Remove(Child);
				}

				return bIsSimpleActor && (ChildElements.Count == 0) && bOptimizeHierarchy;
			}

			public void UpdateMeshName()
			{
				FDatasmithFacadeActorMesh MeshActor = ElementActor as FDatasmithFacadeActorMesh;
				if (MeshActor == null && DocumentData.DirectLink != null)
				{
					// We have valid mesh but the actor is not a mesh actor -- the type of element has changed (DirectLink).
					MeshActor = new FDatasmithFacadeActorMesh(ElementActor.GetName());
					CopyActorData(ElementActor, MeshActor);
					ElementActor = MeshActor;
				}
				MeshActor?.SetMesh(DatasmithMeshElement.GetName());
				bOptimizeHierarchy = false;
			}
		}

		public class FElementData : FBaseElementData
		{
			public Element		CurrentElement = null;
			public Transform	MeshPointsTransform = null;

			private Stack<FBaseElementData> InstanceDataStack = new Stack<FBaseElementData>();

			public FElementData(
				Element InElement,
				Transform InWorldTransform,
				FDocumentData InDocumentData
			)
				: base(InElement.Document.GetElement(InElement.GetTypeId()) as ElementType, InDocumentData)
			{
				CurrentElement = InElement;

				InitializePivotPlacement(ref InWorldTransform);

				// Create a new Datasmith mesh actor.
				InitializeElement(InWorldTransform, this);
			}

			public void InitializePivotPlacement(ref Transform InOutWorldTransform)
			{
				// If element has location, use it as a transform in order to have better pivot placement.
				Transform PivotTransform = GetPivotTransform(CurrentElement);
				if (PivotTransform != null)
				{
					if (!InOutWorldTransform.IsIdentity)
					{
						InOutWorldTransform = InOutWorldTransform * PivotTransform;
					} 
					else
					{
						InOutWorldTransform = PivotTransform;
					}

					if (CurrentElement.GetType() == typeof(Wall)
						|| CurrentElement.GetType() == typeof(ModelText)
						|| CurrentElement.GetType().IsSubclassOf(typeof(MEPCurve))
						|| CurrentElement.GetType() == typeof(StructuralConnectionHandler)
						|| CurrentElement.GetType() == typeof(Floor)
						|| CurrentElement.GetType() == typeof(Ceiling)
						|| CurrentElement.GetType() == typeof(RoofBase)
						|| CurrentElement.GetType().IsSubclassOf(typeof(RoofBase)))
					{
						MeshPointsTransform = PivotTransform.Inverse;
					}
				}
			}

			// Compute orthonormal basis, given the X vector.
			static private void ComputeBasis(XYZ BasisX, ref XYZ BasisY, ref XYZ BasisZ)
			{
				BasisY = XYZ.BasisZ.CrossProduct(BasisX);
				if (BasisY.GetLength() < 0.0001)
				{
					// BasisX is aligned with Z, use dot product to take direction in account
					BasisY = BasisX.CrossProduct(BasisX.DotProduct(XYZ.BasisZ) * XYZ.BasisX).Normalize();
					BasisZ = BasisX.CrossProduct(BasisY).Normalize();
				}
				else
				{
					BasisY = BasisY.Normalize();
					BasisZ = BasisX.CrossProduct(BasisY).Normalize();
				}
			}

			private Transform GetPivotTransform(Element InElement)
			{
				if ((InElement as FamilyInstance) != null)
				{
					return null;
				}

				XYZ Translation = null;
				XYZ BasisX = new XYZ();
				XYZ BasisY = new XYZ();
				XYZ BasisZ = new XYZ();

				// Get pivot translation

				if (InElement.GetType() == typeof(Railing))
				{
					// Railings don't have valid location, so instead we need to get location from its path.
					IList<Curve> Paths = (InElement as Railing).GetPath();
					if (Paths.Count > 0 && Paths[0].IsBound)
					{
						Translation = Paths[0].GetEndPoint(0);
					}
				}
				else if (InElement.GetType() == typeof(StructuralConnectionHandler))
				{
					Translation = (InElement as StructuralConnectionHandler).GetOrigin();
				}
				else if (InElement.GetType() == typeof(Floor) 
					|| InElement.GetType() == typeof(Ceiling) 
					|| InElement.GetType() == typeof(RoofBase)
					|| InElement.GetType().IsSubclassOf(typeof(RoofBase)))
				{
					BoundingBoxXYZ BoundingBox = InElement.get_BoundingBox(InElement.Document.ActiveView);
					if (BoundingBox != null)
					{
						Translation = BoundingBox.Min;
					}
				}
				else if (InElement.Location != null)
				{
					if (InElement.Location.GetType() == typeof(LocationCurve))
					{
						LocationCurve CurveLocation = InElement.Location as LocationCurve;
						if (CurveLocation.Curve != null && CurveLocation.Curve.IsBound)
						{
							Translation = CurveLocation.Curve.GetEndPoint(0);
						}
					}
					else if (InElement.Location.GetType() == typeof(LocationPoint))
					{
						Translation = (InElement.Location as LocationPoint).Point;
					}
				}

				if (Translation == null)
				{
					return null; // Cannot get valid translation
				}

				// Get pivot basis

				if (InElement.GetType() == typeof(Wall))
				{
					// In rare cases, wall may not support orientation.
					// If this happens, we need to use the direction of its Curve property and 
					// derive orientation from there.
					try
					{
						BasisY = (InElement as Wall).Orientation.Normalize();
						BasisX = BasisY.CrossProduct(XYZ.BasisZ).Normalize();
						BasisZ = BasisX.CrossProduct(BasisY).Normalize();
					} 
					catch
					{
						if (InElement.Location.GetType() == typeof(LocationCurve))
						{
							LocationCurve CurveLocation = InElement.Location as LocationCurve;

							if (CurveLocation.Curve.GetType() == typeof(Line))
							{
								BasisX = (CurveLocation.Curve as Line).Direction;
								ComputeBasis(BasisX, ref BasisY, ref BasisZ);
							} 
							else if (CurveLocation.Curve.IsBound)
							{
								Transform Derivatives = CurveLocation.Curve.ComputeDerivatives(0f, true);
								BasisX = Derivatives.BasisX.Normalize();
								BasisY = Derivatives.BasisY.Normalize();
								BasisZ = Derivatives.BasisZ.Normalize();
							} 
							else 
							{
								BasisX = XYZ.BasisX;
								BasisY = XYZ.BasisY;
								BasisZ = XYZ.BasisZ;
							}
						}
					}
				}
				else if (InElement.GetType() == typeof(Railing))
				{
					IList<Curve> Paths = (InElement as Railing).GetPath();
					if (Paths.Count > 0)
					{
						Curve FirstPath = Paths[0];
						if (FirstPath.GetType() == typeof(Line))
						{
							BasisX = (FirstPath as Line).Direction.Normalize();
							ComputeBasis(BasisX, ref BasisY, ref BasisZ);
						}
						else if (FirstPath.GetType() == typeof(Arc) && FirstPath.IsBound)
						{
							Transform Derivatives = (FirstPath as Arc).ComputeDerivatives(0f, true);
							BasisX = Derivatives.BasisX.Normalize();
							BasisY = Derivatives.BasisY.Normalize();
							BasisZ = Derivatives.BasisZ.Normalize();
						}
					}
				}
				else if (InElement.GetType() == typeof(StructuralConnectionHandler))
				{
					BasisX = XYZ.BasisX;
					BasisY = XYZ.BasisY;
					BasisZ = XYZ.BasisZ;
				}
				else if (InElement.GetType() == typeof(ModelText))
				{
					// Model text has no direction information!
					BasisX = XYZ.BasisX;
					BasisY = XYZ.BasisY;
					BasisZ = XYZ.BasisZ;
				}
				else if (InElement.GetType() == typeof(FlexDuct))
				{
					BasisX = (InElement as FlexDuct).StartTangent;
					ComputeBasis(BasisX, ref BasisY, ref BasisZ);
				}
				else if (InElement.GetType() == typeof(FlexPipe))
				{
					BasisX = (InElement as FlexPipe).StartTangent;
					ComputeBasis(BasisX, ref BasisY, ref BasisZ);
				}
				else if (InElement.GetType() == typeof(Floor) 
					|| InElement.GetType() == typeof(Ceiling)
					|| InElement.GetType() == typeof(RoofBase)
					|| InElement.GetType().IsSubclassOf(typeof(RoofBase)))
				{
					BasisX = XYZ.BasisX;
					BasisY = XYZ.BasisY;
					BasisZ = XYZ.BasisZ;
				}
				else if (InElement.Location.GetType() == typeof(LocationCurve))
				{
					LocationCurve CurveLocation = InElement.Location as LocationCurve;

					if (CurveLocation.Curve.GetType() == typeof(Line))
					{
						BasisX = (CurveLocation.Curve as Line).Direction;
						ComputeBasis(BasisX, ref BasisY, ref BasisZ);
					}
					else if (CurveLocation.Curve.IsBound)
					{
						Transform Derivatives = CurveLocation.Curve.ComputeDerivatives(0f, true);
						BasisX = Derivatives.BasisX.Normalize();
						BasisY = Derivatives.BasisY.Normalize();
						BasisZ = Derivatives.BasisZ.Normalize();
					}
					else
					{
						return null;
					}
				}
				else
				{
					return null; // Cannot get valid basis
				}

				Transform PivotTransform = Transform.CreateTranslation(Translation);
				PivotTransform.BasisX = BasisX;
				PivotTransform.BasisY = BasisY;
				PivotTransform.BasisZ = BasisZ;

				return PivotTransform;
			}

			public FBaseElementData PushInstance(
				ElementType InInstanceType,
				Transform InWorldTransform,
				bool bInAllowMeshInstancing
			)
			{
				FBaseElementData InstanceData = new FBaseElementData(InInstanceType, DocumentData);
				InstanceData.bAllowMeshInstancing = bInAllowMeshInstancing;
				InstanceDataStack.Push(InstanceData);

				InitializeElement(InWorldTransform, InstanceData);

				// The Datasmith instance actor is a component in the hierarchy.
				InstanceData.ElementActor.SetIsComponent(true);

				return InstanceData;
			}

			public FBaseElementData PopInstance()
			{
				FBaseElementData Instance = InstanceDataStack.Pop();
				Instance.bIsModified = true;
				return Instance;
			}

			public FDatasmithFacadeMeshElement GetCurrentMeshElement()
			{
				if (InstanceDataStack.Count > 0)
				{
					return InstanceDataStack.Peek().DatasmithMeshElement;
				}
				return DatasmithMeshElement;
			}

			public void AddLightActor(
				Transform InWorldTransform,
				Asset InLightAsset
			)
			{
				// Create a new Datasmith light actor.
				// Hash the Datasmith light actor name to shorten it.
				string HashedActorName = FDatasmithFacadeElement.GetStringHash("L:" + GetActorName(true));
				FDatasmithFacadeActorLight LightActor = FDatasmithRevitLight.CreateLightActor(CurrentElement, HashedActorName);
				LightActor.SetLabel(GetActorLabel());

				// Set the world transform of the Datasmith light actor.
				FDocumentData.SetActorTransform(InWorldTransform, LightActor);

				// Set the base properties of the Datasmith light actor.
				string LayerName = Category.GetCategory(CurrentElement.Document, BuiltInCategory.OST_LightingFixtureSource)?.Name ?? "Light Sources";
				SetActorProperties(LayerName, LightActor);

				FDatasmithFacadeMetaData LightMetaData = GetActorMetaData(LightActor);

				// Set the Datasmith light actor layer to its predefined name.
				string CategoryName = Category.GetCategory(CurrentElement.Document, BuiltInCategory.OST_LightingFixtureSource)?.Name ?? "Light Sources";
				LightActor.SetLayer(CategoryName);

				// Set the specific properties of the Datasmith light actor.
				FDatasmithRevitLight.SetLightProperties(InLightAsset, CurrentElement, LightActor);

				// Add the light actor to the Datasmith actor hierarchy.
				AddChildActor(LightActor, LightMetaData, false);
			}

			public bool AddRPCActor(
				Transform InWorldTransform,
				Asset InRPCAsset,
				FMaterialData InMaterialData,
				out FDatasmithFacadeMesh OutDatasmithMesh,
				out FDatasmithFacadeMeshElement OutDatasmithMeshElement
			)
			{
				// Create a new Datasmith RPC mesh.
				// Hash the Datasmith RPC mesh name to shorten it.
				string HashedName = FDatasmithFacadeElement.GetStringHash("RPCM:" + GetActorName(false));
				FDatasmithFacadeMesh RPCMesh = new FDatasmithFacadeMesh();
				RPCMesh.SetName(HashedName);
				Transform AffineTransform = Transform.Identity;

				LocationPoint RPCLocationPoint = CurrentElement.Location as LocationPoint;

				if (RPCLocationPoint != null)
				{
					if (RPCLocationPoint.Rotation != 0.0)
					{
						AffineTransform = AffineTransform.Multiply(Transform.CreateRotation(XYZ.BasisZ, -RPCLocationPoint.Rotation));
						AffineTransform = AffineTransform.Multiply(Transform.CreateTranslation(RPCLocationPoint.Point.Negate()));
					}
					else
					{
						AffineTransform = Transform.CreateTranslation(RPCLocationPoint.Point.Negate());
					}
				}

				int TotalVertexCount = 0;
				int TotalTriangleCount = 0;
				List<Mesh> GeometryObjectList = new List<Mesh>();
				foreach (GeometryObject RPCGeometryObject in CurrentElement.get_Geometry(new Options()))
				{
					GeometryInstance RPCGeometryInstance = RPCGeometryObject as GeometryInstance;
					if (RPCGeometryInstance == null)
					{
						continue;
					}

					foreach (GeometryObject RPCInstanceGeometryObject in RPCGeometryInstance.GetInstanceGeometry())
					{
						Mesh RPCInstanceGeometryMesh = RPCInstanceGeometryObject as Mesh;
						if (RPCInstanceGeometryMesh == null || RPCInstanceGeometryMesh.NumTriangles < 1)
						{
							continue;
						}

						TotalVertexCount += RPCInstanceGeometryMesh.Vertices.Count;
						TotalTriangleCount += RPCInstanceGeometryMesh.NumTriangles;
						GeometryObjectList.Add(RPCInstanceGeometryMesh);
					}
				}

				RPCMesh.SetVerticesCount(TotalVertexCount);
				RPCMesh.SetFacesCount(TotalTriangleCount * 2); // Triangles are added twice for RPC meshes: CW & CCW

				int MeshMaterialIndex = 0;
				int VertexIndexOffset = 0;
				int TriangleIndexOffset = 0;
				foreach (Mesh RPCInstanceGeometryMesh in GeometryObjectList)
				{
					// RPC geometry does not have normals nor UVs available through the Revit Mesh interface.
					int VertexCount = RPCInstanceGeometryMesh.Vertices.Count;
					int TriangleCount = RPCInstanceGeometryMesh.NumTriangles;

					// Add the RPC geometry vertices to the Datasmith RPC mesh.
					for (int VertexIndex = 0; VertexIndex < RPCInstanceGeometryMesh.Vertices.Count; ++VertexIndex)
					{
						XYZ PositionedVertex = AffineTransform.OfPoint(RPCInstanceGeometryMesh.Vertices[VertexIndex]);
						RPCMesh.SetVertex(VertexIndexOffset + VertexIndex, (float)PositionedVertex.X, (float)PositionedVertex.Y, (float)PositionedVertex.Z);
					}

					// Add the RPC geometry triangles to the Datasmith RPC mesh.
					for (int TriangleNo = 0, BaseTriangleIndex = 0; TriangleNo < TriangleCount; TriangleNo++, BaseTriangleIndex += 2)
					{
						MeshTriangle Triangle = RPCInstanceGeometryMesh.get_Triangle(TriangleNo);

						try
						{
							int Index0 = VertexIndexOffset + Convert.ToInt32(Triangle.get_Index(0));
							int Index1 = VertexIndexOffset + Convert.ToInt32(Triangle.get_Index(1));
							int Index2 = VertexIndexOffset + Convert.ToInt32(Triangle.get_Index(2));

							// Add triangles for both the front and back faces.
							RPCMesh.SetFace(TriangleIndexOffset + BaseTriangleIndex, Index0, Index1, Index2, MeshMaterialIndex);
							RPCMesh.SetFace(TriangleIndexOffset + BaseTriangleIndex + 1, Index2, Index1, Index0, MeshMaterialIndex);
						}
						catch (OverflowException)
						{
							continue;
						}
					}

					VertexIndexOffset += VertexCount;
					TriangleIndexOffset += TriangleCount * 2;
				}

				// Create a new Datasmith RPC mesh actor.
				// Hash the Datasmith RPC mesh actor name to shorten it.
				string HashedActorName = FDatasmithFacadeElement.GetStringHash("RPC:" + GetActorName(true));
				FDatasmithFacadeActor FacadeActor;
				if (RPCMesh.GetVerticesCount() > 0 && RPCMesh.GetFacesCount() > 0)
				{
					FDatasmithFacadeActorMesh RPCMeshActor = new FDatasmithFacadeActorMesh(HashedActorName);
					RPCMeshActor.SetMesh(RPCMesh.GetName());
					FacadeActor = RPCMeshActor;
					
					OutDatasmithMesh = RPCMesh;
					OutDatasmithMeshElement = new FDatasmithFacadeMeshElement(HashedName);
					OutDatasmithMeshElement.SetLabel(GetActorLabel());
					OutDatasmithMeshElement.SetMaterial(InMaterialData.MasterMaterial.GetName(), MeshMaterialIndex);
				}
				else
				{
					//Create an empty actor instead of a static mesh actor with no mesh.
					FacadeActor = new FDatasmithFacadeActor(HashedActorName);

					OutDatasmithMesh = null;
					OutDatasmithMeshElement = null;
				}
				FacadeActor.SetLabel(GetActorLabel());

				// Set the world transform of the Datasmith RPC mesh actor.
				FDocumentData.SetActorTransform(InWorldTransform, FacadeActor);

				// Set the base properties of the Datasmith RPC mesh actor.
				string LayerName = GetCategoryName();
				SetActorProperties(LayerName, FacadeActor);

				// Add a Revit element RPC tag to the Datasmith RPC mesh actor.
				FacadeActor.AddTag("Revit.Element.RPC");

				// Add some Revit element RPC metadata to the Datasmith RPC mesh actor.
				AssetProperty RPCTypeId = InRPCAsset.FindByName("RPCTypeId");
				AssetProperty RPCFilePath = InRPCAsset.FindByName("RPCFilePath");

				FDatasmithFacadeMetaData ElementMetaData = new FDatasmithFacadeMetaData(FacadeActor.GetName() + "_DATA");
				ElementMetaData.SetLabel(FacadeActor.GetLabel());
				ElementMetaData.SetAssociatedElement(FacadeActor);

				if (RPCTypeId != null)
				{
					ElementMetaData.AddPropertyString("Type*RPCTypeId", (RPCTypeId as AssetPropertyString).Value);
				}

				if (RPCFilePath != null)
				{
					ElementMetaData.AddPropertyString("Type*RPCFilePath", (RPCFilePath as AssetPropertyString).Value);
				}

				// Add the RPC mesh actor to the Datasmith actor hierarchy.
				AddChildActor(FacadeActor, ElementMetaData, false);

				return OutDatasmithMesh != null;
			}

			public void AddChildActor(
				FBaseElementData InChildActor
			)
			{
				FBaseElementData ParentElement = (InstanceDataStack.Count == 0) ? this : InstanceDataStack.Peek();

				ParentElement.ChildElements.Add(InChildActor);
				InChildActor.Parent = ParentElement;
			}

			public void AddChildActor(
				FDatasmithFacadeActor ChildActor, 
				FDatasmithFacadeMetaData MetaData, 
				bool bOptimizeHierarchy
			)
			{
				FBaseElementData ElementData = new FBaseElementData(ChildActor, MetaData, DocumentData);
				ElementData.bOptimizeHierarchy = bOptimizeHierarchy;

				FBaseElementData Parent = (InstanceDataStack.Count == 0) ? this : InstanceDataStack.Peek();

				Parent.ChildElements.Add(ElementData);
				ElementData.Parent = Parent;
			}

			public void InitializeElement(
					Transform InWorldTransform,
					FBaseElementData InElement
			)
			{
				InElement.WorldTransform = InWorldTransform;

				// Create a new Datasmith mesh.
				// Hash the Datasmith mesh name to shorten it.
				string HashedMeshName = FDatasmithFacadeElement.GetStringHash("M:" + GetMeshName());
				InElement.DatasmithPolymesh = new FDatasmithPolymesh();
				InElement.DatasmithMeshElement = new FDatasmithFacadeMeshElement(HashedMeshName);
				InElement.DatasmithMeshElement.SetLabel(GetActorLabel());

				if (InElement.ElementActor == null)
				{
					// Create a new Datasmith mesh actor.
					// Hash the Datasmith mesh actor name to shorten it.
					string HashedActorName = FDatasmithFacadeElement.GetStringHash("A:" + GetActorName(true));
					InElement.ElementActor = new FDatasmithFacadeActorMesh(HashedActorName);
					InElement.ElementActor.SetLabel(GetActorLabel());
				}

				// Set the world transform of the Datasmith mesh actor.
				FDocumentData.SetActorTransform(InWorldTransform, InElement.ElementActor);

				// Set the base properties of the Datasmith mesh actor.
				string LayerName = GetCategoryName();
				SetActorProperties(LayerName, InElement.ElementActor);

				if (!DocumentData.bSkipMetadataExport)
				{
					InElement.ElementMetaData = GetActorMetaData(InElement.ElementActor);
				}
			}

			public string GetCategoryName()
			{
				return BaseElementType?.Category?.Name ?? CurrentElement.Category?.Name;
			}

			public bool IgnoreElementGeometry()
			{
				// Ignore elements that have unwanted geometry, such as level symbols.
				return (BaseElementType as LevelType) != null;
			}

			public FDatasmithPolymesh GetCurrentPolymesh()
			{
				if (InstanceDataStack.Count == 0)
				{
					return DatasmithPolymesh;
				}
				else
				{
					return InstanceDataStack.Peek().DatasmithPolymesh;
				}
			}

			public FBaseElementData PeekInstance()
			{
				return InstanceDataStack.Count > 0 ? InstanceDataStack.Peek() : null;
			}

			public FBaseElementData GetCurrentActor()
			{
				if (InstanceDataStack.Count == 0)
				{
					return this;
				}
				else
				{
					return InstanceDataStack.Peek();
				}
			}

			public void Log(
				FDatasmithFacadeLog InDebugLog,
				string InLinePrefix,
				int InLineIndentation
			)
			{
				if (InDebugLog != null)
				{
					if (InLineIndentation < 0)
					{
						InDebugLog.LessIndentation();
					}

					Element SourceElement = (InstanceDataStack.Count == 0) ? CurrentElement : InstanceDataStack.Peek().BaseElementType;

					InDebugLog.AddLine($"{InLinePrefix} {SourceElement.Id.IntegerValue} '{SourceElement.Name}' {SourceElement.GetType()}: '{GetActorLabel()}'");

					if (InLineIndentation > 0)
					{
						InDebugLog.MoreIndentation();
					}
				}
			}

			private string GetActorName(bool bEnsureUnique)
			{
				string ActorName;

				if (InstanceDataStack.Count == 0)
				{
					ActorName = $"{DocumentData.DocumentId}:{CurrentElement.UniqueId}";
				}
				else
				{
					ActorName = GenerateUniqueInstanceName();
				}

				if (bEnsureUnique && DocumentData.DirectLink != null)
				{
					ActorName = DocumentData.DirectLink.EnsureUniqueActorName(ActorName);
				}

				return ActorName;
			}

			private string GenerateUniqueInstanceName()
			{
				// GenerateUniqueInstanceName is being called when generating a name for instance. 
				// After the call, the intance is added as a child to its parent. 
				// Next time the method gets called for the next instance, ChildElements.Count will be different/incremented.

				// To add uniqueness to the generated name, we construct a string with child counts from 
				// current parent instance, up to the root:
				// Elem->Instance->Instance->Instace can produce something like: "1:5:3" for example.
				// However, this is not enough because elsewhere we might encounter the same sequence in terms of child counts, 
				// but adding the CurrentElement unique id ensures we get unique name string in the end.

				StringBuilder ChildCounts = new StringBuilder();

				for (int ElemIndex = 1; ElemIndex < InstanceDataStack.Count; ++ElemIndex)
				{
					FBaseElementData Elem = InstanceDataStack.ElementAt(ElemIndex);
					ChildCounts.AppendFormat(":{0}", Elem.ChildElements.Count);
				}

				// Add child count for the root element (parent of all instances)
				ChildCounts.AppendFormat(":{0}", ChildElements.Count);

				FBaseElementData Instance = InstanceDataStack.Peek();
				return $"{DocumentData.DocumentId}:{CurrentElement.UniqueId}:{Instance.BaseElementType.UniqueId}{ChildCounts.ToString()}";
			}

			private string GetMeshName()
			{
				if (InstanceDataStack.Count == 0)
				{
					return $"{DocumentData.DocumentId}:{CurrentElement.UniqueId}";
				}
				else
				{
					FBaseElementData Instance = InstanceDataStack.Peek();

					if (!Instance.bAllowMeshInstancing)
					{
						return GenerateUniqueInstanceName();
					}
					else
					{
						// Generate instanced mesh name
						return $"{DocumentData.DocumentId}:{Instance.BaseElementType.UniqueId}";
					}
				}
			}

			private string GetActorLabel()
			{
				string CategoryName = GetCategoryName();
				string FamilyName = BaseElementType?.FamilyName;
				string TypeName = BaseElementType?.Name;
				string InstanceName = (InstanceDataStack.Count > 1) ? InstanceDataStack.Peek().BaseElementType?.Name : null;

				string ActorLabel = "";

				if (CurrentElement as Level != null)
				{
					ActorLabel += string.IsNullOrEmpty(FamilyName) ? "" : FamilyName + "*";
					ActorLabel += string.IsNullOrEmpty(TypeName) ? "" : TypeName + "*";
					ActorLabel += CurrentElement.Name;
				}
				else
				{
					ActorLabel += string.IsNullOrEmpty(CategoryName) ? "" : CategoryName + "*";
					ActorLabel += string.IsNullOrEmpty(FamilyName) ? "" : FamilyName + "*";
					ActorLabel += string.IsNullOrEmpty(TypeName) ? CurrentElement.Name : TypeName;
					ActorLabel += string.IsNullOrEmpty(InstanceName) ? "" : "*" + InstanceName;
				}

				return ActorLabel;
			}

			private void SetActorProperties(
				string InLayerName,
				FDatasmithFacadeActor IOActor
			)
			{
				// Set the Datasmith actor layer to the element type category name.
				IOActor.SetLayer(InLayerName);

				// Add the Revit element ID and Unique ID tags to the Datasmith actor.
				IOActor.AddTag($"Revit.Element.Id.{CurrentElement.Id.IntegerValue}");
				IOActor.AddTag($"Revit.Element.UniqueId.{CurrentElement.UniqueId}");

				// For an hosted Revit family instance, add the host ID, Unique ID and Mirrored/Flipped flags as tags to the Datasmith actor.
				FamilyInstance CurrentFamilyInstance = CurrentElement as FamilyInstance;
				if (CurrentFamilyInstance != null)
				{
					IOActor.AddTag($"Revit.DB.FamilyInstance.Mirrored.{CurrentFamilyInstance.Mirrored}");
					IOActor.AddTag($"Revit.DB.FamilyInstance.HandFlipped.{CurrentFamilyInstance.HandFlipped}");
					IOActor.AddTag($"Revit.DB.FamilyInstance.FaceFlipped.{CurrentFamilyInstance.FacingFlipped}");

					if (CurrentFamilyInstance.Host != null)
					{
						IOActor.AddTag($"Revit.Host.Id.{CurrentFamilyInstance.Host.Id.IntegerValue}");
						IOActor.AddTag($"Revit.Host.UniqueId.{CurrentFamilyInstance.Host.UniqueId}");
					}
				}
			}

			private FDatasmithFacadeMetaData GetActorMetaData(FDatasmithFacadeActor IOActor)
			{
				FDatasmithFacadeMetaData ElementMetaData = new FDatasmithFacadeMetaData(IOActor.GetName() + "_DATA");
				ElementMetaData.SetLabel(IOActor.GetLabel());
				ElementMetaData.SetAssociatedElement(IOActor);

				// Add the Revit element category name metadata to the Datasmith actor.
				string CategoryName = GetCategoryName();
				if (!string.IsNullOrEmpty(CategoryName))
				{
					ElementMetaData.AddPropertyString("Element*Category", CategoryName);
				}

				// Add the Revit element family name metadata to the Datasmith actor.
				string FamilyName = BaseElementType?.FamilyName;
				if (!string.IsNullOrEmpty(FamilyName))
				{
					ElementMetaData.AddPropertyString("Element*Family", FamilyName);
				}

				// Add the Revit element type name metadata to the Datasmith actor.
				string TypeName = BaseElementType?.Name;
				if (!string.IsNullOrEmpty(TypeName))
				{
					ElementMetaData.AddPropertyString("Element*Type", TypeName);
				}

				// Add Revit element metadata to the Datasmith actor.
				FDocumentData.AddActorMetadata(CurrentElement, "Element*", ElementMetaData);

				if (BaseElementType != null)
				{
					// Add Revit element type metadata to the Datasmith actor.
					FDocumentData.AddActorMetadata(BaseElementType, "Type*", ElementMetaData);
				}

				return ElementMetaData;
			}
		}

		public Dictionary<string, Tuple<FDatasmithFacadeMeshElement, Task<bool>>> 
														MeshMap = new Dictionary<string, Tuple<FDatasmithFacadeMeshElement, Task<bool>>>();
		public Dictionary<ElementId, FBaseElementData>	ActorMap = new Dictionary<ElementId, FDocumentData.FBaseElementData>();
		public Dictionary<string, FMaterialData>		MaterialDataMap = null;
		public Dictionary<string, FMaterialData>		NewMaterialsMap = new Dictionary<string, FMaterialData>();

		private Stack<FElementData>						ElementDataStack = new Stack<FElementData>();
		private string									CurrentMaterialName = null;
		private List<string>							MessageList = null;

		public  string									DocumentId { get; private set; } = "";

		public bool										bSkipMetadataExport { get; private set; } = false;
		public Document									CurrentDocument { get; private set; } = null;
		public FDirectLink								DirectLink { get; private set; } = null;

		public List<Outline>							SectionBoxOutlines = new List<Outline>();

		public FDocumentData(
			Document InDocument,
			ref List<string> InMessageList,
			FDirectLink InDirectLink,
			string InLinkedDocumentId
		)
		{
			DirectLink = InDirectLink;
			CurrentDocument = InDocument;
			MessageList = InMessageList;
			// With DirectLink, we delay export of metadata for a faster initial export.
			bSkipMetadataExport = (DirectLink != null);

			if (DirectLink != null)
			{
				MaterialDataMap = DirectLink.MaterialDataMap;
			}
			else
			{
				MaterialDataMap = new Dictionary<string, FMaterialData>();
			}

			// Cache document section boxes
			if (CurrentDocument.ActiveView != null)
			{
				FilteredElementCollector Collector = new FilteredElementCollector(CurrentDocument, CurrentDocument.ActiveView.Id);
				IList<Element> SectionBoxes = Collector.OfCategory(BuiltInCategory.OST_SectionBox).ToElements();

				foreach (var SectionBox in SectionBoxes)
				{
					BoundingBoxXYZ BBox = SectionBox.get_BoundingBox(CurrentDocument.ActiveView);
					SectionBoxOutlines.Add(GetOutline(BBox.Transform, BBox));
				}
			}

			if (InLinkedDocumentId != null)
			{
				DocumentId = InLinkedDocumentId;
			}
		}

		private Outline GetOutline(Transform InTransform, BoundingBoxXYZ InBoundingBox)
		{
			XYZ A = InTransform.OfPoint(InBoundingBox.Min);
			XYZ B = InTransform.OfPoint(InBoundingBox.Max);

			XYZ PMin = new XYZ(
					Math.Min(A.X, B.X),
					Math.Min(A.Y, B.Y),
					Math.Min(A.Z, B.Z));

			XYZ PMax = new XYZ(
					Math.Max(A.X, B.X),
					Math.Max(A.Y, B.Y),
					Math.Max(A.Z, B.Z));

			return new Outline(PMin, PMax);
		}

		public Element GetElement(
			ElementId InElementId
		)
		{
			return (InElementId != ElementId.InvalidElementId) ? CurrentDocument.GetElement(InElementId) : null;
		}

		public bool ContainsMesh(string MeshName)
		{
			return MeshMap.ContainsKey(MeshName);
		}

		public bool PushElement(
			Element InElement,
			Transform InWorldTransform
		)
		{
			DirectLink?.MarkForExport(InElement);

			FElementData ElementData = null;

			if (ActorMap.ContainsKey(InElement.Id))
			{
				if (DirectLink != null && DirectLink.IsElementCached(InElement))
				{
					return false;
				}
				ElementData = ActorMap[InElement.Id] as FElementData;
			}
			
			if (ElementData == null)
			{
				if (DirectLink?.IsElementCached(InElement) ?? false)
				{
					ElementData = (FElementData)DirectLink.GetCachedElement(InElement);

					bool bIsLinkedDocument = InElement.GetType() == typeof(RevitLinkInstance);

					if (bIsLinkedDocument || DirectLink.IsElementModified(InElement))
					{
						FDatasmithFacadeActor ExistingActor = ElementData.ElementActor;

						// Remove children that are instances: they will be re-created;
						// The reason is that we cannot uniquely identify family instances (no id) and when element changes,
						// we need to export all of its child instances anew.
						if (ExistingActor != null)
						{
							if (ElementData.ChildElements.Count > 0)
							{
								List<FBaseElementData> ChildrenToRemove = new List<FBaseElementData>();
					
								for(int ChildIndex = 0; ChildIndex < ElementData.ChildElements.Count; ++ChildIndex)
								{
									FBaseElementData ChildElement = ElementData.ChildElements[ChildIndex];

									bool bIsFamilyIntance = 
										((ChildElement as FElementData) == null) && 
										ChildElement.ElementActor.IsComponent();

									if (bIsFamilyIntance)
									{
										ChildrenToRemove.Add(ChildElement);
									}
								}

								foreach (FBaseElementData Child in ChildrenToRemove)
								{
									ExistingActor.RemoveChild(Child.ElementActor);
									ElementData.ChildElements.Remove(Child);
								}
							}

							ExistingActor.ResetTags();
							(ExistingActor as FDatasmithFacadeActorMesh)?.SetMesh(null);
						}

						ElementData.InitializePivotPlacement(ref InWorldTransform);
						ElementData.InitializeElement(InWorldTransform, ElementData);
						ElementData.MeshMaterialsMap.Clear();
					}
					else
					{
						ActorMap[InElement.Id] = ElementData;
						return false; // We have up to date cache for this element.
					}
				}
				else
				{
					ElementData = new FElementData(InElement, InWorldTransform, this);
				}
			}

			ElementDataStack.Push(ElementData);
			ElementDataStack.Peek().ElementActor.AddTag("IsElement");

			return true;
		}

		public void PopElement(FDatasmithFacadeScene InDatasmithScene)
		{
			FElementData ElementData = ElementDataStack.Pop();
			FDatasmithPolymesh DatasmithPolymesh = ElementData.DatasmithPolymesh;

			if(DatasmithPolymesh.Vertices.Count > 0 && DatasmithPolymesh.Faces.Count > 0)
			{
				ElementData.UpdateMeshName();
			}

			CollectMesh(ElementData.DatasmithPolymesh, ElementData.DatasmithMeshElement, InDatasmithScene);

			DirectLink?.ClearModified(ElementData.CurrentElement);

			ElementData.bIsModified = true;

			ElementId ElemId = ElementData.CurrentElement.Id;

			if (ElementDataStack.Count == 0)
			{
				if (ActorMap.ContainsKey(ElemId) && ActorMap[ElemId] != ElementData)
				{
					// Handle the spurious case of Revit Custom Exporter calling back more than once for the same element.
					// These extra empty actors will be cleaned up later by the Datasmith actor hierarchy optimization.
					ActorMap[ElemId].ChildElements.Add(ElementData);
					ElementData.Parent = ActorMap[ElemId];
				}
				else
				{
					// Collect the element mesh actor into the Datasmith actor dictionary.
					ActorMap[ElemId] = ElementData;
				}
			}
			else
			{
				if (!ActorMap.ContainsKey(ElemId))
				{
					// Add the element mesh actor to the Datasmith actor hierarchy.
					ElementDataStack.Peek().AddChildActor(ElementData);
				}
			}
		}

		private static FDatasmithFacadeActor DuplicateBaseActor(FDatasmithFacadeActor SourceActor)
		{
			FDatasmithFacadeActor CloneActor = new FDatasmithFacadeActor(SourceActor.GetName());
			CloneActor.SetLabel(SourceActor.GetLabel());

			float X, Y, Z, W;
			SourceActor.GetTranslation(out X, out Y, out Z);
			CloneActor.SetTranslation(X, Y, Z);
			SourceActor.GetScale(out X, out Y, out Z);
			CloneActor.SetScale(X, Y, Z);
			SourceActor.GetRotation(out X, out Y, out Z, out W);
			CloneActor.SetRotation(X, Y, Z, W);

			CloneActor.SetLayer(SourceActor.GetLayer());

			for (int TagIndex = 0; TagIndex < SourceActor.GetTagsCount(); ++TagIndex)
			{
				CloneActor.AddTag(SourceActor.GetTag(TagIndex));
			}

			CloneActor.SetIsComponent(SourceActor.IsComponent());
			CloneActor.SetVisibility(SourceActor.GetVisibility());

			for (int ChildIndex = 0; ChildIndex < SourceActor.GetChildrenCount(); ++ChildIndex)
			{
				CloneActor.AddChild(SourceActor.GetChild(ChildIndex));
			}

			return CloneActor;
		}

		public void PushInstance(
			ElementType InInstanceType,
			Transform InWorldTransform
		)
		{
			// Check if this instance intersects any section box.
			// If so, we can't instance its mesh--will be considered unique.

			bool bIntersectedBySectionBox = false;

			if (SectionBoxOutlines.Count > 0)
			{
				BoundingBoxXYZ InstanceBoundingBox = InInstanceType.get_BoundingBox(CurrentDocument.ActiveView);

				if (InstanceBoundingBox != null)
				{
					Outline InstanceOutline = GetOutline(InWorldTransform, InstanceBoundingBox);

					foreach (Outline SectionBoxOutline in SectionBoxOutlines)
					{
						bIntersectedBySectionBox = (SectionBoxOutline.Intersects(InstanceOutline, 0) != SectionBoxOutline.ContainsOtherOutline(InstanceOutline, 0));
						if (bIntersectedBySectionBox)
						{
							break;
						}
					}
				}
			}

			FElementData CurrentElementData = ElementDataStack.Peek();
			FBaseElementData NewInstance = CurrentElementData.PushInstance(InInstanceType, InWorldTransform, !bIntersectedBySectionBox);
		}

		public void PopInstance(FDatasmithFacadeScene InDatasmithScene)
		{
			FElementData CurrentElement = ElementDataStack.Peek();
			FBaseElementData InstanceData = CurrentElement.PopInstance();
			FDatasmithPolymesh DatasmithPolymesh = InstanceData.DatasmithPolymesh;

			if (ContainsMesh(InstanceData.DatasmithMeshElement.GetName()) || (DatasmithPolymesh.Vertices.Count > 0 && DatasmithPolymesh.Faces.Count > 0))
			{
				InstanceData.UpdateMeshName();
			}
			else
			{
				/* Instance has no mesh.
				 * Handle the case where instance has valid transform, but parent element has valid mesh (exported after instance gets finished),
				 * in which case we want to apply the instance transform as a pivot transform.
				 * This is a case currently encountered for steel beams.
				 */
				bool bElementHasMesh = ContainsMesh(CurrentElement.DatasmithMeshElement.GetName()) || (CurrentElement.DatasmithPolymesh.Vertices.Count > 0 && CurrentElement.DatasmithPolymesh.Faces.Count > 0);

				if (CurrentElement.CurrentElement.GetType() == typeof(FamilyInstance) && !bElementHasMesh)
				{
					if (!CurrentElement.WorldTransform.IsIdentity)
					{
						CurrentElement.MeshPointsTransform = (CurrentElement.WorldTransform.Inverse * InstanceData.WorldTransform).Inverse;
					}
					else
					{
						CurrentElement.MeshPointsTransform = InstanceData.WorldTransform.Inverse;
					}

					CurrentElement.WorldTransform = InstanceData.WorldTransform;
					SetActorTransform(CurrentElement.WorldTransform, CurrentElement.ElementActor);
				}
			}

			// Collect the element Datasmith mesh into the mesh dictionary.
			CollectMesh(InstanceData.DatasmithPolymesh, InstanceData.DatasmithMeshElement, InDatasmithScene);

			// Add the instance mesh actor to the Datasmith actor hierarchy.
			CurrentElement.AddChildActor(InstanceData);
		}

		public void AddLocationActors(
			Transform InWorldTransform
		)
		{
			// Add a new Datasmith placeholder actor for this document site location.
			AddSiteLocation(CurrentDocument.SiteLocation);

			// Add new Datasmith placeholder actors for the project base point and survey points.
			// A project has one base point and at least one survey point. Linked documents also have their own points.
			AddPointLocations(InWorldTransform);
		}

		public void AddLightActor(
			Transform InWorldTransform,
			Asset InLightAsset
		)
		{
			ElementDataStack.Peek().AddLightActor(InWorldTransform, InLightAsset);
		}

		public void AddRPCActor(
			Transform InWorldTransform,
			Asset InRPCAsset,
			FDatasmithFacadeScene InDatasmithScene
		)
		{
			// Create a simple fallback material for the RPC mesh.
			string RPCCategoryName = ElementDataStack.Peek().GetCategoryName();
			bool isRPCPlant = !string.IsNullOrEmpty(RPCCategoryName) && RPCCategoryName == Category.GetCategory(CurrentDocument, BuiltInCategory.OST_Planting)?.Name;
			string RPCMaterialName = isRPCPlant ? "RPC_Plant" : "RPC_Material";
			string RPCHashedMaterialName = FDatasmithFacadeElement.GetStringHash(RPCMaterialName);

			if (!MaterialDataMap.ContainsKey(RPCHashedMaterialName))
			{
				// Color reference: https://www.color-hex.com/color-palette/70002
				Color RPCColor = isRPCPlant ? /* green */ new Color(88, 126, 96) : /* gray */ new Color(128, 128, 128);

				// Keep track of a new RPC master material.
				MaterialDataMap[RPCHashedMaterialName] = new FMaterialData(RPCHashedMaterialName, RPCMaterialName, RPCColor);
				NewMaterialsMap[RPCHashedMaterialName] = MaterialDataMap[RPCHashedMaterialName];
			}

			FMaterialData RPCMaterialData = MaterialDataMap[RPCHashedMaterialName];

			if (ElementDataStack.Peek().AddRPCActor(InWorldTransform, InRPCAsset, RPCMaterialData, out FDatasmithFacadeMesh RPCMesh, out FDatasmithFacadeMeshElement RPCMeshElement))
			{
				// Collect the RPC mesh into the Datasmith mesh dictionary.
				CollectMesh(RPCMesh, RPCMeshElement, InDatasmithScene);
			}
		}

		public bool SetMaterial(
			MaterialNode InMaterialNode,
			IList<string> InExtraTexturePaths
		)
		{
			Material CurrentMaterial = GetElement(InMaterialNode.MaterialId) as Material;

			CurrentMaterialName = FMaterialData.GetMaterialName(InMaterialNode, CurrentMaterial);

			if (!MaterialDataMap.ContainsKey(CurrentMaterialName))
			{
				// Keep track of a new Datasmith master material.
				MaterialDataMap[CurrentMaterialName] = new FMaterialData(InMaterialNode, CurrentMaterial, InExtraTexturePaths);
				NewMaterialsMap[CurrentMaterialName] = MaterialDataMap[CurrentMaterialName];

				// A new Datasmith master material was created.
				return true;
			}

			// No new Datasmith master material created.
			return false;
		}

		public bool IgnoreElementGeometry()
		{
			bool bIgnore = ElementDataStack.Peek().IgnoreElementGeometry();

			if (!bIgnore)
			{
				// Check for instanced meshes.
				// For mesh to be reused, it must not be cutoff by a section box.
				FBaseElementData CurrentInstance = ElementDataStack.Peek().PeekInstance();

				if (CurrentInstance != null && CurrentInstance.bAllowMeshInstancing && CurrentInstance.DatasmithMeshElement != null)
				{
					bIgnore = MeshMap.ContainsKey(CurrentInstance.DatasmithMeshElement.GetName());
				}
			}

			return bIgnore;
		}

		public FDatasmithPolymesh GetCurrentPolymesh()
		{
			return ElementDataStack.Peek().GetCurrentPolymesh();
		}

		public FDatasmithFacadeMeshElement GetCurrentMeshElement()
		{
			return ElementDataStack.Peek().GetCurrentMeshElement();
		}

		public Transform GetCurrentMeshPointsTransform()
		{
			return ElementDataStack.Peek().MeshPointsTransform;
		}

		public int GetCurrentMaterialIndex()
		{
			FElementData ElemData = ElementDataStack.Peek();
			FBaseElementData InstanceData = ElemData.PeekInstance();
			FBaseElementData CurrentElement = InstanceData != null ? InstanceData : ElemData;

			if (!CurrentElement.MeshMaterialsMap.ContainsKey(CurrentMaterialName))
			{
				int NewMaterialIndex = CurrentElement.MeshMaterialsMap.Count;
				CurrentElement.MeshMaterialsMap[CurrentMaterialName] = NewMaterialIndex;
				CurrentElement.DatasmithMeshElement.SetMaterial(CurrentMaterialName, NewMaterialIndex);
			}

			return CurrentElement.MeshMaterialsMap[CurrentMaterialName];
		}

		public FBaseElementData GetCurrentActor()
		{
			return ElementDataStack.Peek().GetCurrentActor();
		}

		public Element GetCurrentElement()
		{
			return ElementDataStack.Count > 0 ? ElementDataStack.Peek().CurrentElement : null;
		}

		private FBaseElementData OptimizeElementRecursive(FBaseElementData InElementData, FDatasmithFacadeScene InDatasmithScene, FSuperComponentOptimizer SuperComponentOptimizer)
		{
			List<FBaseElementData> RemoveChildren = new List<FBaseElementData>();
			List<FBaseElementData> AddChildren = new List<FBaseElementData>();

			for (int ChildIndex = 0; ChildIndex < InElementData.ChildElements.Count; ChildIndex++)
			{
				FBaseElementData ChildElement = InElementData.ChildElements[ChildIndex];

				// Optimize the Datasmith child actor.
				FBaseElementData ResultElement = OptimizeElementRecursive(ChildElement, InDatasmithScene, SuperComponentOptimizer);

				if (ChildElement != ResultElement)
				{
					RemoveChildren.Add(ChildElement);

					if (ResultElement != null)
					{
						AddChildren.Add(ResultElement);

						SuperComponentOptimizer.UpdateCache(ResultElement, ChildElement);
					}
				}
			}

			foreach (FBaseElementData Child in RemoveChildren)
			{
				Child.Parent = null;
				InElementData.ChildElements.Remove(Child);
				InElementData.ElementActor.RemoveChild(Child.ElementActor);
			}
			foreach (FBaseElementData Child in AddChildren)
			{
				Child.Parent = InElementData;
				InElementData.ChildElements.Add(Child);
				InElementData.ElementActor.AddChild(Child.ElementActor);
			}

			if (InElementData.bOptimizeHierarchy)
			{
				int ChildrenCount = InElementData.ElementActor.GetChildrenCount();

				if (ChildrenCount == 0)
				{
					// This Datasmith actor can be removed by optimization.
					return null;
				}

				if (ChildrenCount == 1)
				{
					Debug.Assert(InElementData.ChildElements.Count == 1);

					// This intermediate Datasmith actor can be removed while keeping its single child actor.
					FBaseElementData SingleChild = InElementData.ChildElements[0];

					// Make sure the single child actor will not become a dangling component in the actor hierarchy.
					if (!InElementData.ElementActor.IsComponent() && SingleChild.ElementActor.IsComponent())
					{
						SingleChild.ElementActor.SetIsComponent(false);
					}

					return SingleChild;
				}
			}

			return InElementData;
		}

		public void OptimizeActorHierarchy(FDatasmithFacadeScene InDatasmithScene)
		{
			FSuperComponentOptimizer SuperComponentOptimizer = new FSuperComponentOptimizer();

			foreach (var ElementEntry in ActorMap)
			{
				FBaseElementData ElementData = ElementEntry.Value;
				FBaseElementData ResultElementData = OptimizeElementRecursive(ElementData, InDatasmithScene, SuperComponentOptimizer);

				if (ResultElementData != ElementData)
				{
					if (ResultElementData == null)
					{
						InDatasmithScene.RemoveActor(ElementData.ElementActor, FDatasmithFacadeScene.EActorRemovalRule.RemoveChildren);
					}
					else
					{
						InDatasmithScene.RemoveActor(ElementData.ElementActor, FDatasmithFacadeScene.EActorRemovalRule.KeepChildrenAndKeepRelativeTransform);

						if (ElementData.ChildElements.Count == 1)
						{
							SuperComponentOptimizer.UpdateCache(ElementData.ChildElements[0], ElementData);
						}
					}
				}
			}

			SuperComponentOptimizer.Optimize();
		}

		public void WrapupLink(
			FDatasmithFacadeScene InDatasmithScene,
			FBaseElementData InLinkActor,
			HashSet<string> UniqueTextureNameSet
		)
		{
			// Add the collected meshes from the Datasmith mesh dictionary to the Datasmith scene.
			AddCollectedMeshes(InDatasmithScene);

			// Factor in the Datasmith actor hierarchy the Revit document host hierarchy.
			AddHostHierarchy();

			// Factor in the Datasmith actor hierarchy the Revit document level hierarchy.
			AddLevelHierarchy();

			if (ActorMap.Count > 0)
			{
				// Prevent the Datasmith link actor from being removed by optimization.
				InLinkActor.bOptimizeHierarchy = false;

				// Add the collected actors from the Datasmith actor dictionary as children of the Datasmith link actor.
				foreach (var Actor in ActorMap.Values)
				{
					InLinkActor.ChildElements.Add(Actor);
					Actor.Parent = InLinkActor;
				}
			}

			// Add the collected master materials from the material data dictionary to the Datasmith scene.
			AddCollectedMaterials(InDatasmithScene, UniqueTextureNameSet);
		}

		public void WrapupScene(
			FDatasmithFacadeScene InDatasmithScene,
			HashSet<string> UniqueTextureNameSet
		)
		{
			// Add the collected meshes from the Datasmith mesh dictionary to the Datasmith scene.
			AddCollectedMeshes(InDatasmithScene);

			// Factor in the Datasmith actor hierarchy the Revit document host hierarchy.
			AddHostHierarchy();

			// Factor in the Datasmith actor hierarchy the Revit document level hierarchy.
			AddLevelHierarchy();

			List<ElementId> OptimizedAwayElements = new List<ElementId>();

			foreach (var CollectedActor in ActorMap)
			{
				if (CollectedActor.Value.Optimize())
				{
					OptimizedAwayElements.Add(CollectedActor.Key);
				}
			}

			foreach (ElementId Element in  OptimizedAwayElements)
			{
				ActorMap.Remove(Element);
			}

			// Add the collected actors from the Datasmith actor dictionary to the Datasmith scene.
			foreach (var ActorEntry in ActorMap)
			{
				Element CollectedElement = CurrentDocument.GetElement(ActorEntry.Key);

				if (DirectLink != null && CollectedElement.GetType() == typeof(RevitLinkInstance))
				{
					Document LinkedDoc = (CollectedElement as RevitLinkInstance).GetLinkDocument();
					if (LinkedDoc != null)
					{
						DirectLink.OnBeginLinkedDocument(CollectedElement);
						foreach (FBaseElementData CurrentChild in ActorEntry.Value.ChildElements)
						{
							CurrentChild.AddToScene(InDatasmithScene, ActorEntry.Value, false);
						}
						DirectLink.OnEndLinkedDocument();
						ActorEntry.Value.AddToScene(InDatasmithScene, null, true);
					}
					else
					{
						ActorEntry.Value.AddToScene(InDatasmithScene, null, false);
					}
				}
				else
				{
					ActorEntry.Value.AddToScene(InDatasmithScene, null, false);
				}
			}

			// Add the collected master materials from the material data dictionary to the Datasmith scene.
			AddCollectedMaterials(InDatasmithScene, UniqueTextureNameSet);
		}

		public void LogElement(
			FDatasmithFacadeLog InDebugLog,
			string InLinePrefix,
			int InLineIndentation
		)
		{
			ElementDataStack.Peek().Log(InDebugLog, InLinePrefix, InLineIndentation);
		}

		public void LogMaterial(
			MaterialNode InMaterialNode,
			FDatasmithFacadeLog InDebugLog,
			string InLinePrefix
		)
		{
			if (MaterialDataMap.ContainsKey(CurrentMaterialName))
			{
				MaterialDataMap[CurrentMaterialName].Log(InMaterialNode, InDebugLog, InLinePrefix);
			}
		}

		private void AddSiteLocation(
			SiteLocation InSiteLocation
		)
		{
			if (InSiteLocation == null || !InSiteLocation.IsValidObject)
			{
				return;
			}

			FDatasmithFacadeActor SiteLocationActor = null;
			FBaseElementData ElementData = null;

			DirectLink?.MarkForExport(InSiteLocation);

			if (DirectLink?.IsElementCached(InSiteLocation) ?? false)
			{
				if (!DirectLink.IsElementModified(InSiteLocation))
				{
					return;
				}

				ElementData = DirectLink.GetCachedElement(InSiteLocation);
				SiteLocationActor = ElementData.ElementActor;
				SiteLocationActor.ResetTags();
			}
			else
			{
				// Create a new Datasmith placeholder actor for the site location.
				// Hash the Datasmith placeholder actor name to shorten it.
				string NameHash = FDatasmithFacadeElement.GetStringHash("SiteLocation");
				SiteLocationActor = new FDatasmithFacadeActor(NameHash);
				SiteLocationActor.SetLabel("Site Location");
			}

			// Set the Datasmith placeholder actor layer to the site location category name.
			SiteLocationActor.SetLayer(InSiteLocation.Category.Name);

			// Add the Revit element ID and Unique ID tags to the Datasmith placeholder actor.
			SiteLocationActor.AddTag($"Revit.Element.Id.{InSiteLocation.Id.IntegerValue}");
			SiteLocationActor.AddTag($"Revit.Element.UniqueId.{InSiteLocation.UniqueId}");

			// Add a Revit element site location tag to the Datasmith placeholder actor.
			SiteLocationActor.AddTag("Revit.Element.SiteLocation");

			FDatasmithFacadeMetaData SiteLocationMetaData = new FDatasmithFacadeMetaData(SiteLocationActor.GetName() + "_DATA");
			SiteLocationMetaData.SetLabel(SiteLocationActor.GetLabel());
			SiteLocationMetaData.SetAssociatedElement(SiteLocationActor);

			// Add site location metadata to the Datasmith placeholder actor.
			const double RadiansToDegrees = 180.0 / Math.PI;
			SiteLocationMetaData.AddPropertyFloat("SiteLocation*Latitude", (float)(InSiteLocation.Latitude * RadiansToDegrees));
			SiteLocationMetaData.AddPropertyFloat("SiteLocation*Longitude", (float)(InSiteLocation.Longitude * RadiansToDegrees));
			SiteLocationMetaData.AddPropertyFloat("SiteLocation*Elevation", (float)InSiteLocation.Elevation);
			SiteLocationMetaData.AddPropertyFloat("SiteLocation*TimeZone", (float)InSiteLocation.TimeZone);
			SiteLocationMetaData.AddPropertyString("SiteLocation*Place", InSiteLocation.PlaceName);

			// Collect the site location placeholder actor into the Datasmith actor dictionary.
			if (ElementData == null)
			{
				ElementData = new FBaseElementData(SiteLocationActor, null, this);
				// Prevent the Datasmith placeholder actor from being removed by optimization.
				ElementData.bOptimizeHierarchy = false; 
			}
			else
			{
				ElementData.ElementMetaData = SiteLocationMetaData;
			}
			
			ActorMap[InSiteLocation.Id] = ElementData;

			DirectLink?.CacheElement(CurrentDocument, InSiteLocation, ElementData);
		}

		private void AddPointLocations(
			Transform InWorldTransform
		)
		{
			FilteredElementCollector Collector = new FilteredElementCollector(CurrentDocument);
			ICollection<Element> PointLocations = Collector.OfClass(typeof(BasePoint)).ToElements();

			foreach (Element PointLocation in PointLocations)
			{
				BasePoint BasePointLocation = PointLocation as BasePoint;

				if (BasePointLocation != null)
				{
					// Since BasePoint.Location is not a location point we cannot get a position from it; so we use a bounding box approach.
					// Note that, as of Revit 2020, BasePoint has 2 new properties: Position for base point and SharedPosition for survey point.
					BoundingBoxXYZ BasePointBoundingBox = BasePointLocation.get_BoundingBox(CurrentDocument.ActiveView);
					if (BasePointBoundingBox == null)
					{
						continue;
					}

					string ActorName = BasePointLocation.IsShared ? "SurveyPoint" : "BasePoint";
					string ActorLabel = BasePointLocation.IsShared ? "Survey Point" : "Base Point";

					FDatasmithFacadeActor BasePointActor = null;
					FBaseElementData BasePointElement = null;

					DirectLink?.MarkForExport(PointLocation);

					if (DirectLink?.IsElementCached(PointLocation) ?? false)
					{
						if (!DirectLink.IsElementModified(PointLocation))
						{
							continue;
						}

						BasePointElement = DirectLink.GetCachedElement(PointLocation);
						BasePointActor = BasePointElement.ElementActor;
						BasePointActor.ResetTags();
					}
					else
					{
						// Create a new Datasmith placeholder actor for the base point.
						// Hash the Datasmith placeholder actor name to shorten it.
						string HashedActorName = FDatasmithFacadeElement.GetStringHash(ActorName);
						BasePointActor = new FDatasmithFacadeActor(HashedActorName);
						BasePointActor.SetLabel(ActorLabel);
					}

					// Set the world transform of the Datasmith placeholder actor.
					XYZ BasePointPosition = BasePointBoundingBox.Min;

					Transform TranslationMatrix = Transform.CreateTranslation(BasePointPosition);
					FDocumentData.SetActorTransform(TranslationMatrix.Multiply(InWorldTransform), BasePointActor);

					// Set the Datasmith placeholder actor layer to the base point category name.
					BasePointActor.SetLayer(BasePointLocation.Category.Name);

					// Add the Revit element ID and Unique ID tags to the Datasmith placeholder actor.
					BasePointActor.AddTag($"Revit.Element.Id.{BasePointLocation.Id.IntegerValue}");
					BasePointActor.AddTag($"Revit.Element.UniqueId.{BasePointLocation.UniqueId}");

					// Add a Revit element base point tag to the Datasmith placeholder actor.
					BasePointActor.AddTag("Revit.Element." + ActorName);

					// Add base point metadata to the Datasmith actor.
					string MetadataPrefix = BasePointLocation.IsShared ? "SurveyPointLocation*" : "BasePointLocation*";

					FDatasmithFacadeMetaData BasePointMetaData = new FDatasmithFacadeMetaData(BasePointActor.GetName() + "_DATA");
					BasePointMetaData.SetLabel(BasePointActor.GetLabel());
					BasePointMetaData.SetAssociatedElement(BasePointActor);

					BasePointMetaData.AddPropertyVector(MetadataPrefix + "Location", $"{BasePointPosition.X} {BasePointPosition.Y} {BasePointPosition.Z}");
					FDocumentData.AddActorMetadata(BasePointLocation, MetadataPrefix, BasePointMetaData);

					if (BasePointElement == null)
					{
						// Collect the base point placeholder actor into the Datasmith actor dictionary.
						BasePointElement = new FBaseElementData(BasePointActor, BasePointMetaData, this);
						BasePointElement.bOptimizeHierarchy = false;
					}
					else
					{
						BasePointElement.ElementMetaData = BasePointMetaData;
					}

					ActorMap[BasePointLocation.Id] = BasePointElement;

					DirectLink?.CacheElement(CurrentDocument, PointLocation, BasePointElement);
				}
			}
		}

		private void CollectMesh(
			FDatasmithFacadeMesh InMesh,
			FDatasmithFacadeMeshElement InMeshElement,
			FDatasmithFacadeScene InDatasmithScene
		)
		{
			if (InDatasmithScene != null && InMesh.GetVerticesCount() > 0 && InMesh.GetFacesCount() > 0)
			{
				string MeshName = InMesh.GetName();

				if (!MeshMap.ContainsKey(MeshName))
				{
					// Export the DatasmithMesh in a task while we parse the rest of the document.
					// The task result indicates if the export was successful and if the associated FDatasmithFacadeMeshElement can be added to the scene.
					MeshMap[MeshName] = new Tuple<FDatasmithFacadeMeshElement, Task<bool>>(InMeshElement, Task.Run<bool>(() => InDatasmithScene.ExportDatasmithMesh(InMeshElement, InMesh)));
				}
			}
		}

		private void CollectMesh(
			FDatasmithPolymesh InPolymesh,
			FDatasmithFacadeMeshElement InMeshElement,
			FDatasmithFacadeScene InDatasmithScene
		)
		{
			if (InDatasmithScene != null && InPolymesh.Vertices.Count > 0 && InPolymesh.Faces.Count > 0)
			{
				string MeshName = InMeshElement.GetName();

				if (!MeshMap.ContainsKey(MeshName))
				{
					// Export the DatasmithMesh in a task while we parse the rest of the document.
					// The task result indicates if the export was successful and if the associated FDatasmithFacadeMeshElement can be added to the scene.
					MeshMap[MeshName] = new Tuple<FDatasmithFacadeMeshElement, Task<bool>>(InMeshElement, Task.Run<bool>(
						() => 
						{
							using (FDatasmithFacadeMesh DatasmithMesh = ParsePolymesh(InPolymesh, MeshName))
							{
								return InDatasmithScene.ExportDatasmithMesh(InMeshElement, DatasmithMesh);
							}
						}
					));
				}
			}
		}

		private FDatasmithFacadeMesh ParsePolymesh(FDatasmithPolymesh InPolymesh, string MeshName)
		{
			FDatasmithFacadeMesh DatasmithMesh = new FDatasmithFacadeMesh();
			DatasmithMesh.SetName(MeshName);
			DatasmithMesh.SetVerticesCount(InPolymesh.Vertices.Count);
			DatasmithMesh.SetFacesCount(InPolymesh.Faces.Count);
			DatasmithMesh.SetUVChannelsCount(1);
			DatasmithMesh.SetUVCount(0, InPolymesh.UVs.Count);
			const int UVChannelIndex = 0;

			// Add the vertex points (in right-handed Z-up coordinates) to the Datasmith mesh.
			for (int VertexIndex = 0; VertexIndex < InPolymesh.Vertices.Count; ++VertexIndex)
			{
				XYZ Point = InPolymesh.Vertices[VertexIndex];
				DatasmithMesh.SetVertex(VertexIndex, (float)Point.X, (float)Point.Y, (float)Point.Z);
			}

			// Add the vertex UV texture coordinates to the Datasmith mesh.
			for (int UVIndex = 0; UVIndex < InPolymesh.UVs.Count; ++UVIndex)
			{
				UV CurrentUV = InPolymesh.UVs[UVIndex];
				DatasmithMesh.SetUV(UVChannelIndex, UVIndex, (float)CurrentUV.U, (float)CurrentUV.V);
			}

			// Add the triangle vertex indexes to the Datasmith mesh.
			for (int FacetIndex = 0; FacetIndex < InPolymesh.Faces.Count; ++FacetIndex)
			{
				FDocumentData.FPolymeshFace Face = InPolymesh.Faces[FacetIndex];
				DatasmithMesh.SetFace(FacetIndex, Face.V1, Face.V2, Face.V3, Face.MaterialIndex);
				DatasmithMesh.SetFaceUV(FacetIndex, UVChannelIndex, Face.V1, Face.V2, Face.V3);
			}

			for (int NormalIndex = 0; NormalIndex < InPolymesh.Normals.Count; ++NormalIndex)
			{
				XYZ Normal = InPolymesh.Normals[NormalIndex];
				DatasmithMesh.SetNormal(NormalIndex, (float)Normal.X, (float)Normal.Y, (float)Normal.Z);
			}
		
			return DatasmithMesh;
		}

		private void AddCollectedMeshes(
			FDatasmithFacadeScene InDatasmithScene
		)
		{
			// Add the collected meshes from the Datasmith mesh dictionary to the Datasmith scene.
			foreach (var MeshElementExportResultTuple in MeshMap.Values)
			{
				// Wait for the export to complete and add the Mesh element on success.
				if (MeshElementExportResultTuple.Item2.Result)
				{
					InDatasmithScene.AddMesh(MeshElementExportResultTuple.Item1);
				}
			}
		}

		private void AddHostHierarchy()
		{
			AddParentElementHierarchy(GetHostElement);
		}

		private void AddLevelHierarchy()
		{
			AddParentElementHierarchy(GetLevelElement);
		}

		private void AddCollectedMaterials(
			FDatasmithFacadeScene InDatasmithScene,
			HashSet<string> UniqueTextureNameSet
		)
		{
			UniqueTextureNameSet = (DirectLink != null) ? DirectLink.UniqueTextureNameSet : UniqueTextureNameSet;

			// Add the collected master materials from the material data dictionary to the Datasmith scene.
			foreach (FMaterialData CollectedMaterialData in NewMaterialsMap.Values)
			{
				InDatasmithScene.AddMaterial(CollectedMaterialData.MasterMaterial);

				foreach(FDatasmithFacadeTexture CurrentTexture in CollectedMaterialData.CollectedTextures)
				{
					string TextureName = CurrentTexture.GetName();
					if (!UniqueTextureNameSet.Contains(TextureName))
					{
						UniqueTextureNameSet.Add(TextureName);
						InDatasmithScene.AddTexture(CurrentTexture);
					}
				}

				if (CollectedMaterialData.MessageList.Count > 0)
				{
					MessageList.AddRange(CollectedMaterialData.MessageList);
				}
			}
		}

		private Element GetHostElement(
			ElementId InElementId
		)
		{
			Element SourceElement = CurrentDocument.GetElement(InElementId);
			Element HostElement = null;

			if (SourceElement as FamilyInstance != null)
			{
				HostElement = (SourceElement as FamilyInstance).Host;
			}
			else if (SourceElement as Wall != null)
			{
				HostElement = CurrentDocument.GetElement((SourceElement as Wall).StackedWallOwnerId);
			}
			else if (SourceElement as ContinuousRail != null)
			{
				HostElement = CurrentDocument.GetElement((SourceElement as ContinuousRail).HostRailingId);
			}
			else if (SourceElement.GetType().IsSubclassOf(typeof(InsulationLiningBase)))
			{
				HostElement = CurrentDocument.GetElement((SourceElement as InsulationLiningBase).HostElementId);
			}

			// DirectLink: if host is hidden, go up the hierarchy (NOTE this does not apply for linked documents)
			if (DirectLink != null && 
				HostElement != null && 
				CurrentDocument.ActiveView != null && 
				HostElement.IsHidden(CurrentDocument.ActiveView))
			{
				return GetHostElement(HostElement.Id);
			}

			return HostElement;
		}

		private Element GetLevelElement(
			ElementId InElementId
		)
		{
			Element SourceElement = CurrentDocument.GetElement(InElementId);

			return (SourceElement == null) ? null : CurrentDocument.GetElement(SourceElement.LevelId);
		}

		private void AddParentElementHierarchy(
			Func<ElementId, Element> InGetParentElement
		)
		{
			Queue<ElementId> ElementIdQueue = new Queue<ElementId>(ActorMap.Keys);

			// Make sure the Datasmith actor dictionary contains actors for all the Revit parent elements.
			while (ElementIdQueue.Count > 0)
			{
				Element ParentElement = InGetParentElement(ElementIdQueue.Dequeue());

				if (ParentElement == null)
				{
					continue;
				}

				ElementId ParentElementId = ParentElement.Id;

				if (ActorMap.ContainsKey(ParentElementId))
				{
					continue;
				}

				if (DirectLink?.IsElementCached(ParentElement) ?? false)
				{
					// Move parent actor out of cache.
					DirectLink.MarkForExport(ParentElement);
					ActorMap[ParentElementId] =  DirectLink.GetCachedElement(ParentElement);
				}
				else
				{
					const FDatasmithFacadeScene NullScene = null;
					PushElement(ParentElement, Transform.Identity);
					PopElement(NullScene);
				}

				ElementIdQueue.Enqueue(ParentElementId);
			}

			// Add the parented actors as children of the parent Datasmith actors.
			foreach (ElementId ElemId in new List<ElementId>(ActorMap.Keys))
			{
				Element ParentElement = InGetParentElement(ElemId);

				if (ParentElement == null)
				{
					continue;
				}

				Element SourceElement = CurrentDocument.GetElement(ElemId);

				if ((SourceElement as FamilyInstance != null && ParentElement as Truss != null) ||
					(SourceElement as Mullion != null) ||
					(SourceElement as Panel != null) ||
					(SourceElement as ContinuousRail != null))
				{
					// The Datasmith actor is a component in the hierarchy.
					ActorMap[ElemId].ElementActor.SetIsComponent(true);
				}

				ElementId ParentElementId = ParentElement.Id;

				// Add the parented actor as child of the parent Datasmith actor.

				FBaseElementData ElementData = ActorMap[ElemId];
				FBaseElementData ParentElementData = ActorMap[ParentElementId];
				
				if (!ParentElementData.ChildElements.Contains(ElementData))
				{
					ParentElementData.ChildElements.Add(ElementData);
					ElementData.Parent = ParentElementData;
				}

				// Prevent the parent Datasmith actor from being removed by optimization.
				ParentElementData.bOptimizeHierarchy = false;
			}

			// Remove the parented child actors from the Datasmith actor dictionary.
			foreach (ElementId ElemId in new List<ElementId>(ActorMap.Keys))
			{
				Element ParentElement = InGetParentElement(ElemId);

				if (ParentElement == null)
				{
					continue;
				}

				// Remove the parented child actor from the Datasmith actor dictionary.
				ActorMap.Remove(ElemId);
			}
		}

		private static void SetActorTransform(
			Transform InWorldTransform,
			FDatasmithFacadeActor IOActor
		)
		{
			XYZ transformBasisX = InWorldTransform.BasisX;
			XYZ transformBasisY = InWorldTransform.BasisY;
			XYZ transformBasisZ = InWorldTransform.BasisZ;
			XYZ transformOrigin = InWorldTransform.Origin;

			float[] worldMatrix = new float[16];

			worldMatrix[0] = (float)transformBasisX.X;
			worldMatrix[1] = (float)transformBasisX.Y;
			worldMatrix[2] = (float)transformBasisX.Z;
			worldMatrix[3] = 0.0F;
			worldMatrix[4] = (float)transformBasisY.X;
			worldMatrix[5] = (float)transformBasisY.Y;
			worldMatrix[6] = (float)transformBasisY.Z;
			worldMatrix[7] = 0.0F;
			worldMatrix[8] = (float)transformBasisZ.X;
			worldMatrix[9] = (float)transformBasisZ.Y;
			worldMatrix[10] = (float)transformBasisZ.Z;
			worldMatrix[11] = 0.0F;
			worldMatrix[12] = (float)transformOrigin.X;
			worldMatrix[13] = (float)transformOrigin.Y;
			worldMatrix[14] = (float)transformOrigin.Z;
			worldMatrix[15] = 1.0F;

			// Set the world transform of the Datasmith actor.
			IOActor.SetWorldTransform(worldMatrix);
		}

		public static ElementType GetElementType(Element InElement)
		{
			return InElement.Document.GetElement(InElement.GetTypeId()) as ElementType;
		}

		public static string GetCategoryName(Element InElement)
		{
			ElementType Type = GetElementType(InElement);
			return Type?.Category?.Name ?? InElement.Category?.Name;
		}

		public static void AddActorMetadata(
			Element InElement,
			FDatasmithFacadeMetaData ActorMetadata
		)
		{
			// Add the Revit element category name metadata to the Datasmith actor.
			string CategoryName = GetCategoryName(InElement);
			if (!string.IsNullOrEmpty(CategoryName))
			{
				ActorMetadata.AddPropertyString("Element*Category", CategoryName);
			}

			// Add the Revit element family name metadata to the Datasmith actor.
			ElementType ElemType = GetElementType(InElement);
			string FamilyName = ElemType?.FamilyName;
			if (!string.IsNullOrEmpty(FamilyName))
			{
				ActorMetadata.AddPropertyString("Element*Family", FamilyName);
			}

			// Add the Revit element type name metadata to the Datasmith actor.
			string TypeName = ElemType?.Name;
			if (!string.IsNullOrEmpty(TypeName))
			{
				ActorMetadata.AddPropertyString("Element*Type", TypeName);
			}

			// Add Revit element metadata to the Datasmith actor.
			FDocumentData.AddActorMetadata(InElement, "Element*", ActorMetadata);

			if (ElemType != null)
			{
				// Add Revit element type metadata to the Datasmith actor.
				FDocumentData.AddActorMetadata(ElemType, "Type*", ActorMetadata);
			}
		}

		private static void AddActorMetadata(
			Element InSourceElement,
			string InMetadataPrefix,
			FDatasmithFacadeMetaData ElementMetaData
		)
		{
			IList<Parameter> Parameters = InSourceElement.GetOrderedParameters();

			if (Parameters != null)
			{
				foreach (Parameter Parameter in Parameters)
				{
					if (Parameter.HasValue)
					{
						string ParameterValue = Parameter.AsValueString();

						if (string.IsNullOrEmpty(ParameterValue))
						{
							switch (Parameter.StorageType)
							{
								case StorageType.Integer:
									ParameterValue = Parameter.AsInteger().ToString();
									break;
								case StorageType.Double:
									ParameterValue = Parameter.AsDouble().ToString();
									break;
								case StorageType.String:
									ParameterValue = Parameter.AsString();
									break;
								case StorageType.ElementId:
									ParameterValue = Parameter.AsElementId().ToString();
									break;
							}
						}

						if (!string.IsNullOrEmpty(ParameterValue))
						{
							string MetadataKey = InMetadataPrefix + Parameter.Definition.Name;
							ElementMetaData.AddPropertyString(MetadataKey, ParameterValue);
						}
					}
				}
			}
		}
	}
}
