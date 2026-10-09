"""Convert exported fixed-shape ONNX models with RKNN Toolkit2 2.3.2."""
import argparse
from pathlib import Path
from rknn.api import RKNN

p = argparse.ArgumentParser()
p.add_argument('model_dir', type=Path)
a = p.parse_args()
for name in ('encoder', 'decoder'):
    model = RKNN(verbose=False)
    try:
        result = model.config(target_platform='rk3568')
        if result != 0:
            raise RuntimeError(f'{name}: config failed: {result}')
        result = model.load_onnx(model=str(a.model_dir / (name + '.onnx')))
        if result != 0:
            raise RuntimeError(f'{name}: load failed: {result}')
        result = model.build(do_quantization=False)
        if result != 0:
            raise RuntimeError(f'{name}: build failed: {result}')
        result = model.export_rknn(str(a.model_dir / (name + '.rknn')))
        if result != 0:
            raise RuntimeError(f'{name}: export failed: {result}')
    finally:
        model.release()
