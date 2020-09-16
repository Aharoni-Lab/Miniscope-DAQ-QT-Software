import numpy as np
import cv2
import colorcet as cc
from dlclive import DLCLive, Processor

class MiniDLC:
    def __init__(self, modelPath, resizeVal):
        print("In init")
        self.modelPath = modelPath
        self.resize = resizeVal
        self.dlcLive = DLCLive(modelPath, resize=resizeVal, display=False)

    def sayHi(self):
        print(self.resize)
        return 0

    def getColors(self):
        # Minimic DLC's coloring
        colorArray = np.zeros((60,1),dtype=np.uint8)
        all_colors = getattr(cc, "bmy")
        colors = all_colors[:: int(len(all_colors) / 20)]
#        print(type(colors))
#        print(type(colors[0]))
#        print(len(colors))
        for idx in range(20):
            colors[idx] = colors[idx].lstrip("#")
#            print(colors[idx])
            colorArray[idx*3,0] = int(colors[idx][:2],16)
            colorArray[idx*3+1,0] = int(colors[idx][2:4],16)
            colorArray[idx*3+2,0] = int(colors[idx][4:6],16)
        return colorArray

    def initInference(self, image):
        self.pose = self.dlcLive.init_inference(image)
        return self.pose

    def getPose(self, image):
        self.pose = self.dlcLive.get_pose(image)
#        print(self.pose)
        return self.pose
