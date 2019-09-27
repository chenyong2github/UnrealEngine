// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using BuildAgent.Run;
using BuildAgent.Run.Interfaces;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using Tools.DotNETCommon;

namespace BuildAgent.Listeners
{
	/// <summary>
	/// Writes parsed errors to a log file usable by Electric Commander
	/// </summary>
	class ElectricCommanderListener : IErrorListener
	{
		/// <summary>
		/// Path to the output file
		/// </summary>
		FileReference OutputFile;

		/// <summary>
		/// List of the matched errors
		/// </summary>
		List<ErrorMatch> Errors = new List<ErrorMatch>();

		/// <summary>
		/// Whether we should set a property indicating that the diagnostics file exists
		/// </summary>
		bool bUpdateProperty = true;

		/// <summary>
		/// Process which is created to update properties in the background
		/// </summary>
		Process UpdatePropertyProcess;

		/// <summary>
		/// Constructs an EC listener
		/// </summary>
		/// <param name="OutputFile">Path to the output file</param>
		public ElectricCommanderListener()
		{
			string EnvVar = Environment.GetEnvironmentVariable("COMMANDER_JOBSTEP");
			if (String.IsNullOrEmpty(EnvVar))
			{
				OutputFile = new FileReference("BuildAgent.xml");
				bUpdateProperty = false;
			}
			else
			{
				OutputFile = new FileReference(String.Format("BuildAgent-{0}.xml", EnvVar));
				bUpdateProperty = true;
			}
		}

		/// <summary>
		/// Wait for the child process to finish
		/// </summary>
		public void Dispose()
		{
			if(UpdatePropertyProcess != null)
			{
				if (!UpdatePropertyProcess.WaitForExit(0))
				{
					Log.TraceInformation("Waiting for ectool to terminate...");
					UpdatePropertyProcess.WaitForExit();
				}
				if (UpdatePropertyProcess.ExitCode != 0)
				{
					Log.TraceWarning("ectool terminated with exit code {0}", UpdatePropertyProcess.ExitCode);
				}
				UpdatePropertyProcess.Dispose();
				UpdatePropertyProcess = null;
			}
		}

		/// <summary>
		/// Called when an error is matched
		/// </summary>
		/// <param name="Error">The matched error</param>
		public void OnErrorMatch(ErrorMatch Error)
		{
			Errors.Add(Error);

			Write();

			if(bUpdateProperty)
			{
				UpdatePropertyProcess = Process.Start("ectool", String.Format("setProperty /myJobStep/diagFile {0}", OutputFile.GetFileName()));
				bUpdateProperty = false;
			}
		}

		/// <summary>
		/// Write the list of errors to disk in XML format
		/// </summary>
		public void Write()
		{
			XmlWriterSettings Settings = new XmlWriterSettings();
			Settings.NewLineChars = Environment.NewLine;
			Settings.Indent = true;
			Settings.IndentChars = "\t";
			using (FileStream Stream = File.Open(OutputFile.FullName, FileMode.Create, FileAccess.Write, FileShare.Read | FileShare.Delete))
			{
				using (XmlWriter Writer = XmlWriter.Create(Stream, Settings))
				{
					Writer.WriteStartElement("diagnostics");
					foreach (ErrorMatch Error in Errors)
					{
						if (Error.Severity != ErrorSeverity.Silent)
						{
							Writer.WriteStartElement("diagnostic");

							Writer.WriteElementString("matcher", Error.Type);
							Writer.WriteElementString("name", "");

							if(Error.Severity == ErrorSeverity.Error)
							{
								Writer.WriteElementString("type", "error");
							}
							else
							{
								Writer.WriteElementString("type", "warning");
							}

							Writer.WriteElementString("firstLine", Error.MinLineNumber.ToString());
							Writer.WriteElementString("numLines", (Error.MaxLineNumber + 1 - Error.MinLineNumber).ToString());

							StringBuilder Message = new StringBuilder();
							foreach(string Line in Error.Lines)
							{
								Message.AppendLine(Line);
							}
							Writer.WriteElementString("message", Message.ToString());

							Writer.WriteEndElement();
						}
					}
					Writer.WriteEndElement();
				}
			}
		}
	}
}
