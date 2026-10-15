"""Measure RGB32F output error against a reference; no bit-equivalence assumption."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np


def compare(reference, candidate):
    if Path(reference).stat().st_size % 12 or Path(candidate).stat().st_size % 12:
        raise ValueError('RGB32F files must contain complete three-channel pixels')
    a = np.fromfile(reference, dtype='<f4').astype(np.float64)
    b = np.fromfile(candidate, dtype='<f4').astype(np.float64)
    if not a.size or a.size != b.size or a.size % 3:
        raise ValueError('Matching nonempty RGB32F planes required')
    if not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError('Nonfinite reference or candidate output')
    delta = np.abs(a-b)
    mse = float(np.mean((a-b)**2))
    return {'reference_sha256': hashlib.sha256(Path(reference).read_bytes()).hexdigest(),
        'candidate_sha256': hashlib.sha256(Path(candidate).read_bytes()).hexdigest(),
        'channels': int(a.size), 'finite': True, 'bit_exact': Path(reference).read_bytes()==Path(candidate).read_bytes(),
        'mean_absolute_error': float(delta.mean()), 'p99_absolute_error': float(np.percentile(delta,99)),
        'maximum_absolute_error': float(delta.max()), 'psnr_db_unit_range': float(-10*np.log10(mse)) if mse else None,
        'candidate_min': float(b.min()), 'candidate_max': float(b.max()),
        'scope': 'Numerical output difference at this fixture and unit signal range; not a perceptual or temporal quality guarantee'}


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--report', type=Path)
    args=parser.parse_args()
    result=compare(args.reference,args.candidate)
    text=json.dumps(result,indent=2,allow_nan=False)+'\n'
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(text,encoding='utf-8')
    print(text,end='')
