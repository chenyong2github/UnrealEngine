# Copyright Epic Games, Inc. All Rights Reserved.

from PySide2 import QtWidgets, QtCore
from PySide2.QtWidgets import QHBoxLayout, QVBoxLayout, QTableView, QSizePolicy, QHeaderView, QPushButton, QSpacerItem
from PySide2.QtCore import Qt
 
class nDisplayMonitorUI(QtWidgets.QWidget):

    def __init__(self, parent, monitor):
        QtWidgets.QWidget.__init__(self, parent)

        # create buttons
        #
        self.btnForceFocus = QPushButton("Force Focus")
        self.btnFixExeFlags = QPushButton("Fix ExeFlags")

        self.btnForceFocus.clicked.connect(monitor.btnForceFocus_clicked)
        self.btnFixExeFlags.clicked.connect(monitor.btnFixExeFlags_clicked)

        # arrange them in a horizontal layout
        layout_buttons = QHBoxLayout()
        layout_buttons.addStretch()
        layout_buttons.addWidget(self.btnForceFocus)
        layout_buttons.addWidget(self.btnFixExeFlags)

        # create table
        #
        self.tableview = QTableView()
        self.tableview.setModel(monitor) # the monitor is the model of this tableview.

        size = QSizePolicy(QSizePolicy.Preferred, QSizePolicy.Preferred)
        size.setHorizontalStretch(1)
        self.tableview.setSizePolicy(size)

        # configure resize modes on headers
        self.tableview.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeToContents)
        self.tableview.horizontalHeader().setStretchLastSection(False)
        self.tableview.verticalHeader().setSectionResizeMode(QHeaderView.ResizeToContents)
        self.tableview.verticalHeader().setVisible(False)

        # create layout
        #
        layout = QVBoxLayout()

        layout.addLayout(layout_buttons)
        layout.addWidget(self.tableview)

        self.setLayout(layout)

        

