// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Drawing;
using System.Windows.Forms;


namespace DatasmithRevitExporter
{
	public class DatasmithRevitExportOptions : Form
	{
		private CheckBox      WriteLogFile;
		private NumericUpDown LevelOfTessellation;

		public DatasmithRevitExportOptions()
		{
			ToolTip OptionToolTip = new ToolTip();
			OptionToolTip.AutoPopDelay = 10000; // milliseconds

			Label WriteLogFileLabel = new Label();
			WriteLogFileLabel.Name = "WriteLogFileLabel";
			WriteLogFileLabel.Text = "Write Log File";
			WriteLogFileLabel.Anchor = AnchorStyles.Left;
			WriteLogFileLabel.AutoSize = true;
			WriteLogFileLabel.Location = new Point(3, 3);
			WriteLogFileLabel.Size = new Size(72, 13);
			WriteLogFileLabel.TabIndex = 0;
			WriteLogFileLabel.TextAlign = ContentAlignment.MiddleLeft;

			WriteLogFile = new CheckBox();
			WriteLogFile.Name = "WriteLogFile";
			WriteLogFile.Anchor = AnchorStyles.Left;
			WriteLogFile.AutoSize = true;
			WriteLogFile.Location = new Point(113, 3);
			WriteLogFile.Size = new Size(15, 14);
			WriteLogFile.TabIndex = 1;
			WriteLogFile.UseVisualStyleBackColor = true;

			OptionToolTip.SetToolTip(WriteLogFile,
									 "Write a '.log' file aside the '.udatasmith' file.\n" +
									 "This log file records some details about the exported Revit elements.\n" +
									 "The log file can become quite big for complex 3D views.");

			Label LevelOfTessellationLabel = new Label();
			LevelOfTessellationLabel.Name = "LevelOfTessellationLabel";
			LevelOfTessellationLabel.Text = "Level of Tessellation";
			LevelOfTessellationLabel.Anchor = AnchorStyles.Left;
			LevelOfTessellationLabel.AutoSize = true;
			LevelOfTessellationLabel.Location = new Point(3, 26);
			LevelOfTessellationLabel.Size = new Size(104, 13);
			LevelOfTessellationLabel.TabIndex = 2;
			LevelOfTessellationLabel.TextAlign = ContentAlignment.MiddleLeft;

			LevelOfTessellation = new NumericUpDown();
			LevelOfTessellation.Name = "LevelOfTessellation";
			LevelOfTessellation.Minimum = -1;
			LevelOfTessellation.Maximum = 15;
			LevelOfTessellation.Value   = 8;
			LevelOfTessellation.Anchor = AnchorStyles.Left;
			LevelOfTessellation.AutoSize = true;
			LevelOfTessellation.Location = new Point(113, 23);
			LevelOfTessellation.Size = new Size(35, 20);
			LevelOfTessellation.TabIndex = 3;
			LevelOfTessellation.TextAlign = HorizontalAlignment.Right;

			OptionToolTip.SetToolTip(LevelOfTessellation,
				                     "-1 :\tRevit will use its default algorithm, which is based on output resolution.\n" +
									 "0 to 15 :\tRevit will use the suggested level of detail when tessellating faces.\n" +
									 "\tUsing a value close to the middle of the range yields a very reasonable tessellation.\n" +
									 "\tRevit uses level 8 as its 'normal' level of detail.");

			Button OKButton = new Button();
			OKButton.Name = "OKButton";
			OKButton.Text = "OK";
			OKButton.Anchor = ((AnchorStyles)((AnchorStyles.Bottom | AnchorStyles.Right)));
			OKButton.DialogResult = DialogResult.OK;
			OKButton.Location = new Point(211, 68);
			OKButton.Margin = new Padding(78, 13, 3, 3);
			OKButton.Size = new Size(75, 23);
			OKButton.TabIndex = 4;
			OKButton.UseVisualStyleBackColor = true;

			TableLayoutPanel DialogLayout = new TableLayoutPanel();
			DialogLayout.Name = "DialogLayout";
			DialogLayout.ColumnCount = 2;
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.RowCount = 3;
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.AutoSize = true;
			DialogLayout.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			DialogLayout.Dock = DockStyle.Fill;
			DialogLayout.Location = new Point(10, 10);
			DialogLayout.Size = new Size(300, 100);
			DialogLayout.TabIndex = 0;

			DialogLayout.Controls.Add(WriteLogFileLabel,        0, 0);
			DialogLayout.Controls.Add(WriteLogFile,             1, 0);
			DialogLayout.Controls.Add(LevelOfTessellationLabel, 0, 1);
			DialogLayout.Controls.Add(LevelOfTessellation,      1, 1);
			DialogLayout.Controls.Add(OKButton,                 1, 2);

			Name = "UnrealDatasmithExportOptions";
			Text = "Unreal Datasmith Export - Debug Options";
			AcceptButton = OKButton;
			AutoScaleDimensions = new SizeF(6F, 13F);
			AutoScaleMode = AutoScaleMode.Font;
			AutoSize = true;
			AutoSizeMode = AutoSizeMode.GrowAndShrink;
			ClientSize = new Size(309, 114);
			FormBorderStyle = FormBorderStyle.FixedDialog;
			MaximizeBox = false;
			MinimizeBox = false;
			Padding = new Padding(10);
			SizeGripStyle = SizeGripStyle.Hide;
			StartPosition = FormStartPosition.CenterParent;

			Controls.Add(DialogLayout);
		}

		public bool GetWriteLogFile()
		{
			return WriteLogFile.Checked;
		}

		public int GetLevelOfTessellation()
		{
			return Decimal.ToInt32(LevelOfTessellation.Value);
		}
	}
}
