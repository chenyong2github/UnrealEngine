// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using BuildAgent.Issues;
using BuildAgent.Run.Interfaces;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Serialization.Json;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent.Run.Listeners
{
	/// <summary>
	/// Issue listener. Generates an output report that can be consumed by the 
	/// </summary>
	class IssuesListener : IErrorListener
	{
		InputJob Job;
		InputJobStep JobStep;
		string LineUrl;
		FileReference OutputFile;

		public IssuesListener(string Stream, int Change, string JobName, string JobUrl, string JobStepName, string JobStepUrl, string LineUrl, string BaseDir, FileReference OutputFile)
		{
			this.OutputFile = OutputFile;
			this.LineUrl = LineUrl;

			JobStep = new InputJobStep();
			JobStep.Name = JobStepName;
			JobStep.Url = JobStepUrl;
			JobStep.BaseDirectory = BaseDir;

			Job = new InputJob();
			Job.Change = Change;
			Job.Stream = Stream;
			Job.Url = JobUrl;
			Job.Name = JobName;
			Job.Steps.Add(JobStep);
		}

		public void Dispose()
		{
			// Try to write the output file in a transactional way; write it to a temporary file and rename it.
			DirectoryReference.CreateDirectory(OutputFile.Directory);

			InputData Data = new InputData();
			Data.Jobs.Add(Job);

			FileReference TempOutputFile = new FileReference(OutputFile.FullName + ".incoming");
			using (MemoryStream Stream = new MemoryStream())
			{
				DataContractJsonSerializer InputFileDataSerializer = new DataContractJsonSerializer(typeof(InputData));
				InputFileDataSerializer.WriteObject(Stream, Data);
				FileReference.WriteAllBytes(TempOutputFile, Stream.ToArray());
			}

			FileReference.Delete(OutputFile);
			FileReference.Move(TempOutputFile, OutputFile);
		}

		public void OnErrorMatch(ErrorMatch Error)
		{
			if (Error.Severity == ErrorSeverity.Error)
			{
				// Find the longest sequence of spaces common to every non-empty line
				int WhitespaceLen = int.MaxValue;
				foreach (string Line in Error.Lines)
				{
					int ThisWhitespaceLen = 0;
					while (ThisWhitespaceLen < Line.Length && Line[ThisWhitespaceLen] == ' ')
					{
						ThisWhitespaceLen++;
					}
					if (ThisWhitespaceLen < Line.Length)
					{
						WhitespaceLen = Math.Min(WhitespaceLen, ThisWhitespaceLen);
					}
				}

				// Remove the whitespace prefix
				List<string> Lines = Error.Lines;
				if (WhitespaceLen < int.MaxValue)
				{
					Lines = new List<string>(Lines);
					for (int Idx = 0; Idx < Lines.Count; Idx++)
					{
						string Line = Lines[Idx];
						if (Line.Length > WhitespaceLen)
						{
							Line = Line.Substring(WhitespaceLen);
						}
						else
						{
							Line = String.Empty;
						}
						Lines[Idx] = Line;
					}
				}

				// Add the output diagnostic
				InputDiagnostic Diagnostic = new InputDiagnostic();
				Diagnostic.Type = "Error";
				Diagnostic.Message = String.Join("\n", Lines);
				if (LineUrl != null)
				{
					string Url = LineUrl;
					Url = Url.Replace("{LINE_START}", Error.MinLineNumber.ToString());
					Url = Url.Replace("{LINE_END}", Error.MaxLineNumber.ToString());
					Url = Url.Replace("{LINE_COUNT}", (Error.MaxLineNumber + 1 - Error.MinLineNumber).ToString());
					Diagnostic.Url = Url;
				}
				JobStep.Diagnostics.Add(Diagnostic);
			}
		}
	}
}
