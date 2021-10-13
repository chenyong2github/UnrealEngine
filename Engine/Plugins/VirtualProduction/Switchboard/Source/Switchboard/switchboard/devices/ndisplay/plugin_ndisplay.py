# Copyright Epic Games, Inc. All Rights Reserved.

import concurrent.futures
import json
import os
from pathlib import Path
import socket
import struct
import traceback
from typing import List, Optional, Tuple

from PySide2 import QtCore
from PySide2 import QtWidgets

from switchboard import message_protocol
from switchboard import switchboard_utils as sb_utils
from switchboard import switchboard_widgets as sb_widgets
from switchboard.config import CONFIG, BoolSetting, FilePathSetting, \
    LoggingSetting, OptionSetting, Setting, StringSetting, SETTINGS
from switchboard.devices.device_widget_base import AddDeviceDialog
from switchboard.devices.unreal.plugin_unreal import DeviceUnreal, \
    DeviceWidgetUnreal
from switchboard.devices.unreal.uassetparser import UassetParser
from switchboard.switchboard_logging import LOGGER

from .ndisplay_monitor_ui import nDisplayMonitorUI
from .ndisplay_monitor import nDisplayMonitor


class AddnDisplayDialog(AddDeviceDialog):
    def __init__(self, existing_devices, parent=None):
        super().__init__(
            device_type="nDisplay", existing_devices=existing_devices,
            parent=parent)

        # Set enough width for a decent file path length
        self.setMinimumWidth(640)

        # remove unneded base dialog layout rows
        self.form_layout.removeRow(self.name_field)
        self.form_layout.removeRow(self.ip_field)
        self.name_field = None
        self.ip_field = None

        # Button to browse for supported config files
        self.btnBrowse = QtWidgets.QPushButton(self, text="Browse")
        self.btnBrowse.clicked.connect(self.on_clicked_btnBrowse)

        # Button to find and populate config files in the UE project
        self.btnFindConfigs = QtWidgets.QPushButton(self, text="Populate")
        self.btnFindConfigs.clicked.connect(self.on_clicked_btnFindConfigs)

        # Combobox with config files
        self.cbConfigs = QtWidgets.QComboBox(self)
        self.cbConfigs.setSizePolicy(
            QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Preferred)
        self.cbConfigs.setEditable(True)  # to allow the user to type the value

        # Create layout for the config file selection widgets
        file_selection_layout = QtWidgets.QHBoxLayout()
        file_selection_layout.addWidget(self.cbConfigs)
        file_selection_layout.addWidget(self.btnBrowse)
        file_selection_layout.addWidget(self.btnFindConfigs)

        self.form_layout.addRow("Config File", file_selection_layout)

        # Add a spacer right before the ok/cancel buttons
        spacer_layout = QtWidgets.QHBoxLayout()
        spacer_layout.addItem(
            QtWidgets.QSpacerItem(
                20, 20, QtWidgets.QSizePolicy.Minimum,
                QtWidgets.QSizePolicy.Expanding))
        self.form_layout.addRow("", spacer_layout)

        # Find existing nDisplay devices in order to issue a warning about
        # replacing them.
        self.existing_ndisplay_devices = []

        for device in existing_devices:
            if device.device_type == "nDisplay":
                self.existing_ndisplay_devices.append(device)

        if self.existing_ndisplay_devices:
            self.layout().addWidget(
                QtWidgets.QLabel(
                    "Warning! All existing nDisplay devices will be "
                    "replaced."))

        # populate the config combobox with the items last populated.
        self.recall_config_itemDatas()

    def recall_config_itemDatas(self):
        '''
        Populate the config combobox with the already found configs, avoiding
        having to re-find every time.
        '''
        self.cbConfigs.clear()

        itemDatas = DevicenDisplay.csettings[
            'populated_config_itemDatas'].get_value()

        try:
            for itemData in itemDatas:
                self.cbConfigs.addItem(itemData['name'], itemData)
        except Exception:
            LOGGER.error('Error recalling config itemDatas')

    def current_config_path(self):
        ''' Get currently selected config path in the combobox '''

        # Detect if this is a manual entry using the itemData

        itemText = self.cbConfigs.currentText()
        itemData = self.cbConfigs.currentData()

        if (self.cbConfigs.currentData() is None or
                itemData['name'] != itemText):
            config_path = itemText.replace('"', '').strip()
        else:
            config_path = itemData['path'].replace('"', '').strip()

        # normalize the path
        config_path = os.path.normpath(config_path)
        return config_path

    def result(self):
        res = super().result()
        if res == QtWidgets.QDialog.Accepted:
            config_path = self.current_config_path()
            DevicenDisplay.csettings['ndisplay_config_file'].update_value(
                config_path)

        return res

    def on_clicked_btnBrowse(self):
        ''' Opens a file dialog to browse for the config file
        '''
        start_path = str(Path.home())

        if (SETTINGS.LAST_BROWSED_PATH and
                os.path.exists(SETTINGS.LAST_BROWSED_PATH)):
            start_path = SETTINGS.LAST_BROWSED_PATH

        cfg_path, _ = QtWidgets.QFileDialog.getOpenFileName(
            self, "Select nDisplay config file", start_path,
            "nDisplay Config (*.ndisplay;*.uasset)")

        if len(cfg_path) > 0 and os.path.exists(cfg_path):
            self.cbConfigs.setCurrentText(cfg_path)
            SETTINGS.LAST_BROWSED_PATH = os.path.dirname(cfg_path)
            SETTINGS.save()

    def on_clicked_btnFindConfigs(self):
        ''' Finds and populates config combobox '''
        # We will look for config files in the project's Content folder
        configs_path = os.path.normpath(
            os.path.join(
                os.path.dirname(
                    CONFIG.UPROJECT_PATH.get_value().replace('"', '')),
                'Content'))

        config_names = []
        config_paths = []

        assets = []

        for dirpath, _, files in os.walk(configs_path):
            for name in files:
                if not name.lower().endswith(('.uasset', '.ndisplay')):
                    continue

                if name not in config_names:
                    config_path = os.path.join(dirpath, name)
                    ext = os.path.splitext(name)[1]

                    # Since .uasset is generic a asset container, only add
                    # assets of the right config class.
                    if ext.lower() == '.uasset':
                        assets.append({'name': name, 'path': config_path})
                    else:
                        config_names.append(name)
                        config_paths.append(config_path)

        # process the assets in a multi-threaded fashion

        # show a progress bar if it is taking more a trivial amount of time
        progressDiag = QtWidgets.QProgressDialog(
            'Parsing assets...', 'Cancel', 0, 0, parent=self)
        progressDiag.setWindowTitle('nDisplay Config Finder')
        progressDiag.setModal(True)
        progressDiag.setMinimumDuration(1000)  # time before it shows up
        progressDiag.setRange(0, len(assets))
        progressDiag.setCancelButton(None)
        # Looks much better without the window frame.
        progressDiag.setWindowFlag(QtCore.Qt.FramelessWindowHint)

        def validateConfigAsset(asset):
            ''' Returns the asset if it is an nDisplay config '''
            with open(asset['path'], 'rb') as file:
                aparser = UassetParser(file, allowUnversioned=True)
                for assetdata in aparser.aregdata:
                    if assetdata.ObjectClassName == 'DisplayClusterBlueprint':
                        return asset
            raise ValueError

        numThreads = 8
        doneAssetCount = 0

        with concurrent.futures.ThreadPoolExecutor(
                max_workers=numThreads) as executor:
            futures = [
                executor.submit(validateConfigAsset, asset)
                for asset in assets]

            for future in concurrent.futures.as_completed(futures):

                # Update progress bar.
                doneAssetCount += 1
                progressDiag.setValue(doneAssetCount)

                # Get the future result and add to list of config names and
                # paths.
                try:
                    asset = future.result()
                    if asset['name'] not in config_names:
                        config_names.append(asset['name'])
                        config_paths.append(asset['path'])
                except Exception:
                    pass

        # close progress bar window
        progressDiag.close()

        # collect the found config files into the itemDatas list

        itemDatas = []

        for idx, config_name in enumerate(config_names):
            itemData = {'name': config_name, 'path': config_paths[idx]}
            itemDatas.append(itemData)

        # sort by name
        itemDatas.sort(key=lambda itemData: itemData['name'])

        # update settings that should survive device removal and addition
        DevicenDisplay.csettings['populated_config_itemDatas'].update_value(
            itemDatas)

        # update the combo box with the items.
        self.recall_config_itemDatas()

    def devices_to_add(self):
        cfg_file = self.current_config_path()
        try:
            (devices, _) = DevicenDisplay.parse_config(cfg_file)

            if len(devices) == 0:
                LOGGER.error(
                    "Could not read any devices from nDisplay config file "
                    f"{cfg_file}")

            # Initialize from the config our setting that identifies which
            # device is the master.
            for node in devices:
                if node['master']:
                    DevicenDisplay.csettings[
                        'master_device_name'].update_value(node['name'])
                    break

            return devices

        except (IndexError, KeyError):
            LOGGER.error(f"Error parsing nDisplay config file {cfg_file}")
            return []

    def devices_to_remove(self):
        return self.existing_ndisplay_devices


