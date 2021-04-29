# Copyright Epic Games, Inc. All Rights Reserved.

from switchboard import message_protocol
from switchboard import switchboard_utils as sb_utils
from switchboard.config import CONFIG, Setting, SETTINGS
from switchboard.devices.device_base import Device
from switchboard.devices.device_widget_base import AddDeviceDialog
from switchboard.devices.unreal.plugin_unreal import DeviceUnreal, DeviceWidgetUnreal
from switchboard.switchboard_logging import LOGGER
from switchboard.devices.unreal.uassetparser import UassetParser

from .ndisplay_monitor_ui import nDisplayMonitorUI
from .ndisplay_monitor    import nDisplayMonitor

from PySide2 import QtWidgets, QtCore

import os, traceback, json, socket, struct, fnmatch
from pathlib import Path
import concurrent.futures


class AddnDisplayDialog(AddDeviceDialog):
    def __init__(self, existing_devices, parent=None):
        super().__init__(device_type="nDisplay", existing_devices=existing_devices, parent=parent)

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
        self.cbConfigs.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Preferred)
        self.cbConfigs.setEditable(True) # to allow the user to type the value

        # Create layout for the config file selection widgets
        file_selection_layout = QtWidgets.QHBoxLayout()
        file_selection_layout.addWidget(self.cbConfigs)
        file_selection_layout.addWidget(self.btnBrowse)
        file_selection_layout.addWidget(self.btnFindConfigs)

        self.form_layout.addRow("Config File", file_selection_layout)

        # Add a spacer right before the ok/cancel buttons
        spacer_layout = QtWidgets.QHBoxLayout()
        spacer_layout.addItem(QtWidgets.QSpacerItem(20, 20, QtWidgets.QSizePolicy.Minimum, QtWidgets.QSizePolicy.Expanding))
        self.form_layout.addRow("", spacer_layout)

        # Find existing nDisplay devices in order to issue a warning about replacing them
        self.existing_ndisplay_devices = []

        for device in existing_devices:
            if device.device_type == "nDisplay":
                self.existing_ndisplay_devices.append(device)

        if self.existing_ndisplay_devices:
            self.layout().addWidget(QtWidgets.QLabel("Warning! All existing nDisplay devices will be replaced."))

        # populate the config combobox with the items last populated.
        self.recall_config_itemDatas()

    def recall_config_itemDatas(self):
        ''' Populate the config combobox with the already found configs, avoiding having to re-find every time '''
        self.cbConfigs.clear()

        itemDatas = DevicenDisplay.csettings['populated_config_itemDatas'].get_value()

        try:
            for itemData in itemDatas:
                self.cbConfigs.addItem(itemData['name'], itemData)
        except:
            LOGGER.error('Error recalling config itemDatas')

    def current_config_path(self):
        ''' Get currently selected config path in the combobox '''

        # Detect if this is a manual entry using the itemData

        itemText = self.cbConfigs.currentText()
        itemData = self.cbConfigs.currentData()

        if (self.cbConfigs.currentData() is None) or (itemData['name'] != itemText):
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
            DevicenDisplay.csettings['ndisplay_config_file'].update_value(config_path)

        return res

    def on_clicked_btnBrowse(self):
        ''' Opens a file dialog to browse for the config file
        '''
        start_path = str(Path.home())

        if SETTINGS.LAST_BROWSED_PATH and os.path.exists(SETTINGS.LAST_BROWSED_PATH):
            start_path = SETTINGS.LAST_BROWSED_PATH

        cfg_path, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Select nDisplay config file", start_path, "nDisplay Config (*.cfg;*.ndisplay;*.uasset)")

        if len(cfg_path) > 0 and os.path.exists(cfg_path):
            self.cbConfigs.setCurrentText(cfg_path)
            SETTINGS.LAST_BROWSED_PATH = os.path.dirname(cfg_path)
            SETTINGS.save()

    def on_clicked_btnFindConfigs(self):
        ''' Finds and populates config combobox '''
        # We will look for config files in the project's Content folder
        configs_path = os.path.normpath(os.path.join(os.path.dirname(CONFIG.UPROJECT_PATH.get_value().replace('"','')), 'Content'))

        config_names = []
        config_paths = []

        assets = []

        for dirpath, _, files in os.walk(configs_path):
            for name in files:
                if not name.lower().endswith(('.uasset','.cfg','.ndisplay')):
                    continue

                if name not in config_names:
                    config_path = os.path.join(dirpath, name)
                    ext = os.path.splitext(name)[1]

                    # Since .uasset is generic a asset container, only add assets of the right config class
                    if ext.lower() == '.uasset':
                        assets.append({'name': name, 'path': config_path})
                    else:
                        config_names.append(name)
                        config_paths.append(config_path)     

        # process the assets in a multi-threaded fashion

        # show a progress bar if it is taking more a trivial amount of time
        progressDiag = QtWidgets.QProgressDialog('Parsing assets...','Cancel', 0, 0, parent=self)
        progressDiag.setWindowTitle('nDisplay Config Finder')
        progressDiag.setModal(True)
        progressDiag.setMinimumDuration(1000) # time before it shows up
        progressDiag.setRange(0,len(assets))
        progressDiag.setCancelButton(None)
        progressDiag.setWindowFlag(QtCore.Qt.FramelessWindowHint) # Looks much better without the window frame

        def validateConfigAsset(asset):
            ''' Returns the asset if it is an nDisplay config '''
            aparser = UassetParser(asset['path'], allowUnversioned=True)
            for assetdata in aparser.aregdata:
                if assetdata.ObjectClassName == 'DisplayClusterBlueprint':
                    return asset
            raise ValueError

        numThreads = 8
        doneAssetCount = 0

        with concurrent.futures.ThreadPoolExecutor(max_workers=numThreads) as executor:
            futures = [executor.submit(validateConfigAsset, asset) for asset in assets]

            for future in concurrent.futures.as_completed(futures):

                #update progress bar
                doneAssetCount += 1
                progressDiag.setValue(doneAssetCount)

                # get the future result and add to list of config names and paths
                try:
                    asset = future.result()
                    if asset['name'] not in config_names:
                        config_names.append(asset['name'])
                        config_paths.append(asset['path'])
                except:
                    pass

        # close progress bar window
        progressDiag.close()

        # collect the found config files into the itemDatas list

        itemDatas = []

        for idx, config_name in enumerate(config_names):
            itemData = {'name': config_name, 'path':config_paths[idx]}
            itemDatas.append(itemData)

        # sort by name
        itemDatas.sort(key=lambda itemData: itemData['name'])

        # update settings that should survive device removal and addition
        DevicenDisplay.csettings['populated_config_itemDatas'].update_value(itemDatas)

        # update the combo box with the items.
        self.recall_config_itemDatas()


    def devices_to_add(self):
        cfg_file = self.current_config_path()
        try:
            devices = DevicenDisplay.parse_config(cfg_file)
            if len(devices) == 0:
                LOGGER.error(f"Could not read any devices from nDisplay config file {cfg_file}")
            return devices
        except (IndexError, KeyError):
            LOGGER.error(f"Error parsing nDisplay config file {cfg_file}")
            return []

    def devices_to_remove(self):
        return self.existing_ndisplay_devices

