// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography.X509Certificates;
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
		public class FBaseElementData
		{
			public ElementType				BaseElementType;
			public FDatasmithFacadeMesh		ElementMesh = null;
			public FDatasmithFacadeActor	ElementActor = null;
			public FDatasmithFacadeMetaData	ElementMetaData = null;
			public FDocumentData			DocumentData = null;
			public bool						bOptimizeHierarchy = true;
			public bool						bIsModified = true;

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
					ElementActor.SetLabel(MeshActor.GetLabel());

					float X, Y, Z, W;
					MeshActor.GetTranslation(out X, out Y, out Z);
					ElementActor.SetTranslation(X, Y, Z);
					MeshActor.GetScale(out X, out Y, out Z);
					ElementActor.SetScale(X, Y, Z);
					MeshActor.GetRotation(out X, out Y, out Z, out W);
					ElementActor.SetRotation(X, Y, Z, W);

					ElementActor.SetLayer(MeshActor.GetLayer());

					for (int TagIndex = 0; TagIndex < MeshActor.GetTagsCount(); ++TagIndex)
					{
						ElementActor.AddTag(MeshActor.GetTag(TagIndex));
					}

					ElementActor.SetIsComponent(MeshActor.IsComponent());
					ElementActor.SetAsSelector(MeshActor.IsASelector());
					ElementActor.SetSelectionIndex(MeshActor.GetSelectionIndex());
					ElementActor.SetVisibility(MeshActor.GetVisibility());

					for (int ChildIndex = 0; ChildIndex < MeshActor.GetChildrenCount(); ++ChildIndex)
					{
						ElementActor.AddChild(MeshActor.GetChild(ChildIndex));
					}

					ElementMetaData?.SetAssociatedElement(ElementActor);

					return true;
				}

				return !(ElementActor is FDatasmithFacadeActorMesh || ElementActor is FDatasmithFacadeActorLight || ElementActor is FDatasmithFacadeActorCamera);
			}

			public void AddToScene(FDatasmithFacadeScene InScene, FBaseElementData InParent, bool bInSkipChildren, bool bInForceAdd = false)
			{
				Element ThisElement = (this as FElementData)?.CurrentElement;

				if (!bInSkipChildren)
				{
					foreach (FBaseElementData CurrentChild in ChildElements)
					{
						CurrentChild.AddToScene(InScene, this, false, (ThisElement == null) && bIsModified);
					}
				}

				bool bIsCached = 
					ThisElement != null && 
					ThisElement.IsValidObject && 
					(DocumentData.DirectLink?.IsElementCached(ThisElement) ?? false);

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
				MeshActor.SetMesh(ElementMesh.GetName());
				bOptimizeHierarchy = false;
			}
		}

		private class FElementData : FBaseElementData
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
						|| CurrentElement.GetType() == typeof(StructuralConnectionHandler))
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

			public void PushInstance(
				ElementType InInstanceType,
				Transform InWorldTransform
			)
			{
				FBaseElementData InstanceData = new FBaseElementData(InInstanceType, DocumentData);

				InstanceDataStack.Push(InstanceData);

				InitializeElement(InWorldTransform, InstanceData);

				// The Datasmith instance actor is a component in the hierarchy.
				InstanceData.ElementActor.SetIsComponent(true);
			}

			public FBaseElementData PopInstance()
			{
				FBaseElementData Instance = InstanceDataStack.Pop();
				Instance.bIsModified = true;
				return Instance;
			}

			public void AddLightActor(
				Transform InWorldTransform,
				Asset InLightAsset
			)
			{
				// Create a new Datasmith light actor.
				// Hash the Datasmith light actor name to shorten it.
				string HashedActorName = FDatasmithFacadeElement.GetStringHash("L:" + GetActorName());
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

			public FDatasmithFacadeMesh AddRPCActor(
				Transform InWorldTransform,
				Asset InRPCAsset,
				int InMaterialIndex
			)
			{
				// Create a new Datasmith RPC mesh.
				// Hash the Datasmith RPC mesh name to shorten it.
				string HashedName = FDatasmithFacadeElement.GetStringHash("RPCM:" + GetActorName());
				FDatasmithFacadeMesh RPCMesh = new FDatasmithFacadeMesh(HashedName);
				RPCMesh.SetLabel(GetActorLabel());

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

				GeometryElement RPCGeometryElement = CurrentElement.get_Geometry(new Options());

				foreach (GeometryObject RPCGeometryObject in RPCGeometryElement)
				{
					GeometryInstance RPCGeometryInstance = RPCGeometryObject as GeometryInstance;

					if (RPCGeometryInstance != null)
					{
						GeometryElement RPCInstanceGeometry = RPCGeometryInstance.GetInstanceGeometry();

						foreach (GeometryObject RPCInstanceGeometryObject in RPCInstanceGeometry)
						{
							Mesh RPCInstanceGeometryMesh = RPCInstanceGeometryObject as Mesh;

							if (RPCInstanceGeometryMesh == null || RPCInstanceGeometryMesh.NumTriangles < 1)
							{
								continue;
							}

							// RPC geometry does not have normals nor UVs available through the Revit Mesh interface.
							int InitialVertexCount = RPCMesh.GetVertexCount();
							int TriangleCount = RPCInstanceGeometryMesh.NumTriangles;

							// Add the RPC geometry vertices to the Datasmith RPC mesh.
							foreach (XYZ Vertex in RPCInstanceGeometryMesh.Vertices)
							{
								XYZ PositionedVertex = AffineTransform.OfPoint(Vertex);
								RPCMesh.AddVertex((float)PositionedVertex.X, (float)PositionedVertex.Y, (float)PositionedVertex.Z);
							}

							// Add the RPC geometry triangles to the Datasmith RPC mesh.
							for (int TriangleNo = 0; TriangleNo < TriangleCount; TriangleNo++)
							{
								MeshTriangle Triangle = RPCInstanceGeometryMesh.get_Triangle(TriangleNo);

								try
								{
									int Index0 = Convert.ToInt32(Triangle.get_Index(0));
									int Index1 = Convert.ToInt32(Triangle.get_Index(1));
									int Index2 = Convert.ToInt32(Triangle.get_Index(2));

									// Add triangles for both the front and back faces.
									RPCMesh.AddTriangle(InitialVertexCount + Index0, InitialVertexCount + Index1, InitialVertexCount + Index2, InMaterialIndex);
									RPCMesh.AddTriangle(InitialVertexCount + Index2, InitialVertexCount + Index1, InitialVertexCount + Index0, InMaterialIndex);
								}
								catch (OverflowException)
								{
									continue;
								}
							}
						}
					}
				}

				// Create a new Datasmith RPC mesh actor.
				// Hash the Datasmith RPC mesh actor name to shorten it.
				string HashedActorName = FDatasmithFacadeElement.GetStringHash("RPC:" + GetActorName());
				FDatasmithFacadeActor FacadeActor;
				if (RPCMesh.GetVertexCount() > 0 && RPCMesh.GetTriangleCount() > 0)
				{
					FDatasmithFacadeActorMesh RPCMeshActor = new FDatasmithFacadeActorMesh(HashedActorName);
					RPCMeshActor.SetMesh(RPCMesh.GetName());
					FacadeActor = RPCMeshActor;
				}
				else
				{
					//Create an empty actor instead of a static mesh actor with no mesh.
					FacadeActor = new FDatasmithFacadeActor(HashedActorName);
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

				return RPCMesh;
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
				// Create a new Datasmith mesh.
				// Hash the Datasmith mesh name to shorten it.
				string HashedMeshName = FDatasmithFacadeElement.GetStringHash("M:" + GetMeshName());
				InElement.ElementMesh = new FDatasmithFacadeMesh(HashedMeshName);
				InElement.ElementMesh.SetLabel(GetActorLabel());

				if(InElement.ElementActor == null)
				{
					// Create a new Datasmith mesh actor.
					// Hash the Datasmith mesh actor name to shorten it.
					string HashedActorName = FDatasmithFacadeElement.GetStringHash("A:" + GetActorName());
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

			public FDatasmithFacadeMesh GetCurrentMesh()
			{
				if (InstanceDataStack.Count == 0)
				{
					return ElementMesh;
				}
				else
				{
					return InstanceDataStack.Peek().ElementMesh;
				}
			}

			public FDatasmithFacadeMesh PeekInstancedMesh()
			{
				if (InstanceDataStack.Count == 0)
				{
					return null;
				}
				else
				{
					return InstanceDataStack.Peek().ElementMesh;
				}
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

			private string GetActorName()
			{
				string DocumentName = Path.GetFileNameWithoutExtension(CurrentElement.Document.PathName);

				if (InstanceDataStack.Count == 0)
				{
					return $"{DocumentName}:{CurrentElement.UniqueId}";
				}
				else
				{
					// GetActorName is being called when generating a name for instance. 
					// After the call, the intance is added as a child to its parent. 
					// Next time the method gets called for the next instance, ChildElements.Count will be different/incremented.
					FBaseElementData Instance = InstanceDataStack.Peek();
					FBaseElementData Parent = InstanceDataStack.Count > 1 ? InstanceDataStack.Skip(1).First() : this;
					return $"{DocumentName}:{CurrentElement.UniqueId}:{Instance.BaseElementType.UniqueId}:{Parent.ChildElements.Count}";
				}
			}

			private string GetMeshName()
			{
				string DocumentName = Path.GetFileNameWithoutExtension(CurrentElement.Document.PathName);

				if (InstanceDataStack.Count == 0)
				{
					return $"{DocumentName}:{CurrentElement.UniqueId}";
				}
				else
				{
					// Generate instanced mesh name
					FBaseElementData Instance = InstanceDataStack.Peek();
					return $"{DocumentName}:{Instance.BaseElementType.UniqueId}";
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

		public Dictionary<string, FDatasmithFacadeMesh>	MeshMap = new Dictionary<string, FDatasmithFacadeMesh>();
		public Dictionary<ElementId, FBaseElementData>	ActorMap = new Dictionary<ElementId, FDocumentData.FBaseElementData>();
		public Dictionary<string, FMaterialData>		MaterialDataMap = new Dictionary<string, FMaterialData>();

		private Stack<FElementData>						ElementDataStack = new Stack<FElementData>();
		private string									CurrentMaterialDataKey = null;
		private int										LatestMaterialIndex = 0;
		private List<string>							MessageList = null;

		public bool										bSkipMetadataExport { get; private set; } = false;
		public Document									CurrentDocument { get; private set; } = null;
		public FDirectLink								DirectLink { get; private set; } = null;

		public FDocumentData(
			Document InDocument,
			ref List<string> InMessageList,
			FDirectLink InDirectLink
		)
		{
			DirectLink = InDirectLink;
			CurrentDocument = InDocument;
			MessageList = InMessageList;
			// With DirectLink, we delay export of metadata for a faster initial export.
			bSkipMetadataExport = (DirectLink != null);
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

					if (DirectLink.IsElementModified(InElement))
					{
						FDatasmithFacadeActor ExistingActor = ElementData.ElementActor;

						// Remove children that are instances: they will be re-created;
						// The reason is that we cannot uniquely identify family instances (no id) and when element changes,
						// we need to export all of its child instances anew.
						if (ExistingActor != null && ElementData.ChildElements.Count > 0)
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

						ElementData.InitializePivotPlacement(ref InWorldTransform);
						ElementData.InitializeElement(InWorldTransform, ElementData);
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

		public void PopElement()
		{
			FElementData ElementData = ElementDataStack.Pop();

			FDatasmithFacadeMesh ElementMesh = ElementData.ElementMesh;

			if(ElementMesh.GetVertexCount() > 0 && ElementMesh.GetTriangleCount() > 0)
			{
				ElementData.UpdateMeshName();
			}

			CollectMesh(ElementMesh);

			DirectLink?.ClearModified(ElementData.CurrentElement);

			ElementData.bIsModified = true;

			if (ElementDataStack.Count == 0)
			{
				ElementId ElemId = ElementData.CurrentElement.Id;

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
				// Add the element mesh actor to the Datasmith actor hierarchy.
				ElementDataStack.Peek().AddChildActor(ElementData);
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
			CloneActor.SetAsSelector(SourceActor.IsASelector());
			CloneActor.SetSelectionIndex(SourceActor.GetSelectionIndex());
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
			ElementDataStack.Peek().PushInstance(InInstanceType, InWorldTransform);
		}

		public void PopInstance()
		{
			FElementData CurrentElement = ElementDataStack.Peek();
			FBaseElementData InstanceData = CurrentElement.PopInstance();
			FDatasmithFacadeMesh ElementMesh = InstanceData.ElementMesh;

			if (ContainsMesh(ElementMesh.GetName()) || (ElementMesh.GetVertexCount() > 0 && ElementMesh.GetTriangleCount() > 0))
			{
				InstanceData.UpdateMeshName();
			}

			// Collect the element Datasmith mesh into the mesh dictionary.
			CollectMesh(ElementMesh);

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
			Asset InRPCAsset
		)
		{
			// Create a simple fallback material for the RPC mesh.
			string RPCCategoryName = ElementDataStack.Peek().GetCategoryName();
			bool isRPCPlant = !string.IsNullOrEmpty(RPCCategoryName) && RPCCategoryName == Category.GetCategory(CurrentDocument, BuiltInCategory.OST_Planting)?.Name;
			string RPCMaterialName = isRPCPlant ? "RPC_Plant" : "RPC_Material";

			if (!MaterialDataMap.ContainsKey(RPCMaterialName))
			{
				// Color reference: https://www.color-hex.com/color-palette/70002
				Color RPCColor = isRPCPlant ? /* green */ new Color(88, 126, 96) : /* gray */ new Color(128, 128, 128);

				// Keep track of a new RPC master material.
				MaterialDataMap[RPCMaterialName] = new FMaterialData(RPCMaterialName, RPCColor, ++LatestMaterialIndex);
			}

			FMaterialData RPCMaterialData = MaterialDataMap[RPCMaterialName];

			FDatasmithFacadeMesh RPCMesh = ElementDataStack.Peek().AddRPCActor(InWorldTransform, InRPCAsset, RPCMaterialData.MaterialIndex);

			// Add the RPC master material name to the dictionary of material names utilized by the RPC mesh.
			RPCMesh.AddMaterial(RPCMaterialData.MaterialIndex, RPCMaterialData.MasterMaterial.GetName());

			// Collect the RPC mesh into the Datasmith mesh dictionary.
			CollectMesh(RPCMesh);
		}

		public bool SetMaterial(
			MaterialNode InMaterialNode,
			IList<string> InExtraTexturePaths
		)
		{
			Material CurrentMaterial = GetElement(InMaterialNode.MaterialId) as Material;

			CurrentMaterialDataKey = FMaterialData.GetMaterialName(InMaterialNode, CurrentMaterial);

			if (!MaterialDataMap.ContainsKey(CurrentMaterialDataKey))
			{
				// Keep track of a new Datasmith master material.
				MaterialDataMap[CurrentMaterialDataKey] = new FMaterialData(InMaterialNode, CurrentMaterial, ++LatestMaterialIndex, InExtraTexturePaths);

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
				FDatasmithFacadeMesh Mesh = ElementDataStack.Peek().PeekInstancedMesh();
				if (Mesh != null)
				{
					bIgnore = MeshMap.ContainsKey(Mesh.GetName());
				}
			}

			return bIgnore;
		}

		public FDatasmithFacadeMesh GetCurrentMesh()
		{
			return ElementDataStack.Peek().GetCurrentMesh();
		}

		public Transform GetCurrentMeshPointsTransform()
		{
			return ElementDataStack.Peek().MeshPointsTransform;
		}

		public int GetCurrentMaterialIndex()
		{
			if (MaterialDataMap.ContainsKey(CurrentMaterialDataKey))
			{
				FMaterialData MaterialData = MaterialDataMap[CurrentMaterialDataKey];

				// Add the current Datasmith master material name to the dictionary of material names utilized by the Datasmith mesh being processed.
				GetCurrentMesh().AddMaterial(MaterialData.MaterialIndex, MaterialData.MasterMaterial.GetName());

				// Return the index of the current material.
				return MaterialData.MaterialIndex;
			}

			return 0;
		}

		public FBaseElementData GetCurrentActor()
		{
			return ElementDataStack.Peek().GetCurrentActor();
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
						DirectLink.OnBeginLinkedDocument(LinkedDoc);
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
			if (MaterialDataMap.ContainsKey(CurrentMaterialDataKey))
			{
				MaterialDataMap[CurrentMaterialDataKey].Log(InMaterialNode, InDebugLog, InLinePrefix);
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
			FDatasmithFacadeMesh InMesh
		)
		{
			if (InMesh.GetVertexCount() > 0 && InMesh.GetTriangleCount() > 0)
			{
				string MeshName = InMesh.GetName();

				if (!MeshMap.ContainsKey(MeshName))
				{
					// Keep track of the Datasmith mesh.
					MeshMap[MeshName] = InMesh;
				}
			}
		}

		private void AddCollectedMeshes(
			FDatasmithFacadeScene InDatasmithScene
		)
		{
			// Add the collected meshes from the Datasmith mesh dictionary to the Datasmith scene.
			foreach (FDatasmithFacadeMesh CollectedMesh in MeshMap.Values)
			{
				InDatasmithScene.AddMesh(CollectedMesh);
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
			// Add the collected master materials from the material data dictionary to the Datasmith scene.
			foreach (FMaterialData CollectedMaterialData in MaterialDataMap.Values)
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
					PushElement(ParentElement, Transform.Identity);
					PopElement();
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
