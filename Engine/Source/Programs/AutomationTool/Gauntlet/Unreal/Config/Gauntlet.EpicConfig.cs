// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using Gauntlet;
using System;
using System.Collections.Generic;
using UnrealBuildTool;

namespace EpicConfig
{
	public class UETelemetry : MySQLConfig<Gauntlet.TelemetryData>
	{
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
			return "test_records";
		}
		/// <summary>
		/// Get Target Table columns based on data type
		/// </summary>
		public override IEnumerable<string> GetTableColumns()
		{
			return new List<string>() {
				"changelist",
				"system",
				"stream",
				"dateTime",
				"project",
				"platform",
				"config",
				"testName",
				"dataPoint",
				"recordedValue"
			};
		}
		/// <summary>
		/// Format the data for target table based on data type
		/// </summary>
		/// <returns></returns>
		public override IEnumerable<string> FormatDataForTable(Gauntlet.TelemetryData Data, ITestContext TestContext)
		{
			if (TestContext is UnrealTestContext Context)
			{
				UnrealTestRoleContext Role = Context.RoleContext[UnrealTargetRole.Client];
				return new List<string>() {
					Context.BuildInfo.Changelist.ToString(),
					Environment.MachineName,
					Context.BuildInfo.Branch,
					DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"),
					Context.BuildInfo.ProjectName,
					Role.Platform.ToString(),
					Role.Configuration.ToString(),
					Data.TestName,
					Data.DataPoint,
					Data.Measurement.ToString()
				};
			}
			else
			{
				throw new AutomationException(string.Format("The Test Context is not UnrealTestContext; '{0}'.", TestContext.GetType().FullName));
			}
		}
	}
}