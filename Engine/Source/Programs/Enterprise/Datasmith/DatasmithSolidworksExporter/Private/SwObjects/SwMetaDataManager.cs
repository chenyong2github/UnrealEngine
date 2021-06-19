// Copyright Epic Games, Inc. All Rights Reserved.

using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;

namespace SolidworksDatasmith.SwObjects
{
	public class SwMetaDataManager
	{
		public interface MetadataPair
		{
			void WriteToDatasmithMetaData(FDatasmithFacadeMetaData metadata);
		}

		public class MetadataStringPair : MetadataPair
		{
			private string _metadataname;
			private string _metadatavalue;
			public MetadataStringPair(string name, string value)
			{
				_metadataname = name;
				_metadatavalue = value;
			}

			public void WriteToDatasmithMetaData(FDatasmithFacadeMetaData metadata)
			{
				metadata.AddPropertyString(_metadataname, _metadatavalue);
			}
		}

		public class MetadataBoolPair : MetadataPair
		{
			private string _metadataname;
			private bool _metadatavalue;
			public MetadataBoolPair(string name, bool value)
			{
				_metadataname = name;
				_metadatavalue = value;
			}

			public void WriteToDatasmithMetaData(FDatasmithFacadeMetaData metadata)
			{
				metadata.AddPropertyBoolean(_metadataname, _metadatavalue);
			}
		}

		private SwScene                 _swscene = null;        
		public SwMetaDataManager(SwScene swscene)
		{
			_swscene = swscene;            
		}

		static public void AddDocumentMetadataToCommand(ModelDoc2 modeldoc, Engine.MetadataCommand metadatacommand)
		{
			string doctype = "";
			bool ispart = false;
			if (modeldoc is AssemblyDoc)
			{
				doctype = "Assembly";
			}
			else if (modeldoc is PartDoc)
			{
				doctype = "Part";
				ispart = true;
			}
			metadatacommand.AddPair("Document_Type", doctype);
			metadatacommand.AddPair("Document_Filename", System.IO.Path.GetFileName(modeldoc.GetPathName()));

			ExportCustomProperties(modeldoc, metadatacommand);
			ExportCurrentConfigurationCustomProperties(modeldoc, metadatacommand);

			//Disabling body and feature properties matadata export for now. See "failed attempt" in ExportBodiesProperties
			//if (ispart == true)
			//{
			//    ExportPartProperties(modeldoc as PartDoc, metaData);
			//}
			//else //is an assembly
			//{
			//    AddAssemblyDisplayStateMetadata(modeldoc as AssemblyDoc, metaData);
			//}

			if (ispart == false)
			{
				AddAssemblyDisplayStateMetadata(modeldoc as AssemblyDoc, metadatacommand);
			}

			ExportCommentsandBOM(modeldoc, metadatacommand);

			//string[] tableNames = null;
			//SolidWorks.Interop.sw ISwDMDocument15 
			//tableNames = (string[])modeldoc.table GetTableNames(SwDmTableType.swDmTableTypeBOM);
		}        

		static private bool ExportCurrentConfigurationCustomProperties(ModelDoc2 modeldoc, Engine.MetadataCommand metadatacommand)
		{
			Configuration activeconf = modeldoc.GetActiveConfiguration();            
			CustomPropertyManager activeconfcustompropertymanager = activeconf.CustomPropertyManager;

			return ExportCustomPropertyManagerToMetadata(activeconfcustompropertymanager, metadatacommand, "Active_Configuration_Custom_Property_");
		}

		static private bool ExportCustomProperties(ModelDoc2 modeldoc, Engine.MetadataCommand metadatacommand)
		{
			ModelDocExtension ext = modeldoc.Extension;
			if (ext == null)
			{
				return false;
			}

			//Add document related custom properties metadata
			CustomPropertyManager _doccustompropertymanager = ext.get_CustomPropertyManager("");

			if (_doccustompropertymanager == null)
			{
				return false;
			}
			return ExportCustomPropertyManagerToMetadata(_doccustompropertymanager, metadatacommand, "Document_Custom_Property_");
		}

		static private bool ExportPartProperties(PartDoc partdoc, Engine.MetadataCommand metadatacommand)
		{
			var enum3 = partdoc.EnumBodies3((int)swBodyType_e.swAllBodies, false);
			Body2 body = null;
			do
			{
				int fetched = 0;
				enum3.Next(1, out body, ref fetched);
				if (body != null)
				{
					ExportBodiesProperties(fetched, body, metadatacommand);
				}
			} while (body != null);
			return true;
		}

