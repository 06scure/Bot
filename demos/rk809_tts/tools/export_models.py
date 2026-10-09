"""Export the supplied Chinese checkpoint using mmontol/MeloTTS's split model.

Run with the Linux RKNN environment (torch 2.4), without loading TTS API/BERT.
"""
import argparse
import json
from pathlib import Path
import sys

p = argparse.ArgumentParser()
p.add_argument('--source', required=True)
p.add_argument('--checkpoint', required=True)
p.add_argument('--config', required=True)
p.add_argument('--output', required=True)
a = p.parse_args()
sys.path.insert(0, str(Path(a.source).resolve()))
import torch
import onnx
import onnxsim
from melo.models import SynthesizerTrn

torch.set_num_threads(4)
torch.manual_seed(42)
c = json.loads(Path(a.config).read_text())
m = SynthesizerTrn(len(c['symbols']), c['data']['filter_length']//2+1,
                   c['train']['segment_size']//c['data']['hop_length'],
                   n_speakers=c['data']['n_speakers'], num_tones=c['num_tones'],
                   num_languages=c['num_languages'], **c['model']).eval()
m.load_state_dict(torch.load(a.checkpoint, map_location='cpu', weights_only=True)['model'], strict=True)
out = Path(a.output)
out.mkdir(parents=True, exist_ok=True)
x = torch.zeros(1, 256, dtype=torch.int64)
langs = x.clone()
langs[:, 1::2] = 3
enc = (x, torch.tensor([256]), torch.tensor([c['data']['spk2id']['ZH']]),
       x.clone(), langs, torch.zeros(1, 768, 256), torch.tensor([.8]), torch.tensor([.2]))
dec = (torch.zeros(1,512,256), torch.ones(1,1,512), torch.zeros(1,256,1),
       torch.zeros(1,192,256), torch.zeros(1,192,256), torch.tensor([.6]))
with torch.no_grad():
    for name, forward, inputs, names, outputs in [
        ('encoder', m.forward_encoder, enc,
         ['x','x_lengths','sid','tone','lang_ids','ja_bert','noise_scale_w','sdp_ratio'],
         ['logw','x_mask','g','m_p','logs_p']),
        ('decoder', m.forward_decoder, dec,
         ['attn','y_mask','g','m_p','logs_p','noise_scale'], ['y'])]:
        m.forward = forward
        path = out / (name + '.onnx')
        torch.onnx.export(m, inputs, str(path), opset_version=16,
                          input_names=names, output_names=outputs)
        simplified, valid = onnxsim.simplify(str(path))
        if not valid:
            raise RuntimeError('ONNX simplification validation failed')
        onnx.checker.check_model(simplified)
        onnx.save(simplified, str(path))
        print('EXPORTED', path, flush=True)
