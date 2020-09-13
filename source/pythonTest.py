import numpy as np
from dlclive import DLCLive, Processor

def setModelPath(modelPath, resizeVal):
    global dlc_live
    dlc_live = DLCLive(modelPath, resize=resizeVal, display=True)
    print(dlc_live.path)
    return 0

def initInference(image):
    dlc_live.init_inference(image)

def getPose(image):
    return dlc_live.get_pose(image)
