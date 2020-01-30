// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Run;
using BuildAgent.Run.Interfaces;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Xml;
using Tools.DotNETCommon;

namespace BuildAgent.Run.Listeners
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
		/// New errors which are posted for the background thread
		/// </summary>
		List<ErrorMatch> NewErrors = new List<ErrorMatch>();

		/// <summary>
		/// Object used for synchronization
		/// </summary>
		object LockObject = new object();

		/// <summary>
		/// Whether we should set a property indicating that the diagnostics file exists
		/// </summary>
		bool bUpdateProperties;

		/// <summary>
		/// Thread used to update EC properties with the error/warning count
		/// </summary>
		Thread BackgroundThread;

		/// <summary>
		/// Event that is set whenever EC needs to be updated
		/// </summary>
		AutoResetEvent UpdateEvent = new AutoResetEvent(false);

		/// <summary>
		/// Set when the object is being disposed
		/// </summary>
		bool bDisposing;

		/// <summary>
		/// Constructs an EC listener
		/// </summary>
		/// <param name="OutputFile">Path to the output file</param>
		public ElectricCommanderListener()
		{
			string EnvVar = Environment.GetEnvironmentVariable("COMMANDER_JOBSTEPID");
			if (String.IsNullOrEmpty(EnvVar))
			{
				OutputFile = new FileReference("BuildAgent.xml");
				bUpdateProperties = false;
			}
			else
			{
				OutputFile = new FileReference(String.Format("BuildAgent-{0}.xml", EnvVar));
				bUpdateProperties = true;
			}
		}

		/// <summary>
		/// Wait for the child process to finish
		/// </summary>
		public void Dispose()
		{
			if (BackgroundThread != null)
			{
				lock(LockObject)
				{
					bDisposing = true;
				}
				UpdateEvent.Set();

				if (!BackgroundThread.Join(500))
				{
					Log.TraceInformation("Waiting for ectool to terminate...");
					BackgroundThread.Join();
				}

				BackgroundThread = null;
			}

			if(UpdateEvent != null)
			{
				UpdateEvent.Dispose();
				UpdateEvent = null;
			}
		}

		/// <summary>
		/// Called when an error is matched
		/// </summary>
		/// <param name="Error">The matched error</param>
		public void OnErrorMatch(ErrorMatch Error)
		{
			lock(LockObject)
			{
				NewErrors.Add(Error);
			}

			UpdateEvent.Set();

			if (BackgroundThread == null)
			{
				BackgroundThread = new Thread(() => BackgroundWorker());
				BackgroundThread.Start();
			}
		}

		/// <summary>
		/// Updates EC properties in the background
		/// </summary>
		private void BackgroundWorker()
		{
			bool bHasSetDiagFile = false;
			int NumErrors = 0;
			int NumWarnings = 0;
			string Outcome = null;

			List<ErrorMatch> Errors = new List<ErrorMatch>();
			while (!bDisposing)
			{
				// Copy the current set of errors
				lock(LockObject)
				{
					if (NewErrors.Count > 0)
					{
						Errors.AddRange(NewErrors);
						NewErrors.Clear();
					}
					else if(bDisposing)
					{
						break;
					}
				}

				// Write them to disk
				Write(OutputFile, Errors);

				// On the first run, set the path to the diagnostics file
				if (!bHasSetDiagFile)
				{
					SetProperty("/myJobStep/diagFile", OutputFile.GetFileName());
					bHasSetDiagFile = true;
				}

				// Check if the number of errors has changed
				int NewNumErrors = Errors.Count(x => x.Severity == ErrorSeverity.Error);
				if (NewNumErrors > NumErrors)
				{
					SetProperty("/myJobStep/errors", NewNumErrors.ToString());
					NumErrors = NewNumErrors;
				}

				// Check if the number of warnings has changed
				int NewNumWarnings = Errors.Count(x => x.Severity == ErrorSeverity.Warning);
				if (NewNumWarnings > NumWarnings)
				{
					SetProperty("/myJobStep/warnings", NewNumWarnings.ToString());
					NumWarnings = NewNumWarnings;
				}

				// Check if the outcome has changed
				string NewOutcome = (NumErrors > 0) ? "error" : (NumWarnings > 0) ? "warning" : null;
				if (NewOutcome != Outcome)
				{
					SetProperty("/myJobStep/outcome", NewOutcome);
					Outcome = NewOutcome;
				}

				// Wait until the next update
				UpdateEvent.WaitOne();
			}
		}

		/// <summary>
		/// Sets an EC property 
		/// </summary>
		/// <param name="Command">The command to run</param>
		void SetProperty(string Name, string Value)
		{
			if (bUpdateProperties)
			{
				string Arguments = String.Format("setProperty \"{0}\" \"{1}\"", Name, Value);
				using (Process Process = Process.Start("ectool", Arguments))
				{
					Process.WaitForExit();

					if (Process.ExitCode != 0)
					{
						Log.TraceWarning("ectool {0} terminated with exit code {1}", Arguments, Process.ExitCode);
					}
				}
			}
		}

		/// <summary>
		/// Write the list of errors to disk in XML format
		/// </summary>
		static void Write(FileReference OutputFile, List<ErrorMatch> Errors)
		{
			byte[] XmlData;

			// Prepare the XML data that needs to be written
			XmlWriterSettings Settings = new XmlWriterSettings();
			Settings.NewLineChars = Environment.NewLine;
			Settings.Indent = true;
			Settings.IndentChars = "\t";
			using (MemoryStream Stream = new MemoryStream())
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

							if (Error.Severity == ErrorSeverity.Error)
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
							foreach (string Line in Error.Lines)
							{
								Message.AppendLine(Line);
							}
							Writer.WriteElementString("message", Message.ToString());

							Writer.WriteEndElement();
						}
					}
					Writer.WriteEndElement();
				}
				XmlData = Stream.ToArray();
			}

			// Open the existing file, write the new data, and truncate it.
			using (FileStream Stream = FileReference.Open(OutputFile, FileMode.OpenOrCreate, FileAccess.Write, FileShare.Read | FileShare.Delete))
			{
				Stream.Position = 0;
				Stream.Write(XmlData, 0, XmlData.Length);
				Stream.SetLength(Stream.Position);
			}
		}
	}
}
