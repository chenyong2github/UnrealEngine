// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Autodesk.Revit.DB;
using Autodesk.Revit.DB.Events;
using Autodesk.Revit.DB.ExtensibleStorage;
using Autodesk.Revit.UI;

namespace DatasmithRevitExporter
{
	public class FSettings
	{
		private IList<string> _MetadataParamNamesFilter = new List<string>();
		private IList<int> _MetadataParamGroupsFilter = new List<int>();

		// Derived data for fast lookup
		private HashSet<string> MetadataParamNamesSet;
		private HashSet<int> MetadataParamGroupsSet;

		public int LevelOfTesselation { get; set; } = 8;

		public IList<string> MetadataParamNamesFilter 
		{
			get => _MetadataParamNamesFilter;
			set
			{
				_MetadataParamNamesFilter = value;
				MetadataParamNamesSet = new HashSet<string>(_MetadataParamNamesFilter);
			}
		}

		public IList<int> MetadataParamGroupsFilter 
		{
			get => _MetadataParamGroupsFilter;
			set
			{
				_MetadataParamGroupsFilter = value;
				MetadataParamGroupsSet = new HashSet<int>(_MetadataParamGroupsFilter);
			}
		}

		// Returns true if the parameter passes the filter, false otherwise.
		public bool MatchParameterByMetadata(Parameter InParam)
		{
			if (MetadataParamNamesSet != null && MetadataParamNamesSet.Count > 0)
			{
				bool bNameMatch = false;
				foreach (string NameFilter in MetadataParamNamesSet)
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
			bool bGroupMatch = true;
			if (MetadataParamGroupsSet != null && MetadataParamGroupsSet.Count > 0)
			{
				bGroupMatch = MetadataParamGroupsSet.Contains((int)InParam.Definition.ParameterGroup);
			}
		
			return bGroupMatch;
		}
	}

	public class FSettingsManager
	{
		static class FSettingsSchema
		{
			readonly static Guid SchemaGuid = new Guid("{EAFD9DF3-1E44-4B16-B9DF-E7DD404729A3}");
			
			public readonly static string MetadataParamNamesField = "MetadataParamNames";
			public readonly static string MetadataParamGroupsField = "MetadataParamGroups";
			public readonly static string LevelOfTesselationField = "LevelOfTesselation";

			public static Schema GetSchema()
			{
				Schema Schema = Schema.Lookup(SchemaGuid);
				if (Schema != null)
				{
					return Schema;
				}

				SchemaBuilder SchemaBuilder = new SchemaBuilder(SchemaGuid);
				SchemaBuilder.SetSchemaName("DatasmithRevitExportSettings");

				SchemaBuilder.AddArrayField(MetadataParamNamesField, typeof(string));
				SchemaBuilder.AddArrayField(MetadataParamGroupsField, typeof(int));
				SchemaBuilder.AddSimpleField(LevelOfTesselationField, typeof(int));

				return SchemaBuilder.Finish();
			}
		}

		static class DataStorageUniqueIdSchema
		{
			static readonly Guid SchemaGuid = new Guid("{2E363F82-4589-43BE-8927-C596AA02396F}");

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

		public static FSettings CurrentSettings = null;

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

		private static FSettings ReadSettings(Document Doc)
		{
			FSettings Settings = new FSettings();

			Entity SettingsEntity = GetSettingsEntity(Doc);

			if (SettingsEntity == null || !SettingsEntity.IsValid())
			{
				// No settings in document, create defaults
				Settings.LevelOfTesselation = 8;
				Settings.MetadataParamGroupsFilter.Add((int)Autodesk.Revit.DB.BuiltInParameterGroup.PG_GEOMETRY);
				Settings.MetadataParamGroupsFilter.Add((int)Autodesk.Revit.DB.BuiltInParameterGroup.PG_IDENTITY_DATA);
				Settings.MetadataParamGroupsFilter.Add((int)Autodesk.Revit.DB.BuiltInParameterGroup.PG_MATERIALS);
				Settings.MetadataParamGroupsFilter.Add((int)Autodesk.Revit.DB.BuiltInParameterGroup.PG_PHASING);
				Settings.MetadataParamGroupsFilter.Add((int)Autodesk.Revit.DB.BuiltInParameterGroup.PG_STRUCTURAL);

				WriteSettings(Doc, Settings);
			}
			else
			{
				Settings.MetadataParamNamesFilter = SettingsEntity.Get<IList<string>>(FSettingsSchema.MetadataParamNamesField);
				Settings.MetadataParamGroupsFilter = SettingsEntity.Get<IList<int>>(FSettingsSchema.MetadataParamGroupsField);
				Settings.LevelOfTesselation = SettingsEntity.Get<int>(FSettingsSchema.LevelOfTesselationField);
			}

			return Settings;
		}

		public static void WriteSettings(Document Doc, FSettings Settings)
		{
			using (Transaction T = new Transaction(Doc, "Write settings"))
			{
				try
				{
					T.Start();

					DataStorage SettingDataStorage = GetSettingsDataStorage(Doc);
					if (SettingDataStorage == null)
					{
						SettingDataStorage = DataStorage.Create(Doc);
					}

					Entity SettingsEntity = new Entity(FSettingsSchema.GetSchema());
					SettingsEntity.Set(FSettingsSchema.MetadataParamNamesField, Settings.MetadataParamNamesFilter);
					SettingsEntity.Set(FSettingsSchema.MetadataParamGroupsField, Settings.MetadataParamGroupsFilter);
					SettingsEntity.Set(FSettingsSchema.LevelOfTesselationField, Settings.LevelOfTesselation);

					// Identify settings data storage
					Entity IdEntity = new Entity(DataStorageUniqueIdSchema.GetSchema());
					IdEntity.Set("Id", SettingDataStorageId);

					SettingDataStorage.SetEntity(IdEntity);
					SettingDataStorage.SetEntity(SettingsEntity);

					CurrentSettings = Settings;

					T.Commit();
				}
				catch { }
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
				Entity SettingEntity = DataStorage.GetEntity(FSettingsSchema.GetSchema());

				// If a DataStorage contains setting entity, we found it
				if (!SettingEntity.IsValid())
				{
					continue;
				}

				return SettingEntity;
			}

			return null;
		}
	}
}