		static private bool ExportBodiesProperties(int bodyindex, Body2 body, Engine.MetadataCommand metadatacommand)
		{
			//SOLIDWORKS recommends that you use the IAttribute, IAttributeDef, and IParameter interfaces
			//instead of the IBody2::AddPropertyExtension2 method

			//IAttribute
			//body.FindAttribute(attributedef
			//How to list the attributedef ??
			string bodyindexstr = "Body_" + bodyindex;
			metadatacommand.AddPair(bodyindexstr, body.Name);

			//parse features
			object[] features = (object[]) body.GetFeatures();
			Feature feature = null;
			for (int i = 0; i < body.GetFeatureCount(); i++)
			{
				feature = (Feature) features[i];
				metadatacommand.AddPair(bodyindexstr + "_Feature_" + feature.GetTypeName2() , feature.Name);

				//failed attempt
				//string featuretype = feature.GetTypeName2();
				//if (featuretype == "Attribute")
				//{
				//    Attribute attr = (Attribute)feature.GetSpecificFeature2();
				//    System.Diagnostics.Debug.WriteLine(attr.GetName());
				//    AttributeDef def = attr.GetDefinition();
				//    if (false == attr.GetEntityState((int)swAssociatedEntityStates_e.swIsEntityInvalid) & false == attr.GetEntityState((int)swAssociatedEntityStates_e.swIsEntitySuppressed) & false == attr.GetEntityState((int)swAssociatedEntityStates_e.swIsEntityAmbiguous) & false == attr.GetEntityState((int)swAssociatedEntityStates_e.swIsEntityDeleted))
				//    {
				//        bool valid = true;
				//        //Parameter paramname = (Parameter)attr.GetParameter(????? );//no way to get attribute name from AttributeDef ????
				//        //Parameter paramvalue = (Parameter)attr.GetParameter(???);//same for attribute value name ?
				//        //Debug.Print("Attribute " + swParamName.GetStringValue() + " found.");
				//        //Debug.Print("  Value = " + swParamValue.GetDoubleValue());

				//    }
				//}

				var faces = feature.GetFaces();
				if (faces != null)
				{
					int j = 0;
					foreach (var obj in faces)
					{
						ExportFaceProperties(j, (Face2)obj, metadatacommand);
						j++;
					}
				}
			}
			return true;
		}

		static private bool ExportFaceProperties(int faceindex, Face2 face, Engine.MetadataCommand metadatacommand)
		{
			metadatacommand.AddPair("Face_" + faceindex, face.GetFaceId().ToString());
			return true;
		}

		static private bool ExportCommentsandBOM(ModelDoc2 modeldoc, Engine.MetadataCommand metadatacommand)
		{
			FeatureManager featureManager = modeldoc.FeatureManager;
			if (featureManager == null)
			{
				return false;
			}

			Feature feature = modeldoc.FirstFeature();
			string featuretype = null;
			CommentFolder commentFolder = null;
			object[] comments = null;
			Comment comment = null;
			//BomFeature bomFeature = null;
			int commentcount = 0;
			while (feature !=  null)
			{
				featuretype = feature.GetTypeName2();
				if (featuretype == null)
				{
					feature = feature.GetNextFeature();
					continue;
				}                

				if (featuretype == "CommentsFolder")
				{
					commentFolder = (CommentFolder)feature.GetSpecificFeature2();
					commentcount = commentFolder.GetCommentCount();
					if (commentFolder != null && commentcount != 0)
					{
						comments = (object[])commentFolder.GetComments();
						if (comments != null)
						{
							for (int i = 0; i < commentcount; i++)
							{
								comment = (Comment)comments[i];
								metadatacommand.AddPair("Comment_" + comment.Name, comment.Text);
							}
						}
					}
				}
				//BOM feature reading disabled until we
				//handle BOM in configuration handling context
				/*
				//"BomFeat" is not found if BOM is added to Tables list. Is there another way to add a BOM that would create "BomFeat" feature ? 
				//used insert > table > BOM from main menu to add a BOM
				else if (featuretype == "BomFeat") 
				{
					bomFeature = (BomFeature)feature.GetSpecificFeature2();
					if (bomFeature != null)
					{
						ExportBOMFeature(bomFeature, metadatacommand);
					}                    
				}
				*/
				feature = feature.GetNextFeature();
			}
			return true;
		}

		private bool ExportBOMFeature(BomFeature bomFeature, Engine.MetadataCommand metadatacommand)
		{
			Feature feat = bomFeature.GetFeature();
			string featurename = feat.Name;
			metadatacommand.AddPair("BOMTable_FeatureName", featurename);

			object[] tables = (object[])bomFeature.GetTableAnnotations();
			if (tables == null || tables.Length == 0)
			{
				return false;
			}

			foreach (object table in tables)
			{
				ExportTable((TableAnnotation)table, metadatacommand, featurename);
			}
			return true;
		}