class DeviceWidgetnDisplay(DeviceWidgetUnreal):

    signal_device_widget_master = QtCore.Signal(object)

    def add_widget_to_layout(self, widget):
        ''' DeviceWidget base class method override. '''

        if widget == self.name_line_edit:
            self.add_master_button()

            # shorten the widget to account for the inserted master button
            btn_added_width = (
                max(
                    self.master_button.iconSize().width(),
                    self.master_button.minimumSize().width())
                + 2 * self.layout.spacing()
                + self.master_button.contentsMargins().left()
                + self.master_button.contentsMargins().right())

            le_maxwidth = self.name_line_edit.maximumWidth() - btn_added_width
            self.name_line_edit.setMaximumWidth(le_maxwidth)

        super().add_widget_to_layout(widget)

    def add_master_button(self):
        '''
        Adds to the layout a button to select which device should be the
        master device in the cluster.
        '''

        self.master_button = sb_widgets.ControlQPushButton.create(
            ':/icons/images/star_yellow_off.png',
            icon_disabled=':/icons/images/star_yellow_off.png',
            icon_hover=':/icons/images/star_yellow_off.png',
            icon_disabled_on=':/icons/images/star_yellow_off.png',
            icon_on=':/icons/images/star_yellow.png',
            icon_hover_on=':/icons/images/star_yellow.png',
            icon_size=QtCore.QSize(16, 16),
            tool_tip='Select as master node'
        )

        self.add_widget_to_layout(self.master_button)

        self.master_button.clicked.connect(self.on_master_button_clicked)

    def on_master_button_clicked(self):
        ''' Called when master_button is clicked '''

        self.signal_device_widget_master.emit(self)


