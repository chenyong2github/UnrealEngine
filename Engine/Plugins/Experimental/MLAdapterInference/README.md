# MLAdapterInference
###### Please note that this plugin is very much experimental and work-in-progress.

This plugin extends the functionality of MLAdapter by adding inference specific classes. Once you have finished developing an ML model using MLAdapter and are ready to ship it in a game, you most likely no longer want to have a dependency on the external Python process. This plugin adds an UMLAdapterInferenceManager class so that you will no longer start an RPC server, and also allows you to directly evaluate your model in engine by using Epic's Neural Network Inference (NNI) Engine. This functionality is provided through UMLAdapterInferenceAgent.

## Feedback
Please send your feedback to brendan.mulcahy@epicgames.com