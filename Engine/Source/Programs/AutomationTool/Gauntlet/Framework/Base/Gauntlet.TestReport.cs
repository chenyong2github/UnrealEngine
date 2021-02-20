// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Reflection;
using System.Collections.Generic;


namespace Gauntlet
{
	/// <summary>
	/// Interface for test report
	/// </summary>
	public interface ITestReport
	{
		/// <summary>
		/// Return report type
		/// </summary>
		string Type { get; }

		/// <summary>
		/// Set a property of the test report type
		/// </summary>
		/// <param name="Property"></param>
		/// <param name="Value"></param>
		/// <returns></returns>
		void SetProperty(string Property, object Value);

		/// <summary>
		/// Add event to the test report
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="Message"></param>
		/// <param name="Context"></param>
		/// <returns></returns>
		void AddEvent(string Type, string Message, object Context = null);

		/// <summary>
		/// Attach an Artifact to the ITestReport
		/// </summary>
		/// <param name="ArtifactPath"></param>
		/// <returns></returns>
		bool AttachArtifact(string ArtifactPath, string Name = null);
	}

	/// <summary>
	/// Hold telemetry data
	/// </summary>
	public class TelemetryData
	{
		public string TestName { get; private set; }
		public string DataPoint { get; private set; }
		public double Measurement { get; private set; }
		public TelemetryData(string InTestName, string InDataPoint, double InMeasurement)
		{
			TestName = InTestName;
			DataPoint = InDataPoint;
			Measurement = InMeasurement;
		}
	}

	/// <summary>
	/// Interface for telemetry report
	/// </summary>
	public interface ITelemetryReport
	{ 
		/// <summary>
		/// Attach Telemetry data to the ITestReport
		/// </summary>
		/// <param name="TestName"></param>
		/// <param name="DataPoint"></param>
		/// <param name="Measurement"></param>
		/// <returns></returns>
		void AddTelemetry(string TestName, string DataPoint, double Measurement);

		/// <summary>
		/// Return the telemetry data accumulated
		/// </summary>
		/// <returns></returns>
		IEnumerable<TelemetryData> GetAllTelemetryData();
	}

	/// <summary>
	/// Test Report Base class
	/// </summary>
	public abstract class BaseTestReport : ITestReport, ITelemetryReport
	{
		/// <summary>
		/// Return report type
		/// </summary>
		public virtual string Type { get { return "Base Test Report"; } }
	
		/// <summary>
		/// Hold the list of telemetry data accumulated
		/// </summary>
		protected List<TelemetryData> TelemetryDataList;

		/// <summary>
		/// Set a property of the test report type
		/// </summary>
		/// <param name="Property"></param>
		/// <param name="Value"></param>
		/// <returns></returns>
		public virtual void SetProperty(string Property, object Value)
		{
			PropertyInfo PropertyInstance = GetType().GetProperty(Property);
			PropertyInstance.SetValue(this, Convert.ChangeType(Value, PropertyInstance.PropertyType));
		}

		/// <summary>
		/// Add event to the test report
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="Message"></param>
		/// <param name="Context"></param>
		/// <returns></returns>
		public abstract void AddEvent(string Type, string Message, object Context = null);

		public void AddError(string Message, object Context = null)
		{
			AddEvent("Error", Message, Context);
		}
		public void AddWarning(string Message, object Context = null)
		{
			AddEvent("Warning", Message, Context);
		}
		public void AddInfo(string Message, object Context = null)
		{
			AddEvent("Info", Message, Context);
		}

		/// <summary>
		/// Attach an Artifact to the ITestReport
		/// </summary>
		/// <param name="ArtifactPath"></param>
		/// <param name="Name"></param>
		/// <returns>return true if the file was successfully attached</returns>
		public abstract bool AttachArtifact(string ArtifactPath, string Name = null);

		/// <summary>
		/// Attach Telemetry data to the ITestReport
		/// </summary>
		/// <param name="TestName"></param>
		/// <param name="DataPoint"></param>
		/// <param name="Measurement"></param>
		/// <returns></returns>
		public virtual void AddTelemetry(string TestName, string DataPoint, double Measurement)
		{
			if (TelemetryDataList is null)
			{
				TelemetryDataList = new List<TelemetryData>();
			}

			TelemetryDataList.Add(new TelemetryData(TestName, DataPoint, Measurement));
		}

		/// <summary>
		/// Return all the telemetry data stored
		/// </summary>
		/// <returns></returns>
		public virtual IEnumerable<TelemetryData> GetAllTelemetryData()
		{
			return TelemetryDataList;
		}
	}
}