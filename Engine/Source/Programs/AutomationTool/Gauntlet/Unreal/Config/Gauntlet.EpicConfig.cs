// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using Gauntlet;
using System;
using System.Data;
using System.Collections.Generic;
using UnrealBuildTool;

namespace EpicConfig
{
	/// <summary>
	/// Production UE Telemetry config
	/// </summary>
	public class UETelemetry : MySQLConfig<Gauntlet.TelemetryData>
	{
		protected virtual string TableName { get { return "test_records_prod"; } }
		protected virtual string BuildTableName { get { return "test_builds_prod"; } }
		private int BuildID = -1;
		public override void LoadConfig(string ConfigFilePath)
		{
			if (string.IsNullOrEmpty(ConfigFilePath))
			{
				if (HostPlatform.Current.HostEditorPlatform == UnrealTargetPlatform.Win64)
				{
					ConfigFilePath = @"\\epicgames.net\root\Builds\Automation\UE5\Config\ue-automation-telemetry-mysql-config.txt";
				}
				else
				{
					ConfigFilePath = "/Volumes/Root/Builds/Automation/UE5/Config/ue-automation-telemetry-mysql-config.txt";
				}
			}
			base.LoadConfig(ConfigFilePath);
		}
		/// <summary>
		/// Get Target Table name based on data type
		/// </summary>
		public override string GetTableName()
		{
			return TableName;
		}
		/// <summary>
		/// Get Target Table columns based on data type
		/// </summary>
		public override IEnumerable<string> GetTableColumns()
		{
			return new List<string>() {
				"buildId",
				"system",
				"dateTime",
				"platform",
				"config",
				"testName",
				"context",
				"dataPoint",
				"recordedValue",
				"unit",
				"baseline",
				"jobLink"
			};
		}
		/// <summary>
		/// Format the data for target table based on data type
		/// </summary>
		/// <returns></returns>
		public override IEnumerable<string> FormatDataForTable(Gauntlet.TelemetryData Data, ITelemetryContext Context)
		{
			return new List<string>() {
				BuildID.ToString(),
				Environment.MachineName,
				DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"),
				Context.GetProperty("Platform").ToString(),
				Context.GetProperty("Configuration").ToString(),
				Data.TestName,
				Data.Context,
				Data.DataPoint,
				Data.Measurement.ToString(),
				Data.Unit,
				Data.Baseline.ToString(),
				Context.GetProperty("JobLink").ToString()
			};
		}
		/// <summary>
		/// Get Build Table name
		/// </summary>
		public virtual string GetBuildTableName()
		{
			return BuildTableName;
		}
		/// <summary>
		/// Query DB to get build id or add it and store the build id.
		/// </summary>
		/// <param name="Driver"></param>
		/// <param name="DataRows"></param>
		/// <param name="Context"></param>
		public override bool PreSubmitQuery(IDatabaseDriver<Gauntlet.TelemetryData> Driver, IEnumerable<Gauntlet.TelemetryData> DataRows, ITelemetryContext Context)
		{
			string Changelist = Context.GetProperty("Changelist").ToString();
			string ChangelistDateTime = ((DateTime)Context.GetProperty("ChangelistDateTime")).ToString("yyyy-MM-dd HH:mm:ss");
			string Stream = Context.GetProperty("Branch").ToString();
			string Project = Context.GetProperty("ProjectName").ToString();

			string SqlQuery = @$"INSERT INTO `{DatabaseName}`.{BuildTableName} (changelist, changelistDateTime, stream, project)
									SELECT '{Changelist}', '{ChangelistDateTime}', '{Stream}', '{Project}' FROM DUAL
										WHERE NOT EXISTS (SELECT 1 FROM `{DatabaseName}`.{BuildTableName} WHERE changelist = '{Changelist}' AND stream = '{Stream}' AND project = '{Project}');
									SELECT id FROM `{DatabaseName}`.{BuildTableName} WHERE changelist = '{Changelist}' AND stream = '{Stream}' AND project = '{Project}';";

			DataSet Set = Driver.ExecuteQuery(SqlQuery);
			// Get value from first row from first column
			if (Set.Tables.Count == 0 || Set.Tables[0].Rows.Count == 0)
			{
				Log.Error("No data return from {0} insert Query.", BuildTableName);
				return false;
			}
			BuildID = (int)Set.Tables[0].Rows[0][0];

			return true;
		}
	}
	/// <summary>
	/// Staging UE Telemetry Config
	/// </summary>
	public class UETelemetryStaging : UETelemetry
	{
		protected override string TableName { get { return "test_records_staging"; } }
		protected override string BuildTableName { get { return "test_builds_staging"; } }
	}
}