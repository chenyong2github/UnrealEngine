// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Autodesk.Revit.DB;
using Autodesk.Revit.DB.Events;
using Autodesk.Revit.DB.ExtensibleStorage;
using Autodesk.Revit.UI;

namespace DatasmithRevitExporter
{
	public class FMetadataSettings
	{
		private IList<string> _ParamNamesFilter = new List<string>();
		private IList<int> _ParamGroupsFilter = new List<int>();

		// Derived data for fast lookup
		private HashSet<string> ParamNamesSet;
		private HashSet<int> ParamGroupsSet;

		public IList<string> ParamNamesFilter 
		{
			get => _ParamNamesFilter;
			set
			{
				_ParamNamesFilter = value;
				ParamNamesSet = new HashSet<string>(_ParamNamesFilter);
			}
		}

		public IList<int> ParamGroupsFilter 
		{
			get => _ParamGroupsFilter;
			set
			{
				_ParamGroupsFilter = value;
				ParamGroupsSet = new HashSet<int>(_ParamGroupsFilter);
			}
		}

		public bool ParamterPassesFilter(Parameter InParam)
		{
			if (ParamNamesSet != null && ParamNamesSet.Count > 0)
			{
				bool bNameMatch = false;
				foreach (string NameFilter in ParamNamesSet)
				{
					// Check if NameFilter is contained in the current param name
					if (InParam.Definition.Name.IndexOf(NameFilter, StringComparison.OrdinalIgnoreCase) >= 0)
					{
						bNameMatch = true;
						break;
					}
				}

				if (!bNameMatch)
				{
					return false;
				}
			}

			// Name filter passes (or empty), so check group filter.
			bool bGroupMatch = ParamGroupsSet.Contains((int)InParam.Definition.ParameterGroup);

			return bGroupMatch;
		}
	}

	public class FMetadataManager
	{
		static class FMatadataSettingsSchema
		{
			readonly static Guid SchemaGuid = new Guid("{CF94439A-55C3-46FC-966D-B5D42036AFAF}");
			
			public readonly static string ParamNamesField = "ParamNames";
			public readonly static string ParamGroupsField = "ParamGroups";

			public static Schema GetSchema()
			{
				Schema Schema = Schema.Lookup(SchemaGuid);
				if (Schema != null)
				{
					return Schema;
				}

				SchemaBuilder SchemaBuilder = new SchemaBuilder(SchemaGuid);
				SchemaBuilder.SetSchemaName("DatasmithRevitMetadataExportSettings");

				SchemaBuilder.AddArrayField(ParamNamesField, typeof(string));
				SchemaBuilder.AddArrayField(ParamGroupsField, typeof(int));

				return SchemaBuilder.Finish();
			}
		}

		static class DataStorageUniqueIdSchema
		{
			static readonly Guid SchemaGuid = new Guid("{0A99DE0F-F096-4EA9-9739-219ECDF83F7E}");

			public static Schema GetSchema()
			{
				Schema Schema = Schema.Lookup(SchemaGuid);
				if (Schema != null)
				{
					return Schema;
				}

				SchemaBuilder SchemaBuilder = new SchemaBuilder(SchemaGuid);
				SchemaBuilder.SetSchemaName("DataStorageUniqueId");
				SchemaBuilder.AddSimpleField("Id", typeof(Guid));

				return SchemaBuilder.Finish();
			}
		}

		static EventHandler<DocumentOpenedEventArgs> DocumentOpenedHandler;
		static EventHandler<DocumentClosingEventArgs> DocumentClosingHandler;
		static readonly Guid SettingDataStorageId = new Guid("{6CD2BB42-4A04-4F12-8E7F-5C6723C33791}");

		public static FMetadataSettings CurrentSettings = null;

		public static void Init(UIControlledApplication InApplication)
		{
			DocumentOpenedHandler = new EventHandler<DocumentOpenedEventArgs>(OnDocumentOpened);
			DocumentClosingHandler = new EventHandler<DocumentClosingEventArgs>(OnDocumentClosing);
			InApplication.ControlledApplication.DocumentOpened += DocumentOpenedHandler;
			InApplication.ControlledApplication.DocumentClosing += DocumentClosingHandler;
		}

		public static void Destroy(UIControlledApplication InApplication)
		{
			InApplication.ControlledApplication.DocumentClosing -= DocumentClosingHandler;
			InApplication.ControlledApplication.DocumentOpened -= DocumentOpenedHandler;
			DocumentClosingHandler = null;
			DocumentOpenedHandler = null;
		}