class DeviceWidgetnDisplay(DeviceWidgetUnreal):
    pass

class DevicenDisplay(DeviceUnreal):

    add_device_dialog = AddnDisplayDialog
    
    csettings = {
        'ndisplay_config_file': Setting(
            attr_name="ndisplay_cfg_file", 
            nice_name="nDisplay Config File", 
            value="", 
            tool_tip="Path to nDisplay config file"
        ),
        'use_all_available_cores': Setting(
            attr_name="use_all_available_cores", 
            nice_name="Use All Available Cores", 
            value=False,
        ),
        'texture_streaming': Setting(
            attr_name="texture_streaming", 
            nice_name="Texture Streaming", 
            value=True,
        ),
        'render_api': Setting(
            attr_name="render_api", 
            nice_name="Render API", 
            value="dx12", 
            possible_values=["dx11", "dx12"],
        ),
        'render_mode': Setting(
            attr_name="render_mode", 
            nice_name="Render Mode", 
            value="Mono", 
            possible_values=["Mono", "Frame sequential", "Side-by-Side", "Top-bottom"]
        ),
        'ndisplay_cmd_args': Setting(
            attr_name="ndisplay_cmd_args", 
            nice_name="Extra Cmd Line Args", 
            value="",
        ),
        'ndisplay_exec_cmds': Setting(
            attr_name="ndisplay_exec_cmds", 
            nice_name='ExecCmds', 
            value="", 
            tool_tip=f'ExecCmds to be passed. No need for outer double quotes.',
        ),
        'ndisplay_dp_cvars': Setting(
            attr_name='ndisplay_dp_cvars',
            nice_name="DPCVars",
            value='',
            tool_tip="Device profile console variables (comma separated)."
        ),
        'max_gpu_count': Setting(
            attr_name="max_gpu_count", 
            nice_name="Number of GPUs",
            value=2,
            possible_values=list(range(1, 17)),
            tool_tip="If you have multiple GPUs in the PC, you can specify how many to use.",
        ),
        'priority_modifier': Setting(
            attr_name='priority_modifier',
            nice_name="Process Priority",
            value=sb_utils.PriorityModifier.Normal.name,
            possible_values=[p.name for p in sb_utils.PriorityModifier],
            tool_tip="Used to override the priority of the process.",
        ),
        'populated_config_itemDatas': Setting(
            attr_name='populated_config_itemDatas',
            nice_name="Populated Config Files",
            value=[], # populated by AddnDisplayDialog
            tool_tip="Remember the last populated list of config files",
            show_ui=False,
        ),
    }

    ndisplay_monitor_ui = None
    ndisplay_monitor = None

    def __init__(self, name, ip_address, **kwargs):
        super().__init__(name, ip_address, **kwargs)

        self.settings = {
            'ue_command_line': Setting(
                attr_name="ue_command_line", 
                nice_name="UE Command Line", 
                value=kwargs.get("ue_command_line", ''), 
                show_ui=True
            ),
            'window_position': Setting(
                attr_name="window_position", 
                nice_name="Window Position", 
                value=tuple(kwargs.get("window_position", (0,0))), 
                show_ui=False
            ),
            'window_resolution': Setting(
                attr_name="window_resolution", 
                nice_name="Window Resolution", 
                value=tuple(kwargs.get("window_resolution", (100,100))), show_ui=False
            ),
            'fullscreen': Setting(
                attr_name="fullscreen", 
                nice_name="fullscreen", 
                value=kwargs.get("fullscreen", False), show_ui=False
            ),
        }

        self.render_mode_cmdline_opts = {
            "Mono"            : "-dc_dev_mono", 
            "Frame sequential": "-quad_buffer_stereo", 
            "Side-by-Side"    : "-dc_dev_side_by_side", 
            "Top-bottom"      : "-dc_dev_top_bottom"
        }

        self.path_to_config_on_host = DevicenDisplay.csettings['ndisplay_config_file'].get_value()

        if len(self.settings['ue_command_line'].get_value()) == 0:
            self.generate_unreal_command_line()

        CONFIG.ENGINE_DIR.signal_setting_overriden.connect(self.on_cmdline_affecting_override)
        CONFIG.ENGINE_DIR.signal_setting_changed.connect(self.on_change_setting_affecting_command_line)
        CONFIG.UPROJECT_PATH.signal_setting_overriden.connect(self.on_cmdline_affecting_override)
        CONFIG.UPROJECT_PATH.signal_setting_changed.connect(self.on_change_setting_affecting_command_line)

        # common settings affect instance command line
        for csetting in self.__class__.plugin_settings():
            csetting.signal_setting_changed.connect(self.on_change_setting_affecting_command_line)

        self.unreal_client.send_file_completed_delegate = self.on_ndisplay_config_transfer_complete
        self.unreal_client.delegates['get sync status'] = self.on_get_sync_status

        # create monitor if it doesn't exist
        self.__class__.create_monitor_if_necessary()

        # node configuration (updated from config file)
        self.nodeconfig = {}

        try:
            cfg_file = DevicenDisplay.csettings['ndisplay_config_file'].get_value(self.name)
            self.update_settings_controlled_by_config(cfg_file)
        except:
            LOGGER.error(f"{self.name}: Could not update from '{cfg_file}' during initialization. \n\n=== Traceback BEGIN ===\n{traceback.format_exc()}=== Traceback END ===\n")


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
        ''' This is given to the config, so that it knows to save them when they change.
        settings_dialog.py will try to create a UI for each setting if setting.show_ui is True.
        '''
        return super().device_settings() + list(self.settings.values())

    def setting_overrides(self):
        return [
            DevicenDisplay.csettings['ndisplay_cmd_args'],
            DevicenDisplay.csettings['ndisplay_exec_cmds'],
            DevicenDisplay.csettings['ndisplay_dp_cvars'],
            DevicenDisplay.csettings['max_gpu_count'],
            DevicenDisplay.csettings['priority_modifier'],
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

    def generate_unreal_command_line(self, map_name=""):
        uproject = os.path.normpath(CONFIG.UPROJECT_PATH.get_value(self.name))

        # Device profile CVars
        dp_cvars = f"{self.csettings['ndisplay_dp_cvars'].get_value(self.name)}".strip()
        dp_cvars = f'-DPCVars="{dp_cvars}"' if len(dp_cvars) else ''

        # Extra arguments specified in settings
        additional_args = self.csettings['ndisplay_cmd_args'].get_value(self.name)

        # Path to config file
        cfg_file = self.path_to_config_on_host

        # nDisplay window info
        win_pos = self.settings['window_position'].get_value()
        win_res = self.settings['window_resolution'].get_value()
        fullscreen = self.settings['fullscreen'].get_value()

        # Misc settings
        render_mode = self.render_mode_cmdline_opts[DevicenDisplay.csettings['render_mode'].get_value(self.name)]
        render_api = f"-{DevicenDisplay.csettings['render_api'].get_value(self.name)}"
        use_all_cores = "-useallavailablecores" if DevicenDisplay.csettings['use_all_available_cores'].get_value(self.name) else ""
        no_texture_streaming = "-notexturestreaming" if not DevicenDisplay.csettings['texture_streaming'].get_value(self.name) else ""

        # MaxGPUCount (mGPU)
        max_gpu_count = DevicenDisplay.csettings["max_gpu_count"].get_value(self.name)
        try:
            max_gpu_count = f"-MaxGPUCount={max_gpu_count}" if int(max_gpu_count) > 1 else ''
        except ValueError:
            LOGGER.warning(f"Invalid Number of GPUs '{max_gpu_count}'")
            max_gpu_count = ''

        # Overridden classes at runtime
        ini_engine = "-ini:Engine"\
            ":[/Script/Engine.Engine]:GameEngine=/Script/DisplayCluster.DisplayClusterGameEngine"\
            ",[/Script/Engine.Engine]:GameViewportClientClassName=/Script/DisplayCluster.DisplayClusterViewportClient"\
            ",[/Script/Engine.UserInterfaceSettings]:bAllowHighDPIInGameMode=True"

        ini_game = "-ini:Game:[/Script/EngineSettings.GeneralProjectSettings]:bUseBorderlessWindow=True"

        # VP roles
        vproles, missing_roles = self.get_vproles()

        if missing_roles:
            LOGGER.error(f"{self.name}: Omitted roles not in the remote roles ini file which would cause UE to fail at launch: {'|'.join(missing_roles)}")

        vproles = '-VPRole=' + '|'.join(vproles) if vproles else ''

        # Session ID
        session_id = DeviceUnreal.csettings["stage_session_id"].get_value(self.name)
        session_id = f"-StageSessionId={session_id}" if session_id > 0 else ''

        # Friendly name. Avoid spaces to avoid parsing issues.
        friendly_name = f'-StageFriendlyName={self.name.replace(" ", "_")}'

        # fill in fixed arguments
        # TODO: Consider -unattended as an option to avoid crash window from appearing.
        args = [
            f'"{uproject}"',
            f'{map_name}',                     # map to open
            "-game",                           # render nodes run in -game
            "-messaging",                      # enables messaging, needed for MultiUser
            "-dc_cluster",                     # this is a cluster node
            "-nosplash",                       # avoids splash screen
            "-fixedseed",                      # for determinism
            "-NoVerifyGC",                     # improves performance
            "-noxrstereo",                     # avoids a conflict with steam/oculus
            "-RemoteControlIsHeadless",        # avoids notification window when using RemoteControlWebUI
            f'{additional_args}',              # specified in settings
            f'{vproles}',                      # VP roles for this instance
            f'{friendly_name}',                # Stage Friendly Name
            f'{session_id}',                   # Session ID. 
            f'{max_gpu_count}',                # Max GPU count (mGPU)
            f'{dp_cvars}',                     # Device profile CVars
            f'-dc_cfg="{cfg_file}"',           # nDisplay config file
            f'{render_api}',                   # dx11/12
            f'{render_mode}',                  # mono/...
            f'{use_all_cores}',                # -useallavailablecores
            f'{no_texture_streaming}',         # -notexturestreaming
            f'-dc_node={self.name}',           # name of this node in the nDisplay cluster
            f'Log={self.name}.log',            # log file
            f'{ini_engine}',                   # Engine ini injections
            f'{ini_game}',                     # Game ini injections
        ]

        # fill in ExecCmds
        exec_cmds = f'{self.csettings["ndisplay_exec_cmds"].get_value(self.name)}'.strip().split(',')
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
                f'-windowed',
                f'-forceres',
                f'WinX={win_pos[0]}',
                f'WinY={win_pos[1]}',
                f'ResX={win_res[0]}',
                f'ResY={win_res[1]}',
            ])

        # MultiUser parameters
        if CONFIG.MUSERVER_AUTO_JOIN:
            args.extend([
                f'-CONCERTRETRYAUTOCONNECTONERROR',
                f'-CONCERTAUTOCONNECT', 
                f'-CONCERTSERVER={CONFIG.MUSERVER_SERVER_NAME}',
                f'-CONCERTSESSION={SETTINGS.MUSERVER_SESSION_NAME}', 
                f'-CONCERTDISPLAYNAME={self.name}',
                f'-CONCERTISHEADLESS',
            ])

        # TODO: Make logs optional and selectable
        #args += f' -LogCmds="LogDisplayClusterPlugin Log, LogDisplayClusterEngine Log, LogDisplayClusterConfig Log, LogDisplayClusterCluster Log, LogDisplayClusterGame Log, LogDisplayClusterGameMode Log, LogDisplayClusterInput Log, LogDisplayClusterInputVRPN Log, LogDisplayClusterNetwork Log, LogDisplayClusterNetworkMsg Log, LogDisplayClusterRender Log, LogDisplayClusterRenderSync Log, LogDisplayClusterBlueprint Log" '

        path_to_exe = self.generate_unreal_exe_path()
        args_expanded = ' '.join(args)
        self.settings['ue_command_line'].update_value(f"{path_to_exe} {args_expanded}")

        return path_to_exe, args_expanded

    def on_ndisplay_config_transfer_complete(self, destination):
        LOGGER.info(f"{self.name}: nDisplay config file was successfully transferred to {destination} on host")
        self.path_to_config_on_host = destination
        super().launch(map_name=self.map_name_to_launch)

    def on_get_sync_status(self, message):
        ''' Called when 'get sync status' is received. '''
        self.__class__.ndisplay_monitor.on_get_sync_status(device=self, message=message)

    @classmethod
    def extract_configexport_from_uasset(cls, cfg_file):
        ''' Extract the configexport from the config uasset '''

        # Initialize uasset parser
        aparser = UassetParser(cfg_file)

        # Check that the asset is of the right class, then find its ConfigExport tag and parse it.
        for assetdata in aparser.aregdata:
            if assetdata.ObjectClassName == 'DisplayClusterBlueprint':
                return assetdata.tags['ConfigExport']

        raise ValueError('Invalid nDisplay config .uasset')

    @classmethod
    def parse_config_uasset(cls, cfg_file):
        ''' Parses nDisplay config file of type uasset '''
        jsstr = cls.extract_configexport_from_uasset(cfg_file)
        return cls.parse_config_json_string(jsstr)

    @classmethod
    def parse_config_json_string(cls, jsstr):
        ''' Parses nDisplay config file of type json string '''

        js = json.loads(jsstr)

        nodes = []
        cnodes = js['nDisplay']['cluster']['nodes']
        masterNode = js['nDisplay']['cluster']['masterNode']

        for name, cnode in cnodes.items():

            kwargs = {"ue_command_line": ""}

            winx = int(cnode['window'].get('x', 0))
            winy = int(cnode['window'].get('y', 0))
            resx = int(cnode['window'].get('w', 0))
            resy = int(cnode['window'].get('h', 0))

            kwargs["window_position"] = (winx, winy)
            kwargs["window_resolution"] = (resx, resy)
            kwargs["fullscreen"] = bool(cnode.get('fullScreen', False)) # note the capital 'S'

            master = True if masterNode['id'] == name else False

            nodes.append({
                "name": name, 
                "ip_address": cnode['host'],
                "master": master,
                "port_ce": int(masterNode['ports']['ClusterEventsJson']),
                "kwargs": kwargs,
            })

        return nodes

    @classmethod
    def parse_config_json(cls, cfg_file):
        ''' Parses nDisplay config file of type json '''
        jsstr = open(cfg_file, 'r').read()
        return cls.parse_config_json_string(jsstr)

    @classmethod
    def parse_config_cfg(cls, cfg_file):
        ''' Parses nDisplay config file in the original format (currently version 23) '''
        nodes = []

        cluster_node_lines = []
        window_lines = []
        with open(cfg_file, 'r') as cfg:
            for line in cfg:
                line = line.strip().lower()
                if not line.startswith('#'):
                    if "[cluster_node]" in line:
                        cluster_node_lines.append(line)
                    elif "[window]" in line:
                        window_lines.append(line)

        for line in cluster_node_lines:

            def parse_value(key, line):
                val = line.split(f"{key}=")[1]
                val = val.split(' ', 1)[0]
                val = val.replace('"', '')
                return val

            name = parse_value(key='id', line=line)
            node_window = parse_value(key='window', line=line)

            try:
                port_ce = int(parse_value(key='port_ce', line=line))
            except (IndexError, ValueError):
                port_ce = 41003

            try:
                master = parse_value(key='master', line=line)
                master = True if (('true' in master.lower()) or (master == "1")) else False
            except:
                master = False

            kwargs = {
                "ue_command_line": "",
            }

            for window_line in window_lines:
                if node_window in window_line:
                    try:
                        winx = int(parse_value(key='winx', line=window_line))
                    except (IndexError,ValueError):
                        winx = 0

                    try:
                        winy = int(parse_value(key='winy', line=window_line))
                    except (IndexError,ValueError):
                        winy = 0

                    try:
                        resx = int(parse_value(key='resx', line=window_line))
                    except (IndexError,ValueError):
                        resx = 0

                    try:
                        resy = int(parse_value(key='resy', line=window_line))
                    except (IndexError,ValueError):
                        resy = 0

                    try:
                        fullscreen = window_line.split("fullscreen=")[1]
                        fullscreen = fullscreen.split(' ', 1)[0]
                        fullscreen = True if (('true' in fullscreen.lower()) or (fullscreen == "1")) else False
                    except IndexError:
                        fullscreen = False

                    kwargs["window_position"] = (int(winx), int(winy))
                    kwargs["window_resolution"] = (int(resx), int(resy))
                    kwargs["fullscreen"] = fullscreen

                    break

            addr = line.split("addr=")[1]
            addr = addr.split(' ', 1)[0]
            addr = addr.replace('"', '')

            nodes.append({
                "name": name, 
                "ip_address": addr,
                "master": master,
                "port_ce": port_ce,
                "kwargs": kwargs,
            })

        return nodes

    @classmethod
    def parse_config(cls, cfg_file):
        ''' Parses an nDisplay file and returns the nodes with the relevant information '''
        ext = os.path.splitext(cfg_file)[1].lower()

        try:
            if ext == '.ndisplay':
                return cls.parse_config_json(cfg_file)

            if ext == '.cfg':
                return cls.parse_config_cfg(cfg_file)

            if ext == '.uasset':
                return cls.parse_config_uasset(cfg_file)

        except Exception as e:
            LOGGER.error(f'Error while parsing nDisplay config file "{cfg_file}": {e}')
            return []

        LOGGER.error(f'Unknown nDisplay config file "{cfg_file}"')
        return []

    def update_settings_controlled_by_config(self, cfg_file):
        ''' Updates settings that are exclusively controlled by the config file '''
        nodes = self.__class__.parse_config(cfg_file)

        # find which node is self:
        try:
            menode = next(node for node in nodes if node['name'] == self.name)
        except StopIteration:
            LOGGER.error(f"{self.name} not found in config file {cfg_file}")
            return

        self.nodeconfig = menode

        self.settings['window_position'].update_value(menode['kwargs'].get("window_position", (0,0)))
        self.settings['window_resolution'].update_value(menode['kwargs'].get("window_resolution", (100,100)))
        self.settings['fullscreen'].update_value(menode['kwargs'].get("fullscreen", False))

    def launch(self, map_name):
        self.map_name_to_launch = map_name

        # Update settings controlled exclusively by the nDisplay config file.
        try:
            cfg_file = DevicenDisplay.csettings['ndisplay_config_file'].get_value(self.name)
            self.update_settings_controlled_by_config(cfg_file)
        except:
            LOGGER.error(f"{self.name}: Could not update from '{cfg_file}' before launch. \n\n=== Traceback BEGIN ===\n{traceback.format_exc()}=== Traceback END ===\n")
            self.widget._close()
            return

        # Transfer config file

        if not os.path.exists(cfg_file):
            LOGGER.error(f"{self.name}: Could not find nDisplay config file at {cfg_file}")
            self.widget._close()
            return

        ext = os.path.splitext(cfg_file)[1]

        # If the config file is a uasset, we send a .ndisplay instead to save bandwidth.
        if ext.lower() == '.uasset':
            ext = '.ndisplay'
            file_content = self.__class__.extract_configexport_from_uasset(cfg_file).encode('utf-8')
        else:
            with open(cfg_file, 'rb') as f:
                file_content = f.read()

        destination = f"%TEMP%/ndisplay/%RANDOM%{ext}"

        self._last_issued_command_id, msg = message_protocol.create_send_filecontent_message(file_content, destination)
        self.unreal_client.send_message(msg)

    @classmethod
    def plug_into_ui(cls, menubar, tabs):
        ''' Implementation of base class function that allows plugin to inject UI elements. '''
        cls.create_monitor_if_necessary()

        # Create Monitor UI if it doesn't exist
        if not cls.ndisplay_monitor_ui:
            cls.ndisplay_monitor_ui = nDisplayMonitorUI(parent=tabs, monitor=cls.ndisplay_monitor)

        # Add our monitor UI to the main tabs in the UI
        tabs.addTab(cls.ndisplay_monitor_ui, 'nDisplay &Monitor')

    @classmethod
    def added_device(cls, device):
        ''' Implementation of base class function. Called when one of our plugin devices has been added. '''
        super().added_device(device)

        if not cls.ndisplay_monitor:
            return

        cls.ndisplay_monitor.added_device(device)

    @classmethod
    def removed_device(cls, device):
        ''' Implementation of base class function. Called when one of our plugin devices has been removed. '''
        super().removed_device(device)

        if not cls.ndisplay_monitor:
            return

        cls.ndisplay_monitor.removed_device(device)

    @classmethod
    def send_cluster_event(cls, devices, cluster_event):
        # find the master nodes (only one expected to be master)
        masters = [dev for dev in devices if dev.nodeconfig.get('master',False)]
                
        if len(masters) != 1:
            LOGGER.warning(f"{len(masters)} masters detected but there can only be one")
            raise ValueError

        master = masters[0]

        msg = bytes(json.dumps(cluster_event), 'utf-8')
        msg = struct.pack('I', len(msg)) + msg

        port = master.nodeconfig['port_ce']
        ip = master.nodeconfig['ip_address']

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((ip, port))
        sock.send(msg)

    @classmethod
    def soft_kill_cluster(cls, devices):
        ''' Kills the cluster by sending a message to the master. '''
        LOGGER.info('Issuing nDisplay cluster soft kill')

        quit_event = {
            "bIsSystemEvent":"true",
            "Category":"nDisplay",
            "Type":"control", 
            "Name":"quit",
            "Parameters":{},
        }

        cls.send_cluster_event(devices, quit_event)

    @classmethod
    def console_exec_cluster(cls, devices, exec_str, executor=''):
        ''' Executes a console command on the cluster. '''
        LOGGER.info(f"Issuing nDisplay cluster console exec: '{exec_str}'")

        exec_event = {
            "bIsSystemEvent":"true",
            "Category":"nDisplay",
            "Type":"control",
            "Name":"console exec",
            "Parameters":{
                "ExecString":exec_str,
                "Executor":executor,
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
        except:
            LOGGER.warning("Could not soft kill the cluster")
        

