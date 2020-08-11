# UE4ML
###### please note that this plugin is very much experimental and work-in-progress

The goal of the UE4ML plugin is to supply external clients (like a python script) with an ability to interface with a running UE4 instance in an organized fashion.

A client can connect to an UE4 instance and using RPCs (remote procedure calls) add in-game agents, configure them (declaring what kind of Sensors and Actuators the agent needs), get data from the world (collected by agent’s sensors) and affect the world (via agent’s actuators). 

An optional but a very important part of the UE4ML plugin is the accompanying python package (ue4ml) that adheres to OpenAI Gym’s API making it easy to work with UE4ML-plugin-empowered-UE4-games/projects just like with any other OpenAI Gym environment. The python package makes such a UE4 game/project a regular OpenAI Gym environment (http://gym.openai.com/docs/) which means users can interact with a UE4 instance without changing their pipelines/workflows. At the moment the python code has no examples of agent training - right now we’re just supplying new environments to interact with.

The following diagram presents an overview of UE4ML's architecture.![img](https://lh4.googleusercontent.com/XLJdhbeFCKODMhDP1M2tYFO3vND7LDtdcsiqaaHFgH08Izo9mDUx8YVNsPNo_aIdp2DISDN1hMhRkeK1VFRpJfxmsbOg_C9uGfxm_-5AWn-Mcqy_hMOQBuIgArJtVk2iSguV6MWB)

Please note that the plugin is still in very early development. All feedback is highly encouraged. InstallationC++The plugin’s source code can be found in Engine/Plugins/AI/UE4ML. I also strongly suggest getting the latest `Samples/Games/ActionRPG`, as well as the last possible (recently removed) `PlatformerGame` version, as I've made some minor modifications to those samples making them cooperate better with the plugin. By default the plugin is not enabled for those games. To enable it add the following section to game’s *.uproject, right after the “Modules” section:

```
"Plugins": [		
	{
		"Name": "UE4ML",
		"Enabled": true
	}
],
```

Note that it’s not necessary to regenerate project files after this change, the UBT will pick the change up automatically.

## Python

#### Getting python

First you need python on your machine. The absolute easiest way to get it is to download an installer from https://www.python.org/downloads/ (3.7.x would be best) and install it with default settings. If asked, agree to adding python to environment variables.

#### ue4ml package
The ue4ml python package can be found in `Engine/Plugins/AI/UE4ML/Source/python`. To add the package to your python distribution just call the following in the `UE4ML/Source/python` directory:

```
pip install -e .
```
The installation script will install the package's dependencies but if it turns out something’s missing please let me know.

## Running
On the C++ side all one needs to do is to enable the plugin for the given project and compile (to ensure UE4ML plugin’s binaries are up to date).

On the python side, the ue4ml package supports both connecting to a running instance of UE4 as well as launching one. Launching does require the user to add UE-DevBinaries to environment variables (pointing at the **directory** where the executable can be found, `UE4Editor-Win64-Debug.exe` or `UE4Editor.exe` will be used, depending on `ue4ml.runner:_DEBUG`) or adding the `--exec` parameter to python script’s execution. The --exec should point at a specific executable **file** to use.

Example:

![img](https://lh6.googleusercontent.com/1XNDfYnvt0WAwqaw7uzXTmnkDk0Lg8PWOIU9qBhnPeziirOum_agpMz5GgLehbiK0i6LYTLZd7JDvLpfCl9ejcEWFe_VUjjYr_LkF_xaDS4csq-uXK4BX5PQljvzMNbWe0mV_VjZ)

or: 

```
python custom_params.py --exec=d:/p4/df/Engine/Binaries/Win64/UE4Editor.exe
```
where `custom_params.py` is a script using the ue4ml package. Both ways will result in the same UE4 binary being used.

## Example scripts
The `UE4ML/Source/python/examples` directory contains example scripts one can use to construct/run/connect to a running UE4 game.

- `as_gym_env.py` - uses OpenAI Gym’s `gym.make` to construct an environment. 
- `custom_params.py` - hand-creates an ue4ml environment, configures it and runs (including launching the UE4 instance)
- `connect_to_running.py` - connects to a running instance of an UE4 game/project.

So assuming you have your UE-DevBinaries environment variable set up you can just navigate to the examples directory and run the following command:
```
python custom_params.py
```
When executed this script will launch ActionRPG (make sure you have it synced!), connect to it, do a one playthrough (executing random actions) and close both the script and the UE4 instance as soon as one playthrough is done.

## Current limitations
There’s a lot more, but here are some highlights:

- The camera’s sensor implementation is very naïve (which affects perf). Proper implementation pending.
- only the Windows platform is supported at the moment and I've tested it only in Win64. Note that the rpclib does support other platforms, we "just" need to compile rpclib for those platforms and it should work. 

## Practical advice
#### General
If your client doesn’t seem to be able to connect to the rpc server try using a different port. It’s the `-4mlport=` option when launching UE4 instance and `server_port` parameter of ue4ml environment’s constructor.

#### Python
When manually creating ue4ml environments or connecting to one you want to debug on the C++ side it’s useful to add timeout parameter to environment’s constructor, like so
```
env = ActionRPG(timeout=3600)
```
This will make sure the rpcclient won’t disconnect while you debug the C++ side (well, it will, after an hour!).

## Feedback
Please send your feedback to mieszko.zielinski@epicgames.com