		private static FMetadataSettings ReadSettings(Document Doc)
		{
			Entity SettingsEntity = GetSettingsEntity(Doc);

			if (SettingsEntity == null || !SettingsEntity.IsValid())
			{
				return null;
			}

			FMetadataSettings Settings = new FMetadataSettings();
			Settings.ParamNamesFilter = SettingsEntity.Get<IList<string>>(FMatadataSettingsSchema.ParamNamesField);
			Settings.ParamGroupsFilter = SettingsEntity.Get<IList<int>>(FMatadataSettingsSchema.ParamGroupsField);

			return Settings;
		}

		public static void WriteSettings(Document Doc, FMetadataSettings Settings)
		{
			using (Transaction T = new Transaction(Doc, "Write settings"))
			{
				T.Start();

				DataStorage SettingDataStorage = GetSettingsDataStorage(Doc);
				if (SettingDataStorage == null)
				{
					SettingDataStorage = DataStorage.Create(Doc);
				}

				Entity SettingsEntity = new Entity(FMatadataSettingsSchema.GetSchema());
				SettingsEntity.Set(FMatadataSettingsSchema.ParamNamesField, Settings.ParamNamesFilter);
				SettingsEntity.Set(FMatadataSettingsSchema.ParamGroupsField, Settings.ParamGroupsFilter);

				// Identify settings data storage
				Entity IdEntity = new Entity(DataStorageUniqueIdSchema.GetSchema());
				IdEntity.Set("Id", SettingDataStorageId);

				SettingDataStorage.SetEntity(IdEntity);
				SettingDataStorage.SetEntity(SettingsEntity);

				CurrentSettings = Settings;

				T.Commit();
			}
		}

		public static void AddActorMetadata(Element InElement, FDatasmithFacadeMetaData ActorMetadata, bool bInRespectFilter)
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
			AddActorMetadata(InElement, "Element*", ActorMetadata, bInRespectFilter);

			if (ElemType != null)
			{
				// Add Revit element type metadata to the Datasmith actor.
				AddActorMetadata(ElemType, "Type*", ActorMetadata, bInRespectFilter);
			}
		}

		public static void AddActorMetadata(
			Element InSourceElement,
			string InMetadataPrefix,
			FDatasmithFacadeMetaData ElementMetaData,
			bool bInRespectFilter
		)
		{
			IList<Parameter> Parameters = InSourceElement.GetOrderedParameters();

			if (Parameters != null)
			{
				foreach (Parameter Parameter in Parameters)
				{
					if (Parameter.HasValue)
					{
						if (bInRespectFilter && CurrentSettings != null)
						{
							if (!CurrentSettings.ParamterPassesFilter(Parameter))
							{
								continue; // Skip export of this param
							}
						}

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

		static void OnDocumentOpened(object sender, DocumentOpenedEventArgs e)
		{
			CurrentSettings = ReadSettings(e.Document);
		}

		static void OnDocumentClosing(object sender, DocumentClosingEventArgs e)
		{
			CurrentSettings = null;
		}

		private static DataStorage GetSettingsDataStorage(Document Doc)
		{
			// Retrieve all data storages from project
			FilteredElementCollector Collector = new FilteredElementCollector(Doc);
			var DataStorages = Collector.OfClass(typeof(DataStorage));

			// Find setting data storage
			foreach (DataStorage DataStorage in DataStorages)
			{
				Entity SettingIdEntity = DataStorage.GetEntity(DataStorageUniqueIdSchema.GetSchema());

				if (!SettingIdEntity.IsValid())
				{
					continue;
				}

				Guid Id = SettingIdEntity.Get<Guid>("Id");

				if (!Id.Equals(SettingDataStorageId))
				{
					continue;
				}

				return DataStorage;
			}
			return null;
		}

		private static Entity GetSettingsEntity(Document Doc)
		{
			FilteredElementCollector Collector = new FilteredElementCollector(Doc);
			var DataStorages = Collector.OfClass(typeof(DataStorage));

			// Find setting data storage
			foreach (DataStorage DataStorage in DataStorages)
			{
				Entity SettingEntity = DataStorage.GetEntity(FMatadataSettingsSchema.GetSchema());

				// If a DataStorage contains setting entity, we found it
				if (!SettingEntity.IsValid())
				{
					continue;
				}

				return SettingEntity;
			}

			return null;
		}

		private static string GetCategoryName(Element InElement)
		{
			ElementType Type = GetElementType(InElement);
			return Type?.Category?.Name ?? InElement.Category?.Name;
		}

		private static ElementType GetElementType(Element InElement)
		{
			return InElement.Document.GetElement(InElement.GetTypeId()) as ElementType;
		}
	}
}
