import sys
import sleap
import numpy as np

class MiniSLEAP:
    def __init__(self, *arg):
        self.modelPath = list()
        for a in arg:
            self.modelPath.append(a)

    def setupSLEAP(self):
        self.predictor = sleap.load_model(modelPath, refinement="local",tracker=None)
        self.model = self.predictor.inference_mode

    def getPose(self, image):
#        image = image[..., ::-1]  # BGR -> RGB
        self.pred = self.model.predict(image[None, ...])
        self.peaks = pred["instance_peaks"][0]

        return self.peaks