		private void ExportTable(TableAnnotation table, Engine.MetadataCommand metadatacommand, string featurenameprefix)
		{
			int nbhearders = table.GetHeaderCount();
			if (nbhearders == 0)
			{
				//TOD log error here
				return;
			}
			int index = 0;
			int splitcount = 0;
			int rangestart = 0;
			int rangeend = 0;
			int nbrows = 0;
			int nbcols = 0;
			swTableSplitDirection_e splitdirection = (swTableSplitDirection_e) table.GetSplitInformation(ref index, ref splitcount, ref rangestart, ref rangeend);
			if (splitdirection == swTableSplitDirection_e.swTableSplit_None)
			{
				nbrows = table.RowCount;
				nbcols = table.ColumnCount;
				rangestart = nbhearders;
				rangeend = nbrows - 1;
			}
			else
			{
				nbcols = table.ColumnCount;
				if (index == 1)
				{
					// Add header offset for first portion of table
					rangestart += nbhearders;
				}
			}

			if (table.TitleVisible)
			{
				metadatacommand.AddPair("BOMTable_Feature_" + featurenameprefix, table.Title);
			}

			string[] headerstitles = new string[nbhearders];
			for (int i = 0; i < nbhearders; i++)
			{
				headerstitles[i] = (string)table.GetColumnTitle2(i, true);
			}

			for (int i = rangestart; i <= rangeend; i++)
			{
				for (int j = 0; j < nbcols; j++)
				{
					metadatacommand.AddPair("BOMTable_Feature_" + featurenameprefix + "_" + headerstitles[j], table.Text2[i, j, true]);
				}
			}

		}


		
		static public bool AddAssemblyComponentMetadata(Component2 component, Engine.MetadataCommand metadatacommand)
		{
			//object[] varComp = (object[])(assemblydoc.GetComponents(false));
			//int nbcomponents = assemblydoc.GetComponentCount(false);
			//Component2 component = null;
			//for (int i = 0; i < nbcomponents; i++)
			//{
			//    component = (Component2)varComp[i];
			//}
			metadatacommand.AddPair("ComponentReference", component.ComponentReference);

			return true;
		}

		static private bool AddAssemblyDisplayStateMetadata(AssemblyDoc assemblydoc, Engine.MetadataCommand metadatacommand)
		{
			ModelDocExtension ext = ((ModelDoc2)assemblydoc).Extension;
			if (ext == null)
			{
				return false;
			}

			ConfigurationManager cfm = ((ModelDoc2)assemblydoc).ConfigurationManager;
			if (cfm != null)
			{
				Configuration activeconf = cfm.ActiveConfiguration;
				object[] displaystates = activeconf.GetDisplayStates();
				if (displaystates != null)
				{
					string activedisplaystatename = (string)displaystates[0];
					if (activedisplaystatename != null)
					{
						metadatacommand.AddPair("ActiveDisplayState", (string)displaystates[0]);
					}
				}
			}

			object[] varComp = (object[])(assemblydoc.GetComponents(false));
			int nbcomponents = assemblydoc.GetComponentCount(false);
			Component2[] listComp = new Component2[nbcomponents];
			DisplayStateSetting dss = ext.GetDisplayStateSetting((int)swDisplayStateOpts_e.swThisDisplayState);
			dss.Option = (int)swDisplayStateOpts_e.swThisDisplayState;

			for (int i = 0; i < nbcomponents; i++)
			{
				listComp[i] = (Component2)varComp[i];
			}
			dss.Entities = listComp;

			System.Array displaymodearray = (System.Array)ext.DisplayMode[dss];
			System.Array transparencyarray = (System.Array)ext.Transparency[dss];
			for (int i = 0; i < nbcomponents; i++)
			{
				metadatacommand.AddPair("Component_Display_State_DisplayMode_" + ((Component2)varComp[i]).Name2, ((swDisplayMode_e)displaymodearray.GetValue(i)).ToString());
				metadatacommand.AddPair("Component_Display_State_Transparency_" + ((Component2)varComp[i]).Name2, ((swTransparencyState_e)transparencyarray.GetValue(i)).ToString());
			}
			return true;
		}

		static private bool ExportCustomPropertyManagerToMetadata(CustomPropertyManager custompropertymanager, Engine.MetadataCommand metadatacommand, string prefix)
		{
			if (custompropertymanager == null)
			{
				return false;
			}
			object propertiesnamesobject = null;
			object propertiesvaluesobject = null;
			string[] propertiesnames;
			object[] propertiesvalues;
			int[] propertiestypes;
			object propertiestypesobject = null;
			object resolvedobject = false;
			object linktopropobject = false;
			custompropertymanager.GetAll3(ref propertiesnamesobject, ref propertiestypesobject, ref propertiesvaluesobject, ref resolvedobject, ref linktopropobject);
			propertiesnames = (string[])propertiesnamesobject;
			propertiesvalues = (object[])propertiesvaluesobject;
			propertiestypes = (int[])propertiestypesobject;
			for (int i = 0; i < custompropertymanager.Count; i++)
			{
				switch (propertiestypes[i])
				{
					case (int)swCustomInfoType_e.swCustomInfoUnknown:
						break;

					case (int)swCustomInfoType_e.swCustomInfoNumber:
					case (int)swCustomInfoType_e.swCustomInfoDouble:
					case (int)swCustomInfoType_e.swCustomInfoText:
					case (int)swCustomInfoType_e.swCustomInfoDate:
						metadatacommand.AddPair(prefix + propertiesnames[i], propertiesvalues[i].ToString());
						break;

					case (int)swCustomInfoType_e.swCustomInfoYesOrNo:
						metadatacommand.AddPair(prefix + propertiesnames[i], propertiesvalues[i].ToString() == "Yes");
						break;

					default:
						break;
				}
			}
			return true;
		}
	}
}