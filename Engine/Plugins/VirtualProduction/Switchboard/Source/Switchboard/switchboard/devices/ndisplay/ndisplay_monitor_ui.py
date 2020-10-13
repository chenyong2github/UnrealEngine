# Copyright Epic Games, Inc. All Rights Reserved.

from PySide2 import QtWidgets, QtCore
from PySide2.QtWidgets import QHBoxLayout, QTableView, QSizePolicy, QHeaderView
from PySide2.QtCore import Qt
 
class nDisplayMonitorUI(QtWidgets.QWidget):

    def __init__(self, parent, monitor):
        QtWidgets.QWidget.__init__(self, parent)

        # create table

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

        # add table to layout

        layout = QHBoxLayout()
        layout.addWidget(self.tableview)

        self.setLayout(layout)

        