class DevicenDisplay(DeviceUnreal):

    add_device_dialog = AddnDisplayDialog

    csettings = {
        'ndisplay_config_file': FilePathSetting(
            attr_name="ndisplay_cfg_file",
            nice_name="nDisplay Config File",
            value="",
            file_path_filter="nDisplay Config (*.ndisplay;*.uasset)",
            tool_tip="Path to nDisplay config file"
        ),
        'use_all_available_cores': BoolSetting(
            attr_name="use_all_available_cores",
            nice_name="Use All Available Cores",
            value=False,
        ),
        'texture_streaming': BoolSetting(
            attr_name="texture_streaming",
            nice_name="Texture Streaming",
            value=True,
        ),
        'render_api': OptionSetting(
            attr_name="render_api",
            nice_name="Render API",
            value="dx12",
            possible_values=["dx11", "dx12"],
        ),
        'render_mode': OptionSetting(
            attr_name="render_mode",
            nice_name="Render Mode",
            value="Mono",
            possible_values=[
                "Mono", "Frame sequential", "Side-by-Side", "Top-bottom"]
        ),
        'executable_filename': FilePathSetting(
            attr_name="executable_filename",
            nice_name="nDisplay Executable Filename",
            value="UE4Editor.exe",
            file_path_filter="Programs (*.exe;*.bat)"
        ),
        'ndisplay_cmd_args': StringSetting(
            attr_name="ndisplay_cmd_args",
            nice_name="Extra Cmd Line Args",
            value="",
        ),
        'ndisplay_exec_cmds': StringSetting(
            attr_name="ndisplay_exec_cmds",
            nice_name='ExecCmds',
            value="",
            tool_tip='ExecCmds to be passed. No need for outer double quotes.',
        ),
        'ndisplay_dp_cvars': StringSetting(
            attr_name='ndisplay_dp_cvars',
            nice_name="DPCVars",
            value='',
            tool_tip="Device profile console variables (comma separated)."
        ),
        'ndisplay_unattended': BoolSetting(
            attr_name='ndisplay_unattended',
            nice_name='Unattended',
            value=True,
            tool_tip=(
                'Include the "-unattended" command line argument, which is '
                'documented to "Disable anything requiring feedback from the '
                'user."'),
        ),
        'max_gpu_count': OptionSetting(
            attr_name="max_gpu_count",
            nice_name="Number of GPUs",
            value=2,
            possible_values=list(range(1, 17)),
            tool_tip=(
                "If you have multiple GPUs in the PC, you can specify how "
                "many to use."),
        ),
        'priority_modifier': OptionSetting(
            attr_name='priority_modifier',
            nice_name="Process Priority",
            value=sb_utils.PriorityModifier.Normal.name,
            possible_values=[p.name for p in sb_utils.PriorityModifier],
            tool_tip="Used to override the priority of the process.",
        ),
        'populated_config_itemDatas': Setting(
            attr_name='populated_config_itemDatas',
            nice_name="Populated Config Files",
            value=[],  # populated by AddnDisplayDialog
            tool_tip="Remember the last populated list of config files",
            show_ui=False,
        ),
        'minimize_before_launch': BoolSetting(
            attr_name='minimize_before_launch',
            nice_name="Minimize Before Launch",
            value=True,
            tool_tip="Minimizes windows before launch"
        ),
        'master_device_name': StringSetting(
            attr_name='master_device_name',
            nice_name="Master Device",
            value='',
            tool_tip=(
                "Identifies which nDisplay device should be the master in the "
                "cluster"),
            show_ui=False,
        ),
        'logging': LoggingSetting(
            attr_name='logging',
            nice_name='Logging',
            value=None,
            # Extracted from:
            #   Engine\Plugins\Runtime\nDisplay\Source\DisplayCluster\
            #   Private\Misc\DisplayClusterLog.cpp
            categories=[
                'LogDisplayClusterEngine',
                'LogDisplayClusterModule',
                'LogDisplayClusterCluster',
                'LogDisplayClusterConfig',
                'LogDisplayClusterGame',
                'LogDisplayClusterNetwork',
                'LogDisplayClusterNetworkMsg',
                'LogDisplayClusterRender',
                'LogDisplayClusterRenderSync',
                'LogDisplayClusterViewport',
                'LogDisplayClusterBlueprint'
            ],
            tool_tip='Logging categories and verbosity levels'
        ),
        'udpmessaging_unicast_endpoint': StringSetting(
            attr_name='udpmessaging_unicast_endpoint',
            nice_name='Unicast Endpoint',
            value=':0',
            tool_tip=(
                'Local interface binding (-UDPMESSAGING_TRANSPORT_UNICAST) of '
                'the form {ip}:{port}. If {ip} is omitted, the device IP '
                'address is used.'),
        ),
        'udpmessaging_extra_static_endpoints': StringSetting(
            attr_name='udpmessaging_extra_static_endpoints',
            nice_name='Extra Static Endpoints',
            value='',
            tool_tip=(
                'Comma separated. Used to add static endpoints '
                '(-UDPMESSAGING_TRANSPORT_STATIC) in addition to those '
                'managed by Switchboard.'),
        ),
        'disable_ensures': BoolSetting(
            attr_name='disable_ensures',
            nice_name="Disable Ensures",
            value=True,
            tool_tip="When checked, disables the handling of ensure errors - which are non-fatal and may cause hitches."
        ),
    }

    ndisplay_monitor_ui = None
    ndisplay_monitor = None

    def __init__(self, name, ip_address, **kwargs):
        super().__init__(name, ip_address, **kwargs)

        self.settings = {
            'ue_command_line': StringSetting(
                attr_name="ue_command_line",
                nice_name="UE Command Line",
                value=kwargs.get("ue_command_line", '')
            ),
            'window_position': Setting(
                attr_name="window_position",
                nice_name="Window Position",
                value=tuple(kwargs.get("window_position", (0, 0))),
                show_ui=False
            ),
            'window_resolution': Setting(
                attr_name="window_resolution",
                nice_name="Window Resolution",
                value=tuple(kwargs.get("window_resolution", (100, 100))),
                show_ui=False
            ),
            'fullscreen': BoolSetting(
                attr_name="fullscreen",
                nice_name="fullscreen",
                value=kwargs.get("fullscreen", False),
                show_ui=False
            ),
        }

        self.render_mode_cmdline_opts = {
            "Mono": "-dc_dev_mono",
            "Frame sequential": "-quad_buffer_stereo",
            "Side-by-Side": "-dc_dev_side_by_side",
            "Top-bottom": "-dc_dev_top_bottom"
        }

        self.pending_transfer_cfg = False
        self.pending_transfer_uasset = False
        self.bp_object_path: Optional[str] = None
        self.path_to_config_on_host = DevicenDisplay.csettings[
            'ndisplay_config_file'].get_value()

        if len(self.settings['ue_command_line'].get_value()) == 0:
            self.generate_unreal_command_line()

        CONFIG.ENGINE_DIR.signal_setting_overridden.connect(
            self.on_cmdline_affecting_override)
        CONFIG.ENGINE_DIR.signal_setting_changed.connect(
            self.on_change_setting_affecting_command_line)
        CONFIG.UPROJECT_PATH.signal_setting_overridden.connect(
            self.on_cmdline_affecting_override)
        CONFIG.UPROJECT_PATH.signal_setting_changed.connect(
            self.on_change_setting_affecting_command_line)

        # common settings affect instance command line
        for csetting in self.__class__.plugin_settings():
            csetting.signal_setting_changed.connect(
                self.on_change_setting_affecting_command_line)

        self.unreal_client.delegates[
            'send file complete'] = self.on_send_file_complete
        self.unreal_client.delegates[
            'get sync status'] = self.on_get_sync_status
        self.unreal_client.delegates[
            'refresh mosaics'] = self.on_refresh_mosaics

        # create monitor if it doesn't exist
        self.__class__.create_monitor_if_necessary()

        # node configuration (updated from config file)
        self.nodeconfig = {}

        try:
            cfg_file = DevicenDisplay.csettings[
                'ndisplay_config_file'].get_value(self.name)
            self.update_settings_controlled_by_config(cfg_file)
        except Exception:
            LOGGER.error(
                f"{self.name}: Could not update from '{cfg_file}' during "
                "initialization. \n\n=== Traceback BEGIN ===\n"
                f"{traceback.format_exc()}=== Traceback END ===\n")

    @classmethod
    def create_monitor_if_necessary(cls):
        ''' Creates the nDisplay Monitor if it doesn't exist yet.
        '''
        if not cls.ndisplay_monitor:
            cls.ndisplay_monitor = nDisplayMonitor(None)

        return cls.ndisplay_monitor

    def on_change_setting_affecting_command_line(self, oldval, newval):
        self.generate_unreal_command_line()

    @classmethod
    def plugin_settings(cls):
        ''' Returns common settings that belong to all devices of this class.
        '''
        return list(cls.csettings.values()) + [
            DeviceUnreal.csettings['port'],
            DeviceUnreal.csettings['roles_filename'],
            DeviceUnreal.csettings['stage_session_id'],
        ]

    def device_settings(self):
        '''
        This is given to the config, so that it knows to save them when they
        change. settings_dialog.py will try to create a UI for each setting if
        setting.show_ui is True.
        '''
        return super().device_settings() + list(self.settings.values())

    def setting_overrides(self):
        return [
            DevicenDisplay.csettings['ndisplay_cmd_args'],
            DevicenDisplay.csettings['ndisplay_exec_cmds'],
            DevicenDisplay.csettings['ndisplay_dp_cvars'],
            DevicenDisplay.csettings['max_gpu_count'],
            DevicenDisplay.csettings['priority_modifier'],
            DevicenDisplay.csettings['udpmessaging_unicast_endpoint'],
            DevicenDisplay.csettings['udpmessaging_extra_static_endpoints'],
            CONFIG.ENGINE_DIR,
            CONFIG.SOURCE_CONTROL_WORKSPACE,
            CONFIG.UPROJECT_PATH,
        ]

    def on_cmdline_affecting_override(self, device_name):
        if self.name == device_name:
            self.generate_unreal_command_line()

    @property
    def is_recording_device(self):
        return False

    # Override these properties from the DeviceUnreal base class to use the
    # nDisplay-specific settings.
    @property
    def executable_filename(self):
        return DevicenDisplay.csettings["executable_filename"].get_value()

    @property
    def extra_cmdline_args_setting(self) -> str:
        return DevicenDisplay.csettings['ndisplay_cmd_args'].get_value(
            self.name)

    @property
    def udpmessaging_unicast_endpoint_setting(self) -> str:
        return DevicenDisplay.csettings[
            'udpmessaging_unicast_endpoint'].get_value(self.name)

    @property
    def udpmessaging_extra_static_endpoints_setting(self) -> str:
        return DevicenDisplay.csettings[
            'udpmessaging_extra_static_endpoints'].get_value(self.name)

    def generate_unreal_command_line(self, map_name=""):
        uproject = os.path.normpath(CONFIG.UPROJECT_PATH.get_value(self.name))

        # Device profile CVars
        dp_cvars = str(
            self.csettings['ndisplay_dp_cvars'].get_value(self.name)).strip()
        dp_cvars = f'-DPCVars="{dp_cvars}"' if len(dp_cvars) else ''

        # Extra arguments specified in settings
        additional_args = self.extra_cmdline_args_setting

        # Path to config file
        cfg_file = self.path_to_config_on_host

        # nDisplay window info
        win_pos = self.settings['window_position'].get_value()
        win_res = self.settings['window_resolution'].get_value()
        fullscreen = self.settings['fullscreen'].get_value()

        # Misc settings
        render_mode = self.render_mode_cmdline_opts[
            DevicenDisplay.csettings['render_mode'].get_value(self.name)]
        render_api = DevicenDisplay.csettings['render_api'].get_value(
            self.name)
        render_api = f"-{render_api}"
        use_all_cores = (
            "-useallavailablecores"
            if DevicenDisplay.csettings['use_all_available_cores'].get_value(
                self.name)
            else "")
        no_texture_streaming = (
            "-notexturestreaming"
            if not DevicenDisplay.csettings['texture_streaming'].get_value(
                self.name)
            else "")

        # MaxGPUCount (mGPU)
        max_gpu_count = DevicenDisplay.csettings["max_gpu_count"].get_value(
            self.name)
        try:
            max_gpu_count = (
                f"-MaxGPUCount={max_gpu_count}"
                if int(max_gpu_count) > 1 else '')
        except ValueError:
            LOGGER.warning(f"Invalid Number of GPUs '{max_gpu_count}'")
            max_gpu_count = ''

        # Overridden classes at runtime
        ini_engine = (
            "-ini:Engine:"
            "[/Script/Engine.Engine]:"
            "GameEngine=/Script/DisplayCluster.DisplayClusterGameEngine"
            ",[/Script/Engine.Engine]:"
            "GameViewportClientClassName="
            "/Script/DisplayCluster.DisplayClusterViewportClient"
            ",[/Script/Engine.UserInterfaceSettings]:"
            "bAllowHighDPIInGameMode=True")

        ini_game = (
            "-ini:Game:"
            "[/Script/EngineSettings.GeneralProjectSettings]:"
            "bUseBorderlessWindow=True")

        # VP roles
        vproles, missing_roles = self.get_vproles()

        if missing_roles:
            LOGGER.error(
                f"{self.name}: Omitted roles not in the remote roles ini "
                "file which would cause UE to fail at launch: "
                f"{'|'.join(missing_roles)}")

        vproles = '-VPRole=' + '|'.join(vproles) if vproles else ''

        # Session ID
        session_id = DeviceUnreal.csettings["stage_session_id"].get_value(
            self.name)
        session_id = f"-StageSessionId={session_id}" if session_id > 0 else ''

        # Friendly name. Avoid spaces to avoid parsing issues.
        friendly_name = f'-StageFriendlyName={self.name.replace(" ", "_")}'

        # Unattended mode
        unattended = (
            '-unattended'
            if DevicenDisplay.csettings['ndisplay_unattended'].get_value()
            else '')

        # UdpMessaging endpoints
        udpm_transport_multi = ''
        if self.udpmessaging_multicast_endpoint:
            udpm_transport_multi = (
                '-UDPMESSAGING_TRANSPORT_MULTICAST='
                f'"{self.udpmessaging_multicast_endpoint}"'
            )

        udpm_transport_unicast = ''
        if self.udpmessaging_unicast_endpoint:
            udpm_transport_unicast = (
                '-UDPMESSAGING_TRANSPORT_UNICAST='
                f'\"{self.udpmessaging_unicast_endpoint}\"')

        udpm_transport_static = ''
        static_endpoints = self.build_udpmessaging_static_endpoint_list()
        if len(static_endpoints) > 0:
            udpm_transport_static = (
                '-UDPMESSAGING_TRANSPORT_STATIC='
                f'\"{",".join(static_endpoints)}\"')

        # Disable ensures
        disable_ensures = (
            '-handleensurepercent=0'
            if DevicenDisplay.csettings['disable_ensures'].get_value()
            else '')

        # fill in fixed arguments
        args = [
            f'"{uproject}"',
            f'{map_name}',                # map to open
            "-game",                      # render nodes run in -game
            "-messaging",                 # enables messaging, needed for
                                          # MultiUser
            "-dc_cluster",                # this is a cluster node
            "-nosplash",                  # avoids splash screen
            "-fixedseed",                 # for determinism
            "-NoVerifyGC",                # improves performance
            "-noxrstereo",                # avoids a conflict with steam/oculus
            "-xrtrackingonly",            # allows multi-UE SteamVR for
                                          # trackers (but disallows sending
                                          # frames)
            "-RemoteControlIsHeadless",   # avoids notification window when
                                          # using RemoteControlWebUI
            f'{additional_args}',         # specified in settings
            f'{vproles}',                 # VP roles for this instance
            f'{friendly_name}',           # Stage Friendly Name
            f'{session_id}',              # Session ID.
            f'{max_gpu_count}',           # Max GPU count (mGPU)
            f'{dp_cvars}',                # Device profile CVars
            f'-dc_cfg="{cfg_file}"',      # nDisplay config file
            f'{render_api}',              # dx11/12
            f'{render_mode}',             # mono/...
            f'{use_all_cores}',           # -useallavailablecores
            f'{no_texture_streaming}',    # -notexturestreaming
            f'-dc_node={self.name}',      # name of this node in the nDisplay
                                          # cluster
            f'Log={self.log_filename}',   # log file
            f'{ini_engine}',              # Engine ini injections
            f'{ini_game}',                # Game ini injections
            f'{unattended}',              # -unattended
            f'{disable_ensures}',         # -handleensurepercent=0
            f'{udpm_transport_multi}',    # -UDPMESSAGING_TRANSPORT_MULTICAST=
            f'{udpm_transport_unicast}',  # -UDPMESSAGING_TRANSPORT_UNICAST=
            f'{udpm_transport_static}',   # -UDPMESSAGING_TRANSPORT_STATIC=
        ]

        # fill in ExecCmds
        exec_cmds = str(
            self.csettings["ndisplay_exec_cmds"].get_value(
                self.name)).strip().split(',')
        exec_cmds.append('DisableAllScreenMessages')
        exec_cmds = [cmd for cmd in exec_cmds if len(cmd.strip())]

        if len(exec_cmds):
            exec_cmds_expanded = ','.join(exec_cmds)
            args.append(f'-ExecCmds="{exec_cmds_expanded}"')

        # when in fullscreen, the window parameters should not be passed
        if fullscreen:
            args.extend([
                '-fullscreen',
            ])
        else:
            args.extend([
                '-windowed',
                '-forceres',
                f'WinX={win_pos[0]}',
                f'WinY={win_pos[1]}',
                f'ResX={win_res[0]}',
                f'ResY={win_res[1]}',
            ])

        # MultiUser parameters
        if CONFIG.MUSERVER_AUTO_JOIN:
            args.extend([
                '-CONCERTRETRYAUTOCONNECTONERROR',
                '-CONCERTAUTOCONNECT',
                f'-CONCERTSERVER={CONFIG.MUSERVER_SERVER_NAME}',
                f'-CONCERTSESSION={SETTINGS.MUSERVER_SESSION_NAME}',
                f'-CONCERTDISPLAYNAME={self.name}',
                '-CONCERTISHEADLESS',
            ])

        args.append(self.csettings['logging'].get_command_line_arg(
            override_device_name=self.name))

        path_to_exe = self.generate_unreal_exe_path()
        args_expanded = ' '.join(args)
        self.settings['ue_command_line'].update_value(
            f"{path_to_exe} {args_expanded}")

        return path_to_exe, args_expanded

    def on_send_file_complete(self, message):
        try:
            destination = message['destination']
            succeeded = message['bAck']
            error = message.get('error')
        except KeyError:
            LOGGER.error(
                f'Error parsing "send file complete" response ({message})')
            return

        ext = os.path.splitext(destination)[1].lower()

        if (ext == '.uasset') and self.pending_transfer_uasset:
            self.pending_transfer_uasset = False
            if succeeded:
                LOGGER.info(
                    f"{self.name}: nDisplay uasset successfully transferred "
                    f"to {destination} on host")
            else:
                LOGGER.error(
                    f"{self.name}: nDisplay uasset transfer failed: {error}")
        elif (ext == '.ndisplay') and self.pending_transfer_cfg:
            self.pending_transfer_cfg = False
            self.path_to_config_on_host = destination
            if succeeded:
                LOGGER.info(
                    f"{self.name}: nDisplay config file successfully "
                    f"transferred to {destination} on host")
            else:
                LOGGER.error(
                    f"{self.name}: nDisplay config file transfer "
                    f"failed: {error}")
        else:
            LOGGER.error(
                f"{self.name}: Unexpected send file completion "
                f"for {destination}")
            return

        if not (self.pending_transfer_cfg or self.pending_transfer_uasset):

            if DevicenDisplay.csettings['minimize_before_launch'].get_value(
                    True):
                self.minimize_windows()

            super().launch(map_name=self.map_name_to_launch)

    def on_get_sync_status(self, message):
        ''' Called when 'get sync status' is received. '''
        self.__class__.ndisplay_monitor.on_get_sync_status(
            device=self, message=message)

    def refresh_mosaics(self):
        ''' Request that the listener update its cached mosaic topologies. '''
        if self.unreal_client.is_connected:
            _, msg = message_protocol.create_refresh_mosaics_message()
            self.unreal_client.send_message(msg)

    def on_refresh_mosaics(self, message):
        try:
            if message['bAck'] is True:
                return
            elif message['error'] == 'Duplicate':
                LOGGER.warning('Duplicate "refresh mosaics" command ignored')
            else:
                LOGGER.error(f'"refresh mosaics" command rejected ({message})')
        except KeyError:
            LOGGER.error(
                f'Error parsing "refresh mosaics" response ({message})')

    def device_widget_registered(self, device_widget):
        ''' Device interface method '''

        super().device_widget_registered(device_widget)

        # hook to its master_button clicked event
        device_widget.signal_device_widget_master.connect(
            self.select_as_master)

        # Update the state of the button depending on whether this device is
        # the master or not.
        is_master = (
            self.name ==
            DevicenDisplay.csettings['master_device_name'].get_value())
        device_widget.master_button.setChecked(is_master)

    def select_as_master(self):
        ''' Selects this node as the master in the nDisplay cluster '''
        self.__class__.select_device_as_master(self)

    @classmethod
    def extract_configexport_from_uasset(cls, cfg_file) -> str:
        ''' Extract the configexport from the config uasset '''

        # Initialize uasset parser
        with open(cfg_file, 'rb') as file:
            aparser = UassetParser(file)

            # Check that the asset is of the right class, then find its
            # ConfigExport tag and parse it.
            for assetdata in aparser.aregdata:
                if assetdata.ObjectClassName == 'DisplayClusterBlueprint':
                    return assetdata.tags['ConfigExport']

        raise ValueError('Invalid nDisplay config .uasset')

    @classmethod
    def select_device_as_master(cls, device):
        ''' Selects the given devices as the master of the nDisplay cluster'''

        DevicenDisplay.csettings['master_device_name'].update_value(
            device.name)

        devices = [
            dev for dev in cls.active_unreal_devices
            if dev.device_type == 'nDisplay']

        for dev in devices:
            if dev.widget:
                checked = (dev.name == device.name)
                dev.widget.master_button.setChecked(checked)

    @classmethod
    def apply_local_overrides_to_config(cls, cfg_content, cfg_ext):
        ''' Applies supported local settings overrides to configuration '''

        if cfg_ext.lower() != '.ndisplay':
            return cfg_content

        data = json.loads(cfg_content)

        nodes = data['nDisplay']['cluster']['nodes']
        masternodeid = data['nDisplay']['cluster']['masterNode']['id']

        activenodes = {}

        devices = [
            dev for dev in cls.active_unreal_devices
            if dev.device_type == 'nDisplay']

        for device in devices:

            # We exclude devices by disconnecting from them
            if device.is_disconnected:
                continue

            # Match local node with config by name
            node = nodes.get(device.name, None)

            # No name match, no node. Renaming nodes in Switchboard is not
            # currently supported.
            if node is None:
                LOGGER.warning(
                    f'Skipped "{device.name}" because it did not match any '
                    'node in the nDisplay configuration')
                continue

            # override ip address
            node['host'] = device.ip_address

            # add to active nodes
            activenodes[device.name] = node

            # override master node by name
            if (device.name ==
                    DevicenDisplay.csettings[
                        'master_device_name'].get_value()):
                masternodeid = device.name

        data['nDisplay']['cluster']['nodes'] = activenodes
        data['nDisplay']['cluster']['masterNode']['id'] = masternodeid

        return json.dumps(data)

    @classmethod
    def parse_config_uasset(cls, cfg_file):
        ''' Parses nDisplay config file of type uasset '''
        jsstr = cls.extract_configexport_from_uasset(cfg_file)
        return cls.parse_config_json_string(jsstr)

    @classmethod
    def parse_config_json_string(cls, jsstr) -> Tuple[List, Optional[str]]:
        '''
        Parses nDisplay config JSON string, returning (nodes, uasset_path).
        '''

        js = json.loads(jsstr)

        nodes = []
        cnodes = js['nDisplay']['cluster']['nodes']
        masterNode = js['nDisplay']['cluster']['masterNode']
        uasset_path = js['nDisplay'].get('assetPath')

        for name, cnode in cnodes.items():
            kwargs = {"ue_command_line": ""}

            winx = int(cnode['window'].get('x', 0))
            winy = int(cnode['window'].get('y', 0))
            resx = int(cnode['window'].get('w', 0))
            resy = int(cnode['window'].get('h', 0))

            kwargs["window_position"] = (winx, winy)
            kwargs["window_resolution"] = (resx, resy)
            # Note the capital 'S'.
            kwargs["fullscreen"] = bool(cnode.get('fullScreen', False))

            master = True if masterNode['id'] == name else False

            nodes.append({
                "name": name,
                "ip_address": cnode['host'],
                "master": master,
                "port_ce": int(masterNode['ports']['ClusterEventsJson']),
                "kwargs": kwargs,
            })

        return (nodes, uasset_path)

    @classmethod
    def parse_config_json(cls, cfg_file):
        ''' Parses nDisplay config file of type json '''
        jsstr = open(cfg_file, 'r').read()
        return cls.parse_config_json_string(jsstr)

    @classmethod
    def parse_config(cls, cfg_file) -> Tuple[List, Optional[str]]:
        '''
        Parses an nDisplay file and returns the nodes with the relevant
        information.
        '''
        ext = os.path.splitext(cfg_file)[1].lower()

        try:
            if ext == '.ndisplay':
                return cls.parse_config_json(cfg_file)

            if ext == '.uasset':
                return cls.parse_config_uasset(cfg_file)

        except Exception as e:
            LOGGER.error(
                f'Error while parsing nDisplay config file "{cfg_file}": {e}')
            return ([], None)

        LOGGER.error(f'Unknown nDisplay config file "{cfg_file}"')
        return ([], None)

    def update_settings_controlled_by_config(self, cfg_file):
        '''
        Updates settings that are exclusively controlled by the config file.
        '''
        nodes, self.bp_object_path = self.__class__.parse_config(cfg_file)

        # find which node is self:
        try:
            menode = next(node for node in nodes if node['name'] == self.name)
        except StopIteration:
            LOGGER.error(f"{self.name} not found in config file {cfg_file}")
            return

        self.nodeconfig = menode

        self.settings['window_position'].update_value(
            menode['kwargs'].get("window_position", (0, 0)))
        self.settings['window_resolution'].update_value(
            menode['kwargs'].get("window_resolution", (100, 100)))
        self.settings['fullscreen'].update_value(
            menode['kwargs'].get("fullscreen", False))

    @classmethod
    def uasset_path_from_object_path(
            cls, object_path: str, project_dir: str) -> str:
        '''
        Given a full object path, return the package file path, treating
        "`project_dir`/Content/" as "/Game/".
        '''
        expected_root = '/Game/'
        if not object_path.startswith(expected_root):
            raise ValueError('Unsupported object path root')

        # If object_path is: /Game/PathA/PathB/Package.Object:SubObject
        # Then package_rel_path is: PathA/PathB/Package
        path_end_idx = object_path.rindex('/')
        package_end_idx = object_path.index('.', path_end_idx)
        package_rel_path = object_path[len(expected_root):package_end_idx]

        return os.path.normpath(
            os.path.join(project_dir, 'Content', f'{package_rel_path}.uasset'))

    def launch(self, map_name):
        if not self.check_settings_valid():
            LOGGER.error(f"{self.name}: Not launching due to invalid settings")
            self.widget._close()
            return

        self.map_name_to_launch = map_name

        # Update settings controlled exclusively by the nDisplay config file.
        cfg_file = DevicenDisplay.csettings['ndisplay_config_file'].get_value(
            self.name)
        try:
            self.update_settings_controlled_by_config(cfg_file)
        except Exception:
            LOGGER.error(
                f"{self.name}: Could not update from '{cfg_file}' before "
                "launch. \n\n=== Traceback BEGIN ===\n"
                f"{traceback.format_exc()}=== Traceback END ===\n")
            self.widget._close()
            return

        # Transfer config file
        if not os.path.exists(cfg_file):
            LOGGER.error(
                f"{self.name}: Could not find nDisplay config file "
                f"at {cfg_file}")
            self.widget._close()
            return

        cfg_ext = os.path.splitext(cfg_file)[1].lower()

        # If the config file is a uasset, we send it as well as the
        # extracted JSON.
        if cfg_ext == '.uasset':
            cfg_ext = '.ndisplay'
            cfg_content = self.__class__.extract_configexport_from_uasset(
                cfg_file).encode('utf-8')
        else:
            with open(cfg_file, 'rb') as f:
                cfg_content = f.read()

        # Apply local overrides to configuration (e.g. ip addresses)
        try:
            cfg_content = self.__class__.apply_local_overrides_to_config(
                cfg_content, cfg_ext).encode('utf-8')
        except Exception:
            LOGGER.error(
                'Could not parse config data when trying to override with '
                'local settings')
            self.widget._close()
            return

        cfg_destination = f"%TEMP%/ndisplay/%RANDOM%{cfg_ext}"

        self.pending_transfer_cfg = True
        _, cfg_msg = message_protocol.create_send_filecontent_message(
            cfg_content, cfg_destination)
        self.unreal_client.send_message(cfg_msg)

        if self.bp_object_path:
            local_uasset_path = self.uasset_path_from_object_path(
                self.bp_object_path, os.path.dirname(
                    CONFIG.UPROJECT_PATH.get_value()))
            dest_uasset_path = self.uasset_path_from_object_path(
                self.bp_object_path, os.path.dirname(
                    CONFIG.UPROJECT_PATH.get_value(self.name)))

            if os.path.isfile(local_uasset_path):
                self.pending_transfer_uasset = True
                _, uasset_msg = message_protocol.create_send_file_message(
                    local_uasset_path, dest_uasset_path, force_overwrite=True)
                self.unreal_client.send_message(uasset_msg)
            else:
                LOGGER.warning(
                    f'{self.name}: Could not find nDisplay '
                    f'uasset at {local_uasset_path}')

    @classmethod
    def plug_into_ui(cls, menubar, tabs):
        '''
        Implementation of base class function that allows plugin to inject
        UI elements.
        '''
        cls.create_monitor_if_necessary()

        # Create Monitor UI if it doesn't exist
        if not cls.ndisplay_monitor_ui:
            cls.ndisplay_monitor_ui = nDisplayMonitorUI(
                parent=tabs, monitor=cls.ndisplay_monitor)

        # Add our monitor UI to the main tabs in the UI
        tabs.addTab(cls.ndisplay_monitor_ui, 'nDisplay &Monitor')

    @classmethod
    def added_device(cls, device):
        '''
        Implementation of base class function. Called when one of our plugin
        devices has been added.
        '''
        super().added_device(device)

        if not cls.ndisplay_monitor:
            return

        cls.ndisplay_monitor.added_device(device)

    @classmethod
    def removed_device(cls, device):
        '''
        Implementation of base class function. Called when one of our plugin
        devices has been removed.
        '''
        super().removed_device(device)

        if not cls.ndisplay_monitor:
            return

        cls.ndisplay_monitor.removed_device(device)

    @classmethod
    def send_cluster_event(cls, devices, cluster_event):
        '''
        Sends a cluster event (to the master, which will replicate to the rest
        of the cluster).
        '''

        # find the master node

        master = None

        for device in devices:
            if (device.name ==
                    DevicenDisplay.csettings[
                        'master_device_name'].get_value()):
                master = device
                break

        if master is None:
            LOGGER.warning('Could not find master device when trying to send '
                           'cluster event. Please make sure the master '
                           'device is marked as such.')
            raise ValueError

        msg = bytes(json.dumps(cluster_event), 'utf-8')
        msg = struct.pack('I', len(msg)) + msg

        # We copy the original master's cluster event port (port_ce) to the
        # config of all nodes, so we can use the port from the overridden
        # master.
        port = master.nodeconfig['port_ce']

        # Use the overridden ip address of this node
        ip = master.ip_address

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((ip, port))
        sock.send(msg)

    @classmethod
    def soft_kill_cluster(cls, devices):
        ''' Kills the cluster by sending a message to the master. '''
        LOGGER.info('Issuing nDisplay cluster soft kill')

        quit_event = {
            "bIsSystemEvent": "true",
            "Category": "nDisplay",
            "Type": "control",
            "Name": "quit",
            "Parameters": {},
        }

        cls.send_cluster_event(devices, quit_event)

    @classmethod
    def console_exec_cluster(cls, devices, exec_str, executor=''):
        ''' Executes a console command on the cluster. '''
        LOGGER.info(f"Issuing nDisplay cluster console exec: '{exec_str}'")

        exec_event = {
            "bIsSystemEvent": "true",
            "Category": "nDisplay",
            "Type": "control",
            "Name": "console exec",
            "Parameters": {
                "ExecString": exec_str,
                "Executor": executor,
            },
        }

        cls.send_cluster_event(devices, exec_event)

    @classmethod
    def close_all(cls, devices):
        ''' Closes all devices in the plugin '''
        try:
            cls.soft_kill_cluster(devices)

            for device in devices:
                device.device_qt_handler.signal_device_closing.emit(device)
        except Exception:
            LOGGER.warning("Could not soft kill the cluster")

    @classmethod
    def all_devices_added(cls):
        ''' Device interface implementation '''

        super().all_devices_added()

        devices = [dev for dev in cls.active_unreal_devices
                   if dev.device_type == 'nDisplay']

        # If no devices were added, there is nothing to do.
        if len(devices) == 0:
            return

        # Verify that there is a device that matches our master selection
        master_device_name = DevicenDisplay.csettings[
            'master_device_name'].get_value()
        master_found = False

        for device in devices:
            if device.name == master_device_name:
                master_found = True
                break

        # If there isn't a master selection (e.g. old config), try to assign
        # one based on the nDisplay config.
        if not master_found:
            cfg_file = DevicenDisplay.csettings[
                'ndisplay_config_file'].get_value()

            try:
                (nodes, _) = DevicenDisplay.parse_config(cfg_file)
            except (IndexError, KeyError):
                LOGGER.error(f"Error parsing nDisplay config file {cfg_file}")
            else:
                for node in nodes:
                    if node['master']:
                        for device in devices:
                            if node['name'] == device.name:
                                cls.select_device_as_master(device)
                                master_found = True
                                break

                        break

            # If we still don't have a master, log the error.
            if not master_found:
                LOGGER.error('Could not find or assign master device in the '
                             f'current devices and config file "{cfg_file}"')
