// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Reflection;


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
	/// Test Report Base class
	/// </summary>
	public class BaseTestReport : ITestReport
	{
		/// <summary>
		/// Return report type
		/// </summary>
		public virtual string Type { get { return "Base Test Report"; } }

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
		public virtual void AddEvent(string Type, string Message, object Context = null)
		{
			throw new System.NotImplementedException("AddEvent not implemented");
		}

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
		public virtual bool AttachArtifact(string ArtifactPath, string Name = null)
		{
			throw new System.NotImplementedException("AttachArtifact not implemented");
		}
	}
